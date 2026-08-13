#include "db/sqlite3/sqlite3.h"
#include "nlohmann/json.hpp"

#include <openssl/crypto.h>
#include <openssl/evp.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <exception>
#include <thread>
#include <vector>

#include <fcntl.h>
#include <sys/statvfs.h>
#include <sys/utsname.h>
#include <sys/wait.h>
#include <unistd.h>

namespace {

using Clock = std::chrono::steady_clock;
using TimePoint = Clock::time_point;
namespace fs = std::filesystem;
using Json = nlohmann::json;

constexpr std::uint64_t kKiB = 1024;
constexpr std::uint64_t kMiB = 1024 * kKiB;
constexpr std::uint64_t kDefaultSeed = 0x4d595df4d0f33173ULL;
constexpr std::uint64_t kMaxBlobChunk = static_cast<std::uint64_t>(std::numeric_limits<int>::max());

// Keep benchmark values aligned with interaction_history.h without coupling this
// standalone target to unfinished production domain sources.
constexpr std::int32_t kScopeProtocol = 1;
constexpr std::int32_t kProtocolTypeHttp = 1;
constexpr std::int32_t kResultMatched = 1;
constexpr std::int32_t kHistorySideRequest = 1;
constexpr std::int32_t kPayloadRefBody = 1;
constexpr std::int32_t kPayloadContentBinary = 7;
constexpr std::int32_t kPayloadParsed = 1;
constexpr std::int32_t kObjectReady = 1;
constexpr std::int32_t kObjectDeleting = 2;
constexpr std::int32_t kObjectDeleted = 3;
constexpr std::int32_t kObjectFailed = 4;

std::int64_t MicrosSince(TimePoint start)
{
    return std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - start).count();
}

std::int64_t WallClockMillis()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

std::string Lower(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

std::string JsonEscape(const std::string &value)
{
    std::ostringstream out;
    for(const unsigned char c : value)
    {
        switch(c)
        {
        case '"': out << "\\\""; break;
        case '\\': out << "\\\\"; break;
        case '\n': out << "\\n"; break;
        case '\r': out << "\\r"; break;
        case '\t': out << "\\t"; break;
        default:
            if(c < 0x20)
            {
                out << "\\u" << std::hex << std::setw(4) << std::setfill('0')
                    << static_cast<unsigned int>(c) << std::dec << std::setfill(' ');
            }
            else
            {
                out << static_cast<char>(c);
            }
            break;
        }
    }
    return out.str();
}

[[noreturn]] void Fail(const std::string &message)
{
    throw std::runtime_error(message);
}

void CheckSqlite(sqlite3 *db, int rc, const std::string &operation)
{
    if(rc == SQLITE_OK || rc == SQLITE_ROW || rc == SQLITE_DONE)
    {
        return;
    }

    const char *detail = db ? sqlite3_errmsg(db) : "no database handle";
    Fail(operation + ": rc=" + std::to_string(rc) + ", error=" + detail);
}

void Exec(sqlite3 *db, const std::string &sql)
{
    char *error = nullptr;
    const int rc = sqlite3_exec(db, sql.c_str(), nullptr, nullptr, &error);
    if(rc != SQLITE_OK)
    {
        const std::string detail = error ? error : sqlite3_errmsg(db);
        sqlite3_free(error);
        Fail("sqlite exec failed: " + sql + ": " + detail);
    }
}

std::string QueryText(sqlite3 *db, const std::string &sql)
{
    sqlite3_stmt *stmt = nullptr;
    CheckSqlite(db, sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr),
        "prepare query: " + sql);

    const int step_rc = sqlite3_step(stmt);
    if(step_rc != SQLITE_ROW)
    {
        sqlite3_finalize(stmt);
        Fail("query returned no row: " + sql + ": rc=" + std::to_string(step_rc));
    }

    const unsigned char *text = sqlite3_column_text(stmt, 0);
    const std::string value = text ? reinterpret_cast<const char *>(text) : "";
    const int finalize_rc = sqlite3_finalize(stmt);
    CheckSqlite(db, finalize_rc, "finalize query: " + sql);
    return value;
}

std::int64_t QueryInt64(sqlite3 *db, const std::string &sql)
{
    sqlite3_stmt *stmt = nullptr;
    CheckSqlite(db, sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr),
        "prepare query: " + sql);
    const int step_rc = sqlite3_step(stmt);
    if(step_rc != SQLITE_ROW)
    {
        sqlite3_finalize(stmt);
        Fail("query returned no row: " + sql + ": rc=" + std::to_string(step_rc));
    }
    const std::int64_t value = sqlite3_column_int64(stmt, 0);
    const int finalize_rc = sqlite3_finalize(stmt);
    CheckSqlite(db, finalize_rc, "finalize query: " + sql);
    return value;
}

bool HasRows(sqlite3 *db, const std::string &sql)
{
    sqlite3_stmt *stmt = nullptr;
    CheckSqlite(db, sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr),
        "prepare query: " + sql);
    const int step_rc = sqlite3_step(stmt);
    if(step_rc != SQLITE_ROW && step_rc != SQLITE_DONE)
    {
        sqlite3_finalize(stmt);
        Fail("query failed: " + sql + ": rc=" + std::to_string(step_rc));
    }
    const bool has_rows = step_rc == SQLITE_ROW;
    const int finalize_rc = sqlite3_finalize(stmt);
    CheckSqlite(db, finalize_rc, "finalize query: " + sql);
    return has_rows;
}

class SqliteDb
{
public:
    explicit SqliteDb(const fs::path &path)
    {
        const int rc = sqlite3_open_v2(path.c_str(), &db_,
            SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX, nullptr);
        if(rc != SQLITE_OK)
        {
            const std::string detail = db_ ? sqlite3_errmsg(db_) : "unknown error";
            if(db_)
            {
                sqlite3_close(db_);
                db_ = nullptr;
            }
            Fail("open sqlite database failed: " + detail);
        }
    }

    ~SqliteDb()
    {
        if(db_)
        {
            sqlite3_close(db_);
        }
    }

    SqliteDb(const SqliteDb &) = delete;
    SqliteDb &operator=(const SqliteDb &) = delete;

    sqlite3 *get() const
    {
        return db_;
    }

private:
    sqlite3 *db_ = nullptr;
};

class SqliteBlob
{
public:
    SqliteBlob() = default;

    ~SqliteBlob()
    {
        if(blob_)
        {
            sqlite3_blob_close(blob_);
        }
    }

    SqliteBlob(const SqliteBlob &) = delete;
    SqliteBlob &operator=(const SqliteBlob &) = delete;

    sqlite3_blob **out()
    {
        return &blob_;
    }

    sqlite3_blob *get() const
    {
        return blob_;
    }

    int Close()
    {
        if(!blob_)
        {
            return SQLITE_OK;
        }
        sqlite3_blob *blob = blob_;
        blob_ = nullptr;
        return sqlite3_blob_close(blob);
    }

private:
    sqlite3_blob *blob_ = nullptr;
};

class Sha256
{
public:
    Sha256()
        : context_(EVP_MD_CTX_new())
    {
        if(!context_)
        {
            Fail("EVP_MD_CTX_new failed");
        }
        if(EVP_DigestInit_ex(context_, EVP_sha256(), nullptr) != 1)
        {
            Fail("EVP_DigestInit_ex failed");
        }
    }

    ~Sha256()
    {
        EVP_MD_CTX_free(context_);
    }

    Sha256(const Sha256 &) = delete;
    Sha256 &operator=(const Sha256 &) = delete;

    void Update(const void *data, std::size_t size)
    {
        if(EVP_DigestUpdate(context_, data, size) != 1)
        {
            Fail("EVP_DigestUpdate failed");
        }
    }

    std::string Final()
    {
        std::array<unsigned char, EVP_MAX_MD_SIZE> digest{};
        unsigned int digest_size = 0;
        if(EVP_DigestFinal_ex(context_, digest.data(), &digest_size) != 1)
        {
            Fail("EVP_DigestFinal_ex failed");
        }

        std::ostringstream out;
        out << std::hex << std::setfill('0');
        for(unsigned int i = 0; i < digest_size; ++i)
        {
            out << std::setw(2) << static_cast<unsigned int>(digest[i]);
        }
        return out.str();
    }

private:
    EVP_MD_CTX *context_ = nullptr;
};

std::uint64_t ParseUnsigned(const std::string &text, const std::string &name)
{
    if(text.empty() || text[0] == '-')
    {
        Fail("invalid " + name + ": " + text);
    }
    std::size_t consumed = 0;
    const unsigned long long value = std::stoull(text, &consumed, 10);
    if(consumed != text.size())
    {
        Fail("invalid " + name + ": " + text);
    }
    return static_cast<std::uint64_t>(value);
}

std::uint64_t ParseSize(const std::string &text, const std::string &name)
{
    const std::string lower = Lower(text);
    std::size_t suffix_start = 0;
    while(suffix_start < lower.size() && std::isdigit(static_cast<unsigned char>(lower[suffix_start])))
    {
        ++suffix_start;
    }
    const std::uint64_t number = ParseUnsigned(lower.substr(0, suffix_start), name);
    const std::string suffix = lower.substr(suffix_start);
    std::uint64_t multiplier = 1;
    if(suffix == "kib" || suffix == "kb")
    {
        multiplier = kKiB;
    }
    else if(suffix == "mib" || suffix == "mb")
    {
        multiplier = kMiB;
    }
    else if(suffix == "gib" || suffix == "gb")
    {
        multiplier = 1024 * kMiB;
    }
    else if(!suffix.empty() && suffix != "b")
    {
        Fail("invalid " + name + ": " + text);
    }
    if(number > std::numeric_limits<std::uint64_t>::max() / multiplier)
    {
        Fail("size overflows " + name + ": " + text);
    }
    return number * multiplier;
}

struct Options
{
    std::string backend = "sqlite_blob";
    bool hash = true;
    std::string workload = "smoke";
    std::int64_t duration_seconds = 0;
    std::int64_t write_rate = 0;
    std::string size_profile = "normal";
    std::uint64_t blob_chunk_size = 256 * kKiB;
    std::string synchronous = "normal";
    std::int64_t export_concurrency = 0;
    std::uint64_t export_rate_limit = 0;
    fs::path output_dir;
    std::uint64_t object_size = 1 * kMiB;
    std::int64_t object_count = 0;
    std::uint64_t seed = kDefaultSeed;
    std::uint64_t producer_count = 4;
    std::uint64_t queue_capacity = 64;
    std::uint64_t read_operations = 1000;
    std::uint64_t retention_count = 4;
    std::string fault_point;
    bool show_help = false;
};

void PrintHelp()
{
    std::cout
        << "interaction_history_storage_bench\n"
        << "  --backend sqlite_blob|local_file\n"
        << "  --hash on|off\n"
        << "  --workload smoke|single-size|profile|mixed|read|export|cleanup|crash\n"
        << "  --duration <seconds>\n"
        << "  --write-rate <records_per_second>\n"
        << "  --size-profile normal|large\n"
        << "  --blob-chunk-size <bytes>\n"
        << "  --synchronous normal|full\n"
        << "  --export-concurrency <count>\n"
        << "  --export-rate-limit <bytes_per_second>\n"
        << "  --output-dir <path>\n"
        << "  --object-size <bytes>\n"
        << "  --objects <count>\n"
        << "  --producers <count>\n"
        << "  --queue-capacity <count>\n"
        << "  --read-operations <count>\n"
        << "  --retention-count <count>\n"
        << "  --fault-point blob-write|commit|cleanup|export\n"
        << "  --seed <integer>\n";
}

Options ParseArgs(int argc, char **argv)
{
    Options options;
    for(int i = 1; i < argc; ++i)
    {
        const std::string arg = argv[i];
        if(arg == "--help" || arg == "-h")
        {
            options.show_help = true;
            continue;
        }
        if(i + 1 >= argc)
        {
            Fail("missing value for " + arg);
        }
        const std::string value = argv[++i];
        if(arg == "--backend") options.backend = value;
        else if(arg == "--hash") options.hash = Lower(value) == "on";
        else if(arg == "--workload") options.workload = value;
        else if(arg == "--duration") options.duration_seconds = static_cast<std::int64_t>(ParseUnsigned(value, arg));
        else if(arg == "--write-rate") options.write_rate = static_cast<std::int64_t>(ParseUnsigned(value, arg));
        else if(arg == "--size-profile") options.size_profile = value;
        else if(arg == "--blob-chunk-size") options.blob_chunk_size = ParseSize(value, arg);
        else if(arg == "--synchronous") options.synchronous = Lower(value);
        else if(arg == "--export-concurrency") options.export_concurrency = static_cast<std::int64_t>(ParseUnsigned(value, arg));
        else if(arg == "--export-rate-limit") options.export_rate_limit = ParseSize(value, arg);
        else if(arg == "--output-dir") options.output_dir = value;
        else if(arg == "--object-size") options.object_size = ParseSize(value, arg);
        else if(arg == "--objects") options.object_count = static_cast<std::int64_t>(ParseUnsigned(value, arg));
        else if(arg == "--producers") options.producer_count = ParseUnsigned(value, arg);
        else if(arg == "--queue-capacity") options.queue_capacity = ParseUnsigned(value, arg);
        else if(arg == "--read-operations") options.read_operations = ParseUnsigned(value, arg);
        else if(arg == "--retention-count") options.retention_count = ParseUnsigned(value, arg);
        else if(arg == "--fault-point") options.fault_point = Lower(value);
        else if(arg == "--seed") options.seed = ParseUnsigned(value, arg);
        else Fail("unknown option: " + arg);
    }

    if(options.show_help)
    {
        return options;
    }
    options.backend = Lower(options.backend);
    options.workload = Lower(options.workload);
    options.size_profile = Lower(options.size_profile);
    if(options.backend != "sqlite_blob" && options.backend != "local_file")
    {
        Fail("--backend must be sqlite_blob or local_file");
    }
    if(options.workload != "smoke" && options.workload != "single-size"
        && options.workload != "profile" && options.workload != "mixed"
        && options.workload != "read" && options.workload != "export"
        && options.workload != "cleanup" && options.workload != "crash")
    {
        Fail("--workload must be smoke, single-size, profile, mixed, read, export, cleanup, or crash");
    }
    if(options.size_profile != "normal" && options.size_profile != "large")
    {
        Fail("--size-profile must be normal or large");
    }
    if(options.synchronous != "normal" && options.synchronous != "full")
    {
        Fail("--synchronous must be normal or full");
    }
    if(options.blob_chunk_size == 0 || options.blob_chunk_size > kMaxBlobChunk)
    {
        Fail("--blob-chunk-size must be in the range 1..2147483647");
    }
    if(options.object_size == 0 || options.object_size > kMaxBlobChunk)
    {
        Fail("--object-size must be in the range 1..2147483647 for this initial benchmark");
    }
    if((options.export_concurrency != 0 || options.export_rate_limit != 0)
        && options.workload != "export")
    {
        Fail("export options require --workload export");
    }
    if(options.producer_count == 0 || options.queue_capacity == 0)
    {
        Fail("--producers and --queue-capacity must be greater than zero");
    }
    if(options.workload == "mixed"
        && (options.object_count == 0 && (options.duration_seconds == 0 || options.write_rate == 0)))
    {
        Fail("mixed workload requires --objects or both --duration and --write-rate");
    }
    if(options.workload == "read" && options.read_operations == 0)
    {
        Fail("--read-operations must be greater than zero");
    }
    if(options.workload == "crash"
        && options.fault_point != "blob-write"
        && options.fault_point != "commit"
        && options.fault_point != "cleanup"
        && options.fault_point != "export")
    {
        Fail("crash workload requires --fault-point blob-write, commit, cleanup, or export");
    }
    return options;
}

std::uint64_t SplitMix64(std::uint64_t value)
{
    value += 0x9e3779b97f4a7c15ULL;
    value = (value ^ (value >> 30)) * 0xbf58476d1ce4e5b9ULL;
    value = (value ^ (value >> 27)) * 0x94d049bb133111ebULL;
    return value ^ (value >> 31);
}

void FillPattern(std::vector<unsigned char> *buffer,
                 std::uint64_t object_index,
                 std::uint64_t offset,
                 std::uint64_t seed)
{
    for(std::size_t i = 0; i < buffer->size(); ++i)
    {
        const std::uint64_t position = offset + static_cast<std::uint64_t>(i);
        const std::uint64_t mixed = seed
            ^ (object_index * 0xd6e8feb86659fd93ULL)
            ^ (position * 0xa0761d6478bd642fULL);
        (*buffer)[i] = static_cast<unsigned char>(SplitMix64(mixed) >> 32);
    }
}

std::string HashObject(std::uint64_t object_index,
                       std::uint64_t object_size,
                       const Options &options)
{
    std::unique_ptr<Sha256> digest;
    if(options.hash)
    {
        digest = std::make_unique<Sha256>();
    }
    std::vector<unsigned char> chunk(static_cast<std::size_t>(
        std::min(options.blob_chunk_size, object_size)));
    for(std::uint64_t offset = 0; offset < object_size;)
    {
        const std::size_t size = static_cast<std::size_t>(
            std::min<std::uint64_t>(chunk.size(), object_size - offset));
        chunk.resize(size);
        FillPattern(&chunk, object_index, offset, options.seed);
        if(digest)
        {
            digest->Update(chunk.data(), chunk.size());
        }
        offset += size;
    }
    return digest ? digest->Final() : std::string();
}

void ConfigureDatabase(sqlite3 *db, const Options &options)
{
    Exec(db, "PRAGMA foreign_keys=ON;");
    Exec(db, "PRAGMA busy_timeout=3000;");
    const std::string journal_mode = QueryText(db, "PRAGMA journal_mode=WAL;");
    if(Lower(journal_mode) != "wal")
    {
        Fail("SQLite did not enter WAL mode: " + journal_mode);
    }
    Exec(db, "PRAGMA synchronous=" + options.synchronous + ";");
    Exec(db, "PRAGMA wal_autocheckpoint=1000;");
    if(QueryInt64(db, "SELECT json_valid('[]');") != 1)
    {
        Fail("SQLite JSON1 extension is required by ranges_json");
    }
    Exec(db,
        "CREATE TABLE IF NOT EXISTS interaction_records("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "project_id INTEGER NOT NULL,"
        "protocol_id INTEGER NOT NULL DEFAULT 0,"
        "scope INTEGER NOT NULL CHECK(scope IN (1,2)),"
        "protocol_type INTEGER NOT NULL,"
        "result INTEGER NOT NULL,"
        "request_received_ms INTEGER NOT NULL,"
        "response_queued_ms INTEGER,"
        "duration_ms INTEGER,"
        "peer_ip TEXT NOT NULL DEFAULT '',"
        "peer_port INTEGER,"
        "request_object_id INTEGER REFERENCES interaction_objects(id),"
        "response_object_id INTEGER REFERENCES interaction_objects(id),"
        "error_message TEXT NOT NULL DEFAULT '',"
        "protocol_meta_json TEXT CHECK(protocol_meta_json IS NULL OR json_valid(protocol_meta_json)),"
        "persisted_at_ms INTEGER NOT NULL,"
        "CHECK((scope = 1 AND protocol_id > 0) OR (scope = 2 AND protocol_id = 0)),"
        "CHECK(peer_port IS NULL OR (peer_port >= 0 AND peer_port <= 65535))"
        ");"
        "CREATE TABLE IF NOT EXISTS interaction_objects("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "sha256 TEXT NOT NULL,"
        "size_bytes INTEGER NOT NULL CHECK(size_bytes >= 0),"
        "storage_location_id TEXT NOT NULL,"
        "object_key TEXT NOT NULL,"
        "state INTEGER NOT NULL CHECK(state IN (1,2,3,4)),"
        "created_at_ms INTEGER NOT NULL,"
        "last_referenced_at_ms INTEGER NOT NULL,"
        // data is retained only for the SQLite-BLOB benchmark candidate. The
        // production contract stores raw bytes through ObjectStorage.
        "data BLOB,"
        "UNIQUE(storage_location_id,object_key)"
        ");"
        "CREATE TABLE IF NOT EXISTS interaction_payload_refs("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "record_id INTEGER NOT NULL REFERENCES interaction_records(id) ON DELETE CASCADE,"
        "source_object_id INTEGER NOT NULL REFERENCES interaction_objects(id),"
        "side INTEGER NOT NULL CHECK(side IN (1,2,3)),"
        "ref_kind INTEGER NOT NULL CHECK(ref_kind IN (1,2)),"
        "part_index INTEGER,"
        "ranges_json TEXT NOT NULL CHECK(json_valid(ranges_json)),"
        "size_bytes INTEGER NOT NULL CHECK(size_bytes >= 0),"
        "content_kind INTEGER NOT NULL CHECK(content_kind BETWEEN 0 AND 8),"
        "source_meta_json TEXT CHECK(source_meta_json IS NULL OR json_valid(source_meta_json)),"
        "parse_state INTEGER NOT NULL CHECK(parse_state IN (1,2,3,4)),"
        "parse_error TEXT NOT NULL DEFAULT '',"
        "CHECK(part_index IS NULL OR part_index >= 0)"
        ");"
        "CREATE TABLE IF NOT EXISTS interaction_retention_policies("
        "project_id INTEGER PRIMARY KEY,"
        "inherit_global INTEGER NOT NULL DEFAULT 1 CHECK(inherit_global IN (0,1)),"
        "retention_days INTEGER,"
        "quota_bytes INTEGER,"
        "updated_by INTEGER NOT NULL,"
        "updated_at_ms INTEGER NOT NULL,"
        "CHECK((inherit_global = 1 AND retention_days IS NULL AND quota_bytes IS NULL)"
        " OR (inherit_global = 0 AND retention_days > 0 AND quota_bytes > 0))"
        ");"
        "CREATE TABLE IF NOT EXISTS interaction_export_jobs("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "project_id INTEGER NOT NULL,"
        "created_by INTEGER NOT NULL,"
        "state INTEGER NOT NULL CHECK(state IN (1,2,3,4,5)),"
        "filter_json TEXT NOT NULL CHECK(json_valid(filter_json)),"
        "storage_location_id TEXT,"
        "object_key TEXT,"
        "size_bytes INTEGER,"
        "sha256 TEXT,"
        "error_message TEXT NOT NULL DEFAULT '',"
        "created_at_ms INTEGER NOT NULL,"
        "completed_at_ms INTEGER,"
        "expires_at_ms INTEGER"
        ");"
        "CREATE TABLE IF NOT EXISTS interaction_export_job_records("
        "job_id INTEGER NOT NULL REFERENCES interaction_export_jobs(id) ON DELETE CASCADE,"
        "record_id INTEGER NOT NULL REFERENCES interaction_records(id) ON DELETE RESTRICT,"
        "PRIMARY KEY(job_id,record_id)"
        ");"
        "CREATE TABLE IF NOT EXISTS interaction_cleanup_audits("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "project_id INTEGER,"
        "reason INTEGER NOT NULL,"
        "actor_user_id INTEGER,"
        "record_count INTEGER NOT NULL,"
        "object_count INTEGER NOT NULL,"
        "freed_bytes INTEGER NOT NULL,"
        "details_json TEXT CHECK(details_json IS NULL OR json_valid(details_json)),"
        "created_at_ms INTEGER NOT NULL"
        ");"
        "CREATE UNIQUE INDEX IF NOT EXISTS interaction_objects_location_key_idx "
        "ON interaction_objects(storage_location_id,object_key);"
        "CREATE INDEX IF NOT EXISTS interaction_records_project_time_idx "
        "ON interaction_records(project_id,request_received_ms DESC,id DESC);"
        "CREATE INDEX IF NOT EXISTS interaction_records_project_scope_time_idx "
        "ON interaction_records(project_id,scope,request_received_ms DESC,id DESC);"
        "CREATE INDEX IF NOT EXISTS interaction_records_project_protocol_time_idx "
        "ON interaction_records(project_id,protocol_id,request_received_ms DESC,id DESC);"
        "CREATE INDEX IF NOT EXISTS interaction_records_project_result_time_idx "
        "ON interaction_records(project_id,result,request_received_ms DESC,id DESC);"
        "CREATE INDEX IF NOT EXISTS interaction_records_project_peer_time_idx "
        "ON interaction_records(project_id,peer_ip,request_received_ms DESC,id DESC);"
        "CREATE INDEX IF NOT EXISTS interaction_payload_refs_record_idx "
        "ON interaction_payload_refs(record_id,side,part_index);"
        "CREATE INDEX IF NOT EXISTS interaction_payload_refs_object_idx "
        "ON interaction_payload_refs(source_object_id);"
        "CREATE INDEX IF NOT EXISTS interaction_payload_refs_preview_idx "
        "ON interaction_payload_refs(content_kind,size_bytes,record_id);"
        "CREATE INDEX IF NOT EXISTS interaction_objects_state_reference_idx "
        "ON interaction_objects(state,last_referenced_at_ms);"
        "CREATE INDEX IF NOT EXISTS interaction_export_jobs_owner_created_idx "
        "ON interaction_export_jobs(created_by,created_at_ms DESC);"
        "CREATE INDEX IF NOT EXISTS interaction_export_jobs_expiry_idx "
        "ON interaction_export_jobs(state,expires_at_ms);"
        "CREATE INDEX IF NOT EXISTS interaction_export_job_records_record_idx "
        "ON interaction_export_job_records(record_id,job_id);"
        "CREATE INDEX IF NOT EXISTS interaction_cleanup_audits_project_time_idx "
        "ON interaction_cleanup_audits(project_id,created_at_ms DESC);");
}

std::string BenchmarkObjectKey(const std::string &storage_location_id,
                               std::uint64_t object_index)
{
    return storage_location_id == "sqlite_blob"
        ? "sqlite/object_" + std::to_string(object_index) + ".bin"
        : "raw/object_" + std::to_string(object_index) + ".bin";
}

std::int64_t InsertRecord(sqlite3 *db, std::int64_t request_object_id)
{
    sqlite3_stmt *stmt = nullptr;
    CheckSqlite(db, sqlite3_prepare_v2(db,
        "INSERT INTO interaction_records("
        "project_id,protocol_id,scope,protocol_type,result,request_received_ms,"
        "response_queued_ms,duration_ms,peer_ip,peer_port,request_object_id,"
        "response_object_id,error_message,protocol_meta_json,persisted_at_ms) "
        "VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?);",
        -1, &stmt, nullptr), "prepare record insert");
    const std::int64_t now_ms = WallClockMillis();
    sqlite3_bind_int64(stmt, 1, 1);
    sqlite3_bind_int64(stmt, 2, 1);
    sqlite3_bind_int(stmt, 3, kScopeProtocol);
    sqlite3_bind_int(stmt, 4, kProtocolTypeHttp);
    sqlite3_bind_int(stmt, 5, kResultMatched);
    sqlite3_bind_int64(stmt, 6, now_ms);
    sqlite3_bind_int64(stmt, 7, now_ms);
    sqlite3_bind_int64(stmt, 8, 0);
    sqlite3_bind_text(stmt, 9, "127.0.0.1", -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 10, 0);
    sqlite3_bind_int64(stmt, 11, request_object_id);
    sqlite3_bind_null(stmt, 12);
    sqlite3_bind_text(stmt, 13, "", -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 14,
        R"({"request":{"method":"GET","target":"/benchmark"}})",
        -1, SQLITE_STATIC);
    sqlite3_bind_int64(stmt, 15, now_ms);
    const int step_rc = sqlite3_step(stmt);
    const int finalize_rc = sqlite3_finalize(stmt);
    CheckSqlite(db, step_rc, "insert record");
    CheckSqlite(db, finalize_rc, "finalize record insert");
    return sqlite3_last_insert_rowid(db);
}

std::int64_t InsertBlobObject(sqlite3 *db,
                              std::uint64_t object_index,
                              std::uint64_t object_size,
                              const std::string &sha256)
{
    sqlite3_stmt *stmt = nullptr;
    CheckSqlite(db, sqlite3_prepare_v2(db,
        "INSERT INTO interaction_objects(sha256,size_bytes,storage_location_id,"
        "object_key,state,created_at_ms,last_referenced_at_ms,data) "
        "VALUES(?,?,?,?,?,?,?,zeroblob(?));", -1, &stmt, nullptr),
        "prepare blob insert");
    const std::int64_t now_ms = WallClockMillis();
    sqlite3_bind_text(stmt, 1, sha256.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 2, static_cast<sqlite3_int64>(object_size));
    sqlite3_bind_text(stmt, 3, "sqlite_blob", -1, SQLITE_STATIC);
    const std::string object_key = BenchmarkObjectKey("sqlite_blob", object_index);
    sqlite3_bind_text(stmt, 4, object_key.c_str(), -1, SQLITE_TRANSIENT);
    // The final schema has no writing state. The benchmark uses failed as a
    // private transient placeholder until the incremental BLOB is complete.
    sqlite3_bind_int(stmt, 5, kObjectFailed);
    sqlite3_bind_int64(stmt, 6, now_ms);
    sqlite3_bind_int64(stmt, 7, now_ms);
    sqlite3_bind_int64(stmt, 8, static_cast<sqlite3_int64>(object_size));
    const int step_rc = sqlite3_step(stmt);
    const int finalize_rc = sqlite3_finalize(stmt);
    CheckSqlite(db, step_rc, "insert blob object");
    CheckSqlite(db, finalize_rc, "finalize blob insert");
    return sqlite3_last_insert_rowid(db);
}

std::int64_t InsertFileObject(sqlite3 *db,
                              std::uint64_t object_index,
                              std::uint64_t object_size,
                              const std::string &sha256,
                              const std::string &object_key)
{
    sqlite3_stmt *stmt = nullptr;
    CheckSqlite(db, sqlite3_prepare_v2(db,
        "INSERT INTO interaction_objects(sha256,size_bytes,storage_location_id,"
        "object_key,state,created_at_ms,last_referenced_at_ms) "
        "VALUES(?,?,?,?,?,?,?);", -1, &stmt, nullptr), "prepare file insert");
    const std::int64_t now_ms = WallClockMillis();
    sqlite3_bind_text(stmt, 1, sha256.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 2, static_cast<sqlite3_int64>(object_size));
    sqlite3_bind_text(stmt, 3, "local_file", -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 4, object_key.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 5, kObjectReady);
    sqlite3_bind_int64(stmt, 6, now_ms);
    sqlite3_bind_int64(stmt, 7, now_ms);
    const int step_rc = sqlite3_step(stmt);
    const int finalize_rc = sqlite3_finalize(stmt);
    CheckSqlite(db, step_rc, "insert file object");
    CheckSqlite(db, finalize_rc, "finalize file insert");
    return sqlite3_last_insert_rowid(db);
}

void MarkBlobReady(sqlite3 *db, std::int64_t object_id)
{
    sqlite3_stmt *stmt = nullptr;
    CheckSqlite(db, sqlite3_prepare_v2(db,
        "UPDATE interaction_objects SET state=1 WHERE id=?;", -1, &stmt, nullptr),
        "prepare blob state update");
    sqlite3_bind_int64(stmt, 1, object_id);
    const int step_rc = sqlite3_step(stmt);
    const int finalize_rc = sqlite3_finalize(stmt);
    CheckSqlite(db, step_rc, "mark blob ready");
    CheckSqlite(db, finalize_rc, "finalize blob state update");
}

void WriteAll(int fd, const unsigned char *data, std::size_t size)
{
    std::size_t written = 0;
    while(written < size)
    {
        const ssize_t rc = ::write(fd, data + written, size - written);
        if(rc < 0 && errno == EINTR)
        {
            continue;
        }
        if(rc <= 0)
        {
            Fail("write raw file failed: " + std::string(std::strerror(errno)));
        }
        written += static_cast<std::size_t>(rc);
    }
}

fs::path WriteFileObject(const fs::path &raw_dir,
                         std::uint64_t object_index,
                         std::uint64_t object_size,
                         const Options &options)
{
    const fs::path temporary = raw_dir / ("object_" + std::to_string(object_index) + ".bin.tmp");
    const fs::path final_path = raw_dir / ("object_" + std::to_string(object_index) + ".bin");
    int fd = ::open(temporary.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0600);
    if(fd < 0)
    {
        Fail("open raw temporary file failed: " + std::string(std::strerror(errno)));
    }

    try
    {
        std::vector<unsigned char> chunk(static_cast<std::size_t>(
            std::min(options.blob_chunk_size, object_size)));
        for(std::uint64_t offset = 0; offset < object_size;)
        {
            const std::size_t size = static_cast<std::size_t>(
                std::min<std::uint64_t>(chunk.size(), object_size - offset));
            chunk.resize(size);
            FillPattern(&chunk, object_index, offset, options.seed);
            WriteAll(fd, chunk.data(), chunk.size());
            offset += size;
        }
        if(::fsync(fd) != 0)
        {
            Fail("fsync raw file failed: " + std::string(std::strerror(errno)));
        }
        if(::close(fd) != 0)
        {
            Fail("close raw temporary file failed: " + std::string(std::strerror(errno)));
        }
        fd = -1;
        if(::rename(temporary.c_str(), final_path.c_str()) != 0)
        {
            Fail("rename raw file failed: " + std::string(std::strerror(errno)));
        }
    }
    catch(...)
    {
        if(fd >= 0)
        {
            ::close(fd);
        }
        std::error_code error;
        fs::remove(temporary, error);
        throw;
    }
    return final_path;
}

struct Sample
{
    std::uint64_t object_index = 0;
    std::uint64_t object_size = 0;
    std::int64_t hash_us = 0;
    std::int64_t writer_wait_us = 0;
    std::int64_t transaction_us = 0;
    std::int64_t commit_us = 0;
    std::int64_t end_to_end_us = 0;
};

std::uint64_t ObjectIndexFromKey(const std::string &object_key);

std::vector<std::uint64_t> ProfileSizes(const Options &options)
{
    if(options.workload == "smoke")
    {
        return {64 * kKiB};
    }
    if(options.workload == "single-size")
    {
        return {options.object_size};
    }
    if(options.workload == "mixed")
    {
        return {1 * kKiB, 16 * kKiB, 64 * kKiB, 256 * kKiB,
                1 * kMiB, 1 * kMiB, 1 * kMiB, 8 * kMiB};
    }
    if(options.size_profile == "large")
    {
        return {8 * kMiB, 16 * kMiB, 64 * kMiB};
    }
    return {1 * kKiB, 16 * kKiB, 64 * kKiB, 256 * kKiB,
            1 * kMiB, 8 * kMiB, 16 * kMiB, 64 * kMiB};
}

std::uint64_t ObjectCount(const Options &options)
{
    if(options.object_count > 0)
    {
        return static_cast<std::uint64_t>(options.object_count);
    }
    if(options.duration_seconds > 0 && options.write_rate > 0)
    {
        return static_cast<std::uint64_t>(options.duration_seconds)
            * static_cast<std::uint64_t>(options.write_rate);
    }
    return options.workload == "smoke" ? 2 : 1;
}

struct WorkItem
{
    std::uint64_t object_index = 0;
    std::uint64_t object_size = 0;
    std::string sha256;
    std::int64_t hash_us = 0;
    TimePoint enqueued_at;
};

struct QueueStats
{
    std::uint64_t capacity = 0;
    std::uint64_t peak = 0;
    std::uint64_t enqueued = 0;
    std::uint64_t producers = 0;
};

class BoundedQueue
{
public:
    explicit BoundedQueue(std::uint64_t capacity)
        : capacity_(capacity)
    {
    }

    bool Push(WorkItem item)
    {
        std::unique_lock<std::mutex> lock(mutex_);
        not_full_.wait(lock, [this] {
            return closed_ || queue_.size() < capacity_;
        });
        if(closed_)
        {
            return false;
        }
        queue_.push_back(std::move(item));
        peak_ = std::max<std::uint64_t>(peak_, queue_.size());
        ++enqueued_;
        not_empty_.notify_one();
        return true;
    }

    bool Pop(WorkItem *item)
    {
        std::unique_lock<std::mutex> lock(mutex_);
        not_empty_.wait(lock, [this] {
            return closed_ || !queue_.empty();
        });
        if(queue_.empty())
        {
            return false;
        }
        *item = std::move(queue_.front());
        queue_.pop_front();
        not_full_.notify_one();
        return true;
    }

    void Close()
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            closed_ = true;
        }
        not_empty_.notify_all();
        not_full_.notify_all();
    }

    QueueStats Stats(std::uint64_t producers) const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return {capacity_, peak_, enqueued_, producers};
    }

private:
    const std::uint64_t capacity_;
    mutable std::mutex mutex_;
    std::condition_variable not_empty_;
    std::condition_variable not_full_;
    std::deque<WorkItem> queue_;
    bool closed_ = false;
    std::uint64_t peak_ = 0;
    std::uint64_t enqueued_ = 0;
};

struct PayloadSpec
{
    std::vector<std::pair<std::uint64_t, std::uint64_t>> ranges;
};

std::vector<PayloadSpec> MakePayloadSpecs(std::uint64_t object_size)
{
    PayloadSpec range;
    const std::uint64_t range_offset = object_size > 128 ? 128 : 0;
    const std::uint64_t range_length = std::min<std::uint64_t>(
        256 * kKiB, object_size - range_offset);
    range.ranges.emplace_back(range_offset, range_length);

    const std::uint64_t segment_length = std::min<std::uint64_t>(
        64 * kKiB, std::max<std::uint64_t>(1, object_size / 4));
    std::vector<std::uint64_t> offsets = {0, object_size / 2,
        object_size >= segment_length ? object_size - segment_length : 0};
    std::sort(offsets.begin(), offsets.end());
    offsets.erase(std::unique(offsets.begin(), offsets.end()), offsets.end());
    std::vector<std::pair<std::uint64_t, std::uint64_t>> segment_ranges;
    for(const std::uint64_t offset : offsets)
    {
        segment_ranges.emplace_back(offset, segment_length);
    }

    PayloadSpec segmented;
    segmented.ranges = std::move(segment_ranges);
    return {range, segmented};
}

void InsertPayload(sqlite3 *db,
                   std::int64_t record_id,
                   std::int64_t object_id,
                   const PayloadSpec &spec)
{
    Json ranges = Json::array();
    std::uint64_t size_bytes = 0;
    for(const auto &[offset, length] : spec.ranges)
    {
        if(length == 0 || length > std::numeric_limits<std::uint64_t>::max() - size_bytes)
        {
            Fail("payload range size overflow");
        }
        ranges.push_back({{"offset", offset}, {"length", length}});
        size_bytes += length;
    }
    if(spec.ranges.empty())
    {
        Fail("payload ranges must not be empty");
    }
    sqlite3_stmt *stmt = nullptr;
    CheckSqlite(db, sqlite3_prepare_v2(db,
        "INSERT INTO interaction_payload_refs(record_id,source_object_id,side,ref_kind,"
        "part_index,ranges_json,size_bytes,content_kind,source_meta_json,parse_state,parse_error) "
        "VALUES(?,?,?,?,?,?,?,?,?,?,?);",
        -1, &stmt, nullptr), "prepare payload insert");
    sqlite3_bind_int64(stmt, 1, record_id);
    sqlite3_bind_int64(stmt, 2, object_id);
    sqlite3_bind_int(stmt, 3, kHistorySideRequest);
    sqlite3_bind_int(stmt, 4, kPayloadRefBody);
    sqlite3_bind_null(stmt, 5);
    const std::string ranges_json = ranges.dump();
    sqlite3_bind_text(stmt, 6, ranges_json.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 7, static_cast<sqlite3_int64>(size_bytes));
    sqlite3_bind_int(stmt, 8, kPayloadContentBinary);
    sqlite3_bind_null(stmt, 9);
    sqlite3_bind_int(stmt, 10, kPayloadParsed);
    sqlite3_bind_text(stmt, 11, "", -1, SQLITE_STATIC);
    const int step_rc = sqlite3_step(stmt);
    const int finalize_rc = sqlite3_finalize(stmt);
    CheckSqlite(db, step_rc, "insert payload");
    CheckSqlite(db, finalize_rc, "finalize payload insert");
}

void InsertPayloads(sqlite3 *db,
                    std::int64_t record_id,
                    std::int64_t object_id,
                    std::uint64_t object_size)
{
    for(const PayloadSpec &spec : MakePayloadSpecs(object_size))
    {
        InsertPayload(db, record_id, object_id, spec);
    }
}

void BeginImmediate(sqlite3 *db, std::int64_t *writer_wait_us)
{
    const TimePoint wait_start = Clock::now();
    Exec(db, "BEGIN IMMEDIATE;");
    *writer_wait_us = MicrosSince(wait_start);
}

void RollbackNoThrow(sqlite3 *db) noexcept
{
    char *error = nullptr;
    sqlite3_exec(db, "ROLLBACK;", nullptr, nullptr, &error);
    sqlite3_free(error);
}

Sample WriteSqliteObject(sqlite3 *db,
                         std::uint64_t object_index,
                         std::uint64_t object_size,
                         const std::string &sha256,
                         const Options &options)
{
    Sample sample;
    sample.object_index = object_index;
    sample.object_size = object_size;
    const TimePoint e2e_start = Clock::now();
    const TimePoint hash_start = Clock::now();
    if(options.hash)
    {
        (void)sha256;
    }
    sample.hash_us = MicrosSince(hash_start);

    bool transaction_active = false;
    try
    {
        BeginImmediate(db, &sample.writer_wait_us);
        transaction_active = true;
        const TimePoint transaction_start = Clock::now();
        const std::int64_t object_id = InsertBlobObject(
            db, object_index, object_size, sha256);

        SqliteBlob blob;
        CheckSqlite(db, sqlite3_blob_open(db, "main", "interaction_objects", "data",
            object_id, 1, blob.out()), "open incremental blob");
        std::vector<unsigned char> chunk(static_cast<std::size_t>(
            std::min(options.blob_chunk_size, object_size)));
        for(std::uint64_t offset = 0; offset < object_size;)
        {
            const std::size_t size = static_cast<std::size_t>(
                std::min<std::uint64_t>(chunk.size(), object_size - offset));
            chunk.resize(size);
            FillPattern(&chunk, object_index, offset, options.seed);
            CheckSqlite(db, sqlite3_blob_write(blob.get(), chunk.data(),
                static_cast<int>(chunk.size()), static_cast<int>(offset)),
                "write incremental blob");
            offset += size;
        }
        CheckSqlite(db, blob.Close(), "close incremental blob");
        MarkBlobReady(db, object_id);
        const std::int64_t record_id = InsertRecord(db, object_id);
        InsertPayloads(db, record_id, object_id, object_size);

        const TimePoint commit_start = Clock::now();
        Exec(db, "COMMIT;");
        transaction_active = false;
        sample.commit_us = MicrosSince(commit_start);
        sample.transaction_us = MicrosSince(transaction_start);
    }
    catch(...)
    {
        if(transaction_active)
        {
            RollbackNoThrow(db);
        }
        throw;
    }
    sample.end_to_end_us = MicrosSince(e2e_start);
    return sample;
}

Sample WriteFileBackend(sqlite3 *db,
                        const fs::path &raw_dir,
                        std::uint64_t object_index,
                        std::uint64_t object_size,
                        const std::string &sha256,
                        const Options &options)
{
    Sample sample;
    sample.object_index = object_index;
    sample.object_size = object_size;
    const TimePoint e2e_start = Clock::now();
    const TimePoint hash_start = Clock::now();
    if(options.hash)
    {
        (void)sha256;
    }
    sample.hash_us = MicrosSince(hash_start);
    const fs::path final_path = WriteFileObject(raw_dir, object_index, object_size, options);
    const std::string object_key = "raw/" + final_path.filename().string();

    bool transaction_active = false;
    try
    {
        BeginImmediate(db, &sample.writer_wait_us);
        transaction_active = true;
        const TimePoint transaction_start = Clock::now();
        const std::int64_t object_id = InsertFileObject(
            db, object_index, object_size, sha256, object_key);
        const std::int64_t record_id = InsertRecord(db, object_id);
        InsertPayloads(db, record_id, object_id, object_size);
        const TimePoint commit_start = Clock::now();
        Exec(db, "COMMIT;");
        transaction_active = false;
        sample.commit_us = MicrosSince(commit_start);
        sample.transaction_us = MicrosSince(transaction_start);
    }
    catch(...)
    {
        if(transaction_active)
        {
            RollbackNoThrow(db);
        }
        std::error_code error;
        fs::remove(final_path, error);
        throw;
    }
    sample.end_to_end_us = MicrosSince(e2e_start);
    return sample;
}

void VerifyRaw(sqlite3 *db,
               std::int64_t object_id,
               std::uint64_t object_index,
               std::uint64_t object_size,
               const std::string &expected_hash,
               const fs::path &output_dir,
               const Options &options,
               const std::string &object_key)
{
    std::unique_ptr<Sha256> digest;
    if(options.hash)
    {
        digest = std::make_unique<Sha256>();
    }
    std::vector<unsigned char> actual(static_cast<std::size_t>(
        std::min(options.blob_chunk_size, object_size)));
    std::uint64_t offset = 0;
    SqliteBlob blob;
    int fd = -1;
    if(options.backend == "sqlite_blob")
    {
        CheckSqlite(db, sqlite3_blob_open(db, "main", "interaction_objects", "data",
            object_id, 0, blob.out()), "open blob for verification");
        if(static_cast<std::uint64_t>(sqlite3_blob_bytes(blob.get())) != object_size)
        {
            Fail("blob size mismatch for object " + std::to_string(object_index));
        }
    }
    else
    {
        const fs::path path = output_dir / object_key;
        fd = ::open(path.c_str(), O_RDONLY);
        if(fd < 0)
        {
            Fail("open raw file for verification failed: " + path.string());
        }
    }

    try
    {
        while(offset < object_size)
        {
            const std::size_t size = static_cast<std::size_t>(
                std::min<std::uint64_t>(actual.size(), object_size - offset));
            actual.resize(size);
            if(options.backend == "sqlite_blob")
            {
                CheckSqlite(db, sqlite3_blob_read(blob.get(), actual.data(),
                    static_cast<int>(actual.size()), static_cast<int>(offset)),
                    "read blob for verification");
            }
            else
            {
                std::size_t read = 0;
                while(read < actual.size())
                {
                    const ssize_t rc = ::read(fd, actual.data() + read, actual.size() - read);
                    if(rc < 0 && errno == EINTR) continue;
                    if(rc <= 0) Fail("read raw file for verification failed");
                    read += static_cast<std::size_t>(rc);
                }
            }
            std::vector<unsigned char> expected(actual.size());
            FillPattern(&expected, object_index, offset, options.seed);
            if(actual != expected)
            {
                Fail("raw bytes mismatch for object " + std::to_string(object_index));
            }
            if(options.hash)
            {
                digest->Update(actual.data(), actual.size());
            }
            offset += actual.size();
        }
    }
    catch(...)
    {
        if(fd >= 0) ::close(fd);
        throw;
    }
    if(fd >= 0 && ::close(fd) != 0)
    {
        Fail("close raw file after verification failed");
    }
    if(options.backend == "sqlite_blob")
    {
        CheckSqlite(db, blob.Close(), "close blob after verification");
    }
    if(options.hash && digest->Final() != expected_hash)
    {
        Fail("SHA-256 mismatch for object " + std::to_string(object_index));
    }
}

void Verify(sqlite3 *db,
            const fs::path &output_dir,
            const Options &options,
            std::uint64_t expected_count)
{
    const std::int64_t count = QueryInt64(db,
        "SELECT COUNT(*) FROM interaction_objects WHERE state=1;");
    if(count != static_cast<std::int64_t>(expected_count))
    {
        Fail("ready object count mismatch: expected " + std::to_string(expected_count)
            + ", actual " + std::to_string(count));
    }
    if(HasRows(db, "PRAGMA foreign_key_check;"))
    {
        Fail("foreign_key_check returned rows");
    }
    if(QueryText(db, "PRAGMA integrity_check;") != "ok")
    {
        Fail("PRAGMA integrity_check did not return ok");
    }

    sqlite3_stmt *stmt = nullptr;
    CheckSqlite(db, sqlite3_prepare_v2(db,
        "SELECT id,size_bytes,sha256,state,object_key "
        "FROM interaction_objects ORDER BY id;", -1, &stmt, nullptr),
        "prepare verification query");
    try
    {
        while(true)
        {
            const int step_rc = sqlite3_step(stmt);
            if(step_rc == SQLITE_DONE) break;
            CheckSqlite(db, step_rc, "step verification query");
            const std::int64_t object_id = sqlite3_column_int64(stmt, 0);
            const std::uint64_t object_size = static_cast<std::uint64_t>(sqlite3_column_int64(stmt, 1));
            const char *hash = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 2));
            const std::int32_t state = sqlite3_column_int(stmt, 3);
            const char *object_key = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 4));
            if(state != kObjectReady)
            {
                Fail("object is not ready: " + std::to_string(object_id));
            }
            if(!object_key)
            {
                Fail("ready object has no object_key: " + std::to_string(object_id));
            }
            const std::uint64_t object_index = ObjectIndexFromKey(object_key);
            VerifyRaw(db, object_id, object_index, object_size, hash ? hash : "",
                output_dir, options, object_key);
        }
    }
    catch(...)
    {
        sqlite3_finalize(stmt);
        throw;
    }
    CheckSqlite(db, sqlite3_finalize(stmt), "finalize verification query");
}

double Percentile(std::vector<std::int64_t> values, double percentile)
{
    if(values.empty()) return 0.0;
    std::sort(values.begin(), values.end());
    const double index = percentile * static_cast<double>(values.size() - 1);
    const std::size_t lower = static_cast<std::size_t>(index);
    const std::size_t upper = std::min(lower + 1, values.size() - 1);
    const double fraction = index - static_cast<double>(lower);
    return static_cast<double>(values[lower])
        + fraction * static_cast<double>(values[upper] - values[lower]);
}

std::uint64_t DirectoryBytes(const fs::path &directory)
{
    std::uint64_t total = 0;
    std::error_code error;
    if(!fs::exists(directory, error)) return 0;
    for(const fs::directory_entry &entry : fs::recursive_directory_iterator(directory, error))
    {
        if(error) break;
        if(entry.is_regular_file(error))
        {
            total += entry.file_size(error);
        }
    }
    return total;
}

std::uint64_t FileBytes(const fs::path &path)
{
    std::error_code error;
    if(!fs::is_regular_file(path, error)) return 0;
    return fs::file_size(path, error);
}

std::string Timestamp()
{
    const auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm local_time{};
    localtime_r(&now, &local_time);
    std::ostringstream out;
    out << std::put_time(&local_time, "%Y%m%d-%H%M%S");
    return out.str();
}

fs::path CreateOutputDir(const Options &options)
{
    fs::path output = options.output_dir;
    if(output.empty())
    {
        output = fs::path("tests/benchmarks/results")
            / (Timestamp() + "-" + options.backend + "-" + options.workload);
        int suffix = 0;
        while(fs::exists(output))
        {
            output = fs::path("tests/benchmarks/results")
                / (Timestamp() + "-" + options.backend + "-" + options.workload
                   + "-" + std::to_string(++suffix));
        }
    }
    std::error_code error;
    fs::create_directories(output, error);
    if(error)
    {
        Fail("create output directory failed: " + output.string() + ": " + error.message());
    }
    if(fs::exists(output / "interaction_history.sqlite"))
    {
        Fail("output directory already contains interaction_history.sqlite: " + output.string());
    }
    fs::create_directories(output / "raw", error);
    if(error)
    {
        Fail("create raw directory failed: " + error.message());
    }
    fs::create_directories(output / "export", error);
    if(error)
    {
        Fail("create export directory failed: " + error.message());
    }
    return output;
}

void WriteEnvironment(const fs::path &output_dir, const Options &options)
{
    struct utsname system_info{};
    ::uname(&system_info);
    char hostname[256] = {};
    ::gethostname(hostname, sizeof(hostname) - 1);
    struct statvfs disk_info{};
    ::statvfs(output_dir.c_str(), &disk_info);
    const std::uint64_t free_bytes = static_cast<std::uint64_t>(disk_info.f_bavail)
        * static_cast<std::uint64_t>(disk_info.f_frsize);

    std::ofstream out(output_dir / "environment.json");
    if(!out) Fail("open environment.json failed");
    out << "{\n"
        << "  \"hostname\": \"" << JsonEscape(hostname) << "\",\n"
        << "  \"kernel\": \"" << JsonEscape(system_info.sysname) << " "
        << JsonEscape(system_info.release) << "\",\n"
        << "  \"machine\": \"" << JsonEscape(system_info.machine) << "\",\n"
        << "  \"cpu_threads\": " << std::thread::hardware_concurrency() << ",\n"
        << "  \"sqlite_version\": \"" << sqlite3_libversion() << "\",\n"
        << "  \"openssl_version\": \"" << JsonEscape(OpenSSL_version(OPENSSL_VERSION)) << "\",\n"
        << "  \"backend\": \"" << JsonEscape(options.backend) << "\",\n"
        << "  \"hash\": \"" << (options.hash ? "on" : "off") << "\",\n"
        << "  \"synchronous\": \"" << JsonEscape(options.synchronous) << "\",\n"
        << "  \"blob_chunk_size\": " << options.blob_chunk_size << ",\n"
        << "  \"free_bytes_at_start\": " << free_bytes << "\n"
        << "}\n";
}

void WriteSamples(const fs::path &output_dir, const std::vector<Sample> &samples)
{
    std::ofstream out(output_dir / "samples.csv");
    if(!out) Fail("open samples.csv failed");
    out << "object_index,object_size,hash_us,writer_wait_us,transaction_us,commit_us,end_to_end_us\n";
    for(const Sample &sample : samples)
    {
        out << sample.object_index << ',' << sample.object_size << ','
            << sample.hash_us << ',' << sample.writer_wait_us << ','
            << sample.transaction_us << ',' << sample.commit_us << ','
            << sample.end_to_end_us << '\n';
    }
}

void WriteSummary(const fs::path &output_dir,
                  const Options &options,
                  const std::vector<Sample> &samples,
                  bool correctness_ok,
                  const QueueStats &queue_stats = {},
                  std::int64_t wall_time_us = 0)
{
    std::vector<std::int64_t> end_to_end;
    std::vector<std::int64_t> writer_wait;
    std::vector<std::int64_t> transactions;
    std::vector<std::int64_t> commits;
    std::int64_t total_bytes = 0;
    std::int64_t total_e2e_us = 0;
    for(const Sample &sample : samples)
    {
        end_to_end.push_back(sample.end_to_end_us);
        writer_wait.push_back(sample.writer_wait_us);
        transactions.push_back(sample.transaction_us);
        commits.push_back(sample.commit_us);
        total_bytes += static_cast<std::int64_t>(sample.object_size);
        total_e2e_us += sample.end_to_end_us;
    }
    const std::int64_t elapsed_us = wall_time_us > 0 ? wall_time_us : total_e2e_us;
    const double seconds = static_cast<double>(elapsed_us) / 1000000.0;
    const double records_per_second = seconds > 0.0
        ? static_cast<double>(samples.size()) / seconds : 0.0;
    const double mib_per_second = seconds > 0.0
        ? static_cast<double>(total_bytes) / static_cast<double>(kMiB) / seconds : 0.0;

    std::ofstream out(output_dir / "summary.json");
    if(!out) Fail("open summary.json failed");
    out << "{\n"
        << "  \"backend\": \"" << JsonEscape(options.backend) << "\",\n"
        << "  \"hash\": \"" << (options.hash ? "on" : "off") << "\",\n"
        << "  \"workload\": \"" << JsonEscape(options.workload) << "\",\n"
        << "  \"synchronous\": \"" << JsonEscape(options.synchronous) << "\",\n"
        << "  \"blob_chunk_size\": " << options.blob_chunk_size << ",\n"
        << "  \"queue_capacity\": " << queue_stats.capacity << ",\n"
        << "  \"queue_peak\": " << queue_stats.peak << ",\n"
        << "  \"queue_enqueued\": " << queue_stats.enqueued << ",\n"
        << "  \"producer_count\": " << queue_stats.producers << ",\n"
        << "  \"write_wall_time_us\": " << elapsed_us << ",\n"
        << "  \"object_count\": " << samples.size() << ",\n"
        << "  \"raw_bytes\": " << total_bytes << ",\n"
        << "  \"records_per_second\": " << records_per_second << ",\n"
        << "  \"raw_mib_per_second\": " << mib_per_second << ",\n"
        << "  \"end_to_end_p50_us\": " << Percentile(end_to_end, 0.50) << ",\n"
        << "  \"end_to_end_p95_us\": " << Percentile(end_to_end, 0.95) << ",\n"
        << "  \"end_to_end_p99_us\": " << Percentile(end_to_end, 0.99) << ",\n"
        << "  \"writer_wait_p95_us\": " << Percentile(writer_wait, 0.95) << ",\n"
        << "  \"transaction_p95_us\": " << Percentile(transactions, 0.95) << ",\n"
        << "  \"commit_p95_us\": " << Percentile(commits, 0.95) << ",\n"
        << "  \"database_bytes\": " << FileBytes(output_dir / "interaction_history.sqlite") << ",\n"
        << "  \"wal_bytes\": " << FileBytes(output_dir / "interaction_history.sqlite-wal") << ",\n"
        << "  \"shm_bytes\": " << FileBytes(output_dir / "interaction_history.sqlite-shm") << ",\n"
        << "  \"raw_file_bytes\": " << DirectoryBytes(output_dir / "raw") << ",\n"
        << "  \"correctness_ok\": " << (correctness_ok ? "true" : "false") << "\n"
        << "}\n";
}

void WriteReport(const fs::path &output_dir,
                 const Options &options,
                 const std::vector<Sample> &samples,
                 bool correctness_ok,
                 const QueueStats &queue_stats = {},
                 std::int64_t wall_time_us = 0)
{
    std::vector<std::int64_t> end_to_end;
    std::vector<std::int64_t> transactions;
    std::int64_t total_bytes = 0;
    std::int64_t total_time_us = 0;
    for(const Sample &sample : samples)
    {
        end_to_end.push_back(sample.end_to_end_us);
        transactions.push_back(sample.transaction_us);
        total_bytes += static_cast<std::int64_t>(sample.object_size);
        total_time_us += sample.end_to_end_us;
    }
    const std::int64_t elapsed_us = wall_time_us > 0 ? wall_time_us : total_time_us;
    const double seconds = static_cast<double>(elapsed_us) / 1000000.0;
    std::ofstream out(output_dir / "report.md");
    if(!out) Fail("open report.md failed");
    out << "# SQLite history storage benchmark\n\n"
        << "- Scope: initial P0/P1/P2 implementation; no concurrent queue, locator, export, cleanup, or crash injection.\n"
        << "- Backend: `" << options.backend << "`\n"
        << "- Hash: `" << (options.hash ? "on" : "off") << "`\n"
        << "- Workload: `" << options.workload << "`\n"
        << "- Synchronous: `" << options.synchronous << "`\n"
        << "- Blob chunk: `" << options.blob_chunk_size << "` bytes\n"
        << "- Queue capacity/peak: `" << queue_stats.capacity << "/"
        << queue_stats.peak << "`\n"
        << "- Producers: `" << queue_stats.producers << "`\n"
        << "- Write wall time: `" << elapsed_us << "` us\n"
        << "- Correctness: **" << (correctness_ok ? "PASS" : "FAIL") << "**\n\n"
        << "## Results\n\n"
        << "| Metric | Value |\n| --- | ---: |\n"
        << "| Objects | " << samples.size() << " |\n"
        << "| Raw bytes | " << total_bytes << " |\n"
        << "| Records/s | " << (seconds > 0 ? samples.size() / seconds : 0.0) << " |\n"
        << "| Raw MiB/s | " << (seconds > 0 ? total_bytes / static_cast<double>(kMiB) / seconds : 0.0) << " |\n"
        << "| End-to-end p50 (us) | " << Percentile(end_to_end, 0.50) << " |\n"
        << "| End-to-end p95 (us) | " << Percentile(end_to_end, 0.95) << " |\n"
        << "| End-to-end p99 (us) | " << Percentile(end_to_end, 0.99) << " |\n"
        << "| Transaction p95 (us) | " << Percentile(transactions, 0.95) << " |\n"
        << "| Database bytes | " << FileBytes(output_dir / "interaction_history.sqlite") << " |\n"
        << "| WAL bytes | " << FileBytes(output_dir / "interaction_history.sqlite-wal") << " |\n"
        << "| Raw file bytes | " << DirectoryBytes(output_dir / "raw") << " |\n\n"
        << "## Correctness checks\n\n"
        << "- `PRAGMA integrity_check`: `ok`\n"
        << "- `PRAGMA foreign_key_check`: no rows\n"
        << "- Object state: all `ready`\n"
        << "- Deterministic raw bytes: verified after write\n"
        << "- SHA-256: " << (options.hash ? "verified" : "disabled") << "\n";
}

struct StoredObject
{
    std::int64_t id = 0;
    std::uint64_t object_index = 0;
    std::uint64_t object_size = 0;
    std::string object_key;
};

struct StoredPayload
{
    std::int64_t source_object_id = 0;
    std::vector<std::pair<std::uint64_t, std::uint64_t>> ranges;
};

std::uint64_t ObjectIndexFromKey(const std::string &object_key)
{
    const std::string marker = "object_";
    const std::size_t marker_pos = object_key.rfind(marker);
    if(marker_pos == std::string::npos)
    {
        Fail("object key has no benchmark index: " + object_key);
    }
    const std::size_t begin = marker_pos + marker.size();
    const std::size_t end = object_key.find('.', begin);
    const std::string index_text = object_key.substr(begin,
        end == std::string::npos ? std::string::npos : end - begin);
    return ParseUnsigned(index_text, "object key index");
}

std::vector<std::pair<std::uint64_t, std::uint64_t>> ParseRangesJson(
    const std::string &ranges_json)
{
    const Json ranges = Json::parse(ranges_json);
    if(!ranges.is_array() || ranges.empty())
    {
        Fail("ranges_json must be a non-empty array");
    }
    std::vector<std::pair<std::uint64_t, std::uint64_t>> result;
    std::uint64_t total = 0;
    for(const Json &range : ranges)
    {
        if(!range.is_object() || !range.contains("offset")
            || !range.contains("length"))
        {
            Fail("ranges_json item is invalid");
        }
        const std::uint64_t offset = range.at("offset").get<std::uint64_t>();
        const std::uint64_t length = range.at("length").get<std::uint64_t>();
        if(length == 0 || length > std::numeric_limits<std::uint64_t>::max() - total)
        {
            Fail("ranges_json contains zero length or overflow");
        }
        total += length;
        result.emplace_back(offset, length);
    }
    return result;
}

std::vector<StoredObject> LoadObjects(sqlite3 *db)
{
    sqlite3_stmt *stmt = nullptr;
    CheckSqlite(db, sqlite3_prepare_v2(db,
        "SELECT id,size_bytes,object_key "
        "FROM interaction_objects WHERE state=1 ORDER BY id;",
        -1, &stmt, nullptr), "prepare object list");
    std::vector<StoredObject> objects;
    try
    {
        while(true)
        {
            const int step_rc = sqlite3_step(stmt);
            if(step_rc == SQLITE_DONE) break;
            CheckSqlite(db, step_rc, "step object list");
            StoredObject object;
            object.id = sqlite3_column_int64(stmt, 0);
            object.object_size = static_cast<std::uint64_t>(sqlite3_column_int64(stmt, 1));
            const char *key = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 2));
            object.object_key = key ? key : "";
            object.object_index = ObjectIndexFromKey(object.object_key);
            objects.push_back(std::move(object));
        }
    }
    catch(...)
    {
        sqlite3_finalize(stmt);
        throw;
    }
    CheckSqlite(db, sqlite3_finalize(stmt), "finalize object list");
    return objects;
}

std::vector<StoredPayload> LoadPayloads(sqlite3 *db)
{
    sqlite3_stmt *stmt = nullptr;
    CheckSqlite(db, sqlite3_prepare_v2(db,
        "SELECT source_object_id,ranges_json "
        "FROM interaction_payload_refs ORDER BY id;",
        -1, &stmt, nullptr), "prepare payload list");
    std::vector<StoredPayload> payloads;
    try
    {
        while(true)
        {
            const int step_rc = sqlite3_step(stmt);
            if(step_rc == SQLITE_DONE) break;
            CheckSqlite(db, step_rc, "step payload list");
            StoredPayload payload;
            payload.source_object_id = sqlite3_column_int64(stmt, 0);
            const char *ranges = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 1));
            if(!ranges)
            {
                Fail("payload ranges_json is null");
            }
            payload.ranges = ParseRangesJson(ranges);
            payloads.push_back(std::move(payload));
        }
    }
    catch(...)
    {
        sqlite3_finalize(stmt);
        throw;
    }
    CheckSqlite(db, sqlite3_finalize(stmt), "finalize payload list");
    return payloads;
}

std::vector<unsigned char> ReadRawSlice(sqlite3 *db,
                                        const fs::path &output_dir,
                                        const Options &options,
                                        const StoredObject &object,
                                        std::uint64_t offset,
                                        std::uint64_t length)
{
    if(offset > object.object_size || length > object.object_size - offset)
    {
        Fail("locator is out of bounds for object " + std::to_string(object.object_index));
    }
    std::vector<unsigned char> bytes(static_cast<std::size_t>(length));
    if(options.backend == "sqlite_blob")
    {
        SqliteBlob blob;
        CheckSqlite(db, sqlite3_blob_open(db, "main", "interaction_objects", "data",
            object.id, 0, blob.out()), "open blob for locator read");
        std::uint64_t read_offset = 0;
        try
        {
            while(read_offset < length)
            {
                const int chunk_size = static_cast<int>(std::min<std::uint64_t>(
                    options.blob_chunk_size, length - read_offset));
                CheckSqlite(db, sqlite3_blob_read(blob.get(), bytes.data() + read_offset,
                    chunk_size, static_cast<int>(offset + read_offset)),
                    "read blob locator");
                read_offset += static_cast<std::uint64_t>(chunk_size);
            }
        }
        catch(...)
        {
            throw;
        }
        CheckSqlite(db, blob.Close(), "close blob locator read");
    }
    else
    {
        const fs::path path = output_dir / object.object_key;
        const int fd = ::open(path.c_str(), O_RDONLY);
        if(fd < 0)
        {
            Fail("open raw file for locator read failed: " + path.string());
        }
        std::size_t read_offset = 0;
        try
        {
            while(read_offset < bytes.size())
            {
                const ssize_t rc = ::pread(fd, bytes.data() + read_offset,
                    bytes.size() - read_offset,
                    static_cast<off_t>(offset + read_offset));
                if(rc < 0 && errno == EINTR) continue;
                if(rc <= 0) Fail("read raw file locator failed");
                read_offset += static_cast<std::size_t>(rc);
            }
        }
        catch(...)
        {
            ::close(fd);
            throw;
        }
        if(::close(fd) != 0)
        {
            Fail("close raw file locator failed");
        }
    }
    return bytes;
}

void VerifySlice(const StoredObject &object,
                 std::uint64_t offset,
                 const std::vector<unsigned char> &bytes,
                 const Options &options)
{
    std::vector<unsigned char> expected(bytes.size());
    FillPattern(&expected, object.object_index, offset, options.seed);
    if(bytes != expected)
    {
        Fail("locator bytes mismatch for object " + std::to_string(object.object_index));
    }
}

void ReadPayload(sqlite3 *db,
                 const fs::path &output_dir,
                 const Options &options,
                 const StoredObject &object,
                 const StoredPayload &payload)
{
    if(payload.ranges.empty())
    {
        Fail("payload ranges are empty");
    }
    for(const auto &[offset, length] : payload.ranges)
    {
        const std::vector<unsigned char> bytes = ReadRawSlice(db, output_dir, options,
            object, offset, length);
        VerifySlice(object, offset, bytes, options);
    }
}

void RunListQuery(sqlite3 *db)
{
    sqlite3_stmt *stmt = nullptr;
    CheckSqlite(db, sqlite3_prepare_v2(db,
        "SELECT id,project_id,request_received_ms,scope,protocol_type,result "
        "FROM interaction_records ORDER BY request_received_ms DESC,id DESC LIMIT 100;",
        -1, &stmt, nullptr),
        "prepare history list");
    try
    {
        while(true)
        {
            const int step_rc = sqlite3_step(stmt);
            if(step_rc == SQLITE_DONE) break;
            CheckSqlite(db, step_rc, "step history list");
        }
    }
    catch(...)
    {
        sqlite3_finalize(stmt);
        throw;
    }
    CheckSqlite(db, sqlite3_finalize(stmt), "finalize history list");
}

void RunDetailQuery(sqlite3 *db, const StoredObject &object)
{
    sqlite3_stmt *stmt = nullptr;
    CheckSqlite(db, sqlite3_prepare_v2(db,
        "SELECT id,request_object_id,response_object_id,scope,protocol_type,result "
        "FROM interaction_records WHERE request_object_id=?;",
        -1, &stmt, nullptr), "prepare history detail");
    sqlite3_bind_int64(stmt, 1, object.id);
    const int step_rc = sqlite3_step(stmt);
    if(step_rc != SQLITE_ROW)
    {
        sqlite3_finalize(stmt);
        Fail("history detail returned no row");
    }
    if(sqlite3_column_int64(stmt, 1) != object.id
        || sqlite3_column_int(stmt, 3) != kScopeProtocol
        || sqlite3_column_int(stmt, 4) != kProtocolTypeHttp
        || sqlite3_column_int(stmt, 5) != kResultMatched)
    {
        sqlite3_finalize(stmt);
        Fail("history detail mismatch");
    }
    CheckSqlite(db, sqlite3_finalize(stmt), "finalize history detail");
}

struct ReadResults
{
    std::vector<std::int64_t> list_us;
    std::vector<std::int64_t> detail_us;
    std::vector<std::int64_t> range_us;
    std::vector<std::int64_t> segments_us;
};

void WriteReadResults(const fs::path &output_dir,
                      const Options &options,
                      const ReadResults &results,
                      std::uint64_t object_count,
                      bool correctness_ok,
                      std::uint64_t concurrent_writer_objects = 0)
{
    auto average_size = [](const std::vector<std::int64_t> &values) {
        if(values.empty()) return 0.0;
        std::int64_t total = 0;
        for(const std::int64_t value : values) total += value;
        return static_cast<double>(total) / static_cast<double>(values.size());
    };
    std::ofstream out(output_dir / "read_summary.json");
    if(!out) Fail("open read_summary.json failed");
    out << "{\n"
        << "  \"backend\": \"" << JsonEscape(options.backend) << "\",\n"
        << "  \"object_count\": " << object_count << ",\n"
        << "  \"list_operations\": " << results.list_us.size() << ",\n"
        << "  \"detail_operations\": " << results.detail_us.size() << ",\n"
        << "  \"range_operations\": " << results.range_us.size() << ",\n"
        << "  \"segments_operations\": " << results.segments_us.size() << ",\n"
        << "  \"concurrent_writer_objects\": " << concurrent_writer_objects << ",\n"
        << "  \"list_p50_us\": " << Percentile(results.list_us, 0.50) << ",\n"
        << "  \"list_p95_us\": " << Percentile(results.list_us, 0.95) << ",\n"
        << "  \"list_average_us\": " << average_size(results.list_us) << ",\n"
        << "  \"detail_p50_us\": " << Percentile(results.detail_us, 0.50) << ",\n"
        << "  \"detail_p95_us\": " << Percentile(results.detail_us, 0.95) << ",\n"
        << "  \"range_p50_us\": " << Percentile(results.range_us, 0.50) << ",\n"
        << "  \"range_p95_us\": " << Percentile(results.range_us, 0.95) << ",\n"
        << "  \"segments_p50_us\": " << Percentile(results.segments_us, 0.50) << ",\n"
        << "  \"segments_p95_us\": " << Percentile(results.segments_us, 0.95) << ",\n"
        << "  \"correctness_ok\": " << (correctness_ok ? "true" : "false") << "\n"
        << "}\n";

    std::ofstream report(output_dir / "read_report.md");
    if(!report) Fail("open read_report.md failed");
    report << "# SQLite history read benchmark\n\n"
        << "- Backend: `" << options.backend << "`\n"
        << "- Objects: `" << object_count << "`\n"
        << "- Correctness: **" << (correctness_ok ? "PASS" : "FAIL") << "**\n\n"
        << "| Operation | p50 (us) | p95 (us) | Count |\n"
        << "| --- | ---: | ---: | ---: |\n"
        << "| List | " << Percentile(results.list_us, 0.50) << " | "
        << Percentile(results.list_us, 0.95) << " | " << results.list_us.size() << " |\n"
        << "| Detail | " << Percentile(results.detail_us, 0.50) << " | "
        << Percentile(results.detail_us, 0.95) << " | " << results.detail_us.size() << " |\n"
        << "| Range | " << Percentile(results.range_us, 0.50) << " | "
        << Percentile(results.range_us, 0.95) << " | " << results.range_us.size() << " |\n"
        << "| Segments | " << Percentile(results.segments_us, 0.50) << " | "
        << Percentile(results.segments_us, 0.95) << " | " << results.segments_us.size() << " |\n";
}

int RunRead(const Options &options)
{
    const fs::path output_dir = CreateOutputDir(options);
    const fs::path database_path = output_dir / "interaction_history.sqlite";
    WriteEnvironment(output_dir, options);

    SqliteDb database(database_path);
    sqlite3 *db = database.get();
    ConfigureDatabase(db, options);

    Options seed_options = options;
    seed_options.workload = "profile";
    if(seed_options.object_count == 0)
    {
        seed_options.object_count = 8;
    }
    const std::uint64_t object_count = ObjectCount(seed_options);
    const std::vector<std::uint64_t> profile_sizes = ProfileSizes(seed_options);
    std::vector<Sample> seed_samples;
    seed_samples.reserve(static_cast<std::size_t>(object_count));
    const TimePoint seed_start = Clock::now();
    for(std::uint64_t object_index = 0; object_index < object_count; ++object_index)
    {
        const std::uint64_t object_size = profile_sizes[
            static_cast<std::size_t>(object_index % profile_sizes.size())];
        const TimePoint hash_start = Clock::now();
        const std::string sha256 = options.hash
            ? HashObject(object_index, object_size, options) : "";
        Sample sample = options.backend == "sqlite_blob"
            ? WriteSqliteObject(db, object_index, object_size, sha256, options)
            : WriteFileBackend(db, output_dir / "raw", object_index, object_size, sha256, options);
        sample.hash_us = MicrosSince(hash_start);
        seed_samples.push_back(sample);
    }
    const std::int64_t seed_wall_time_us = MicrosSince(seed_start);
    Verify(db, output_dir, options, object_count);

    const std::vector<StoredObject> objects = LoadObjects(db);
    const std::vector<StoredPayload> payloads = LoadPayloads(db);
    if(objects.size() != object_count || payloads.size() != object_count * 2)
    {
        Fail("read workload seed metadata count mismatch");
    }
    std::vector<StoredObject> object_by_id = objects;
    std::vector<StoredPayload> range_payloads;
    std::vector<StoredPayload> segment_payloads;
    for(const StoredPayload &payload : payloads)
    {
        if(payload.ranges.size() == 1) range_payloads.push_back(payload);
        else segment_payloads.push_back(payload);
    }
    if(range_payloads.empty() || segment_payloads.empty())
    {
        Fail("read workload did not create both single-range and multi-range payload refs");
    }

    auto find_object = [&](std::int64_t object_id) -> const StoredObject & {
        for(const StoredObject &object : object_by_id)
        {
            if(object.id == object_id) return object;
        }
        Fail("payload references missing object");
    };

    ReadResults results;
    results.list_us.reserve(options.read_operations);
    results.detail_us.reserve(options.read_operations);
    results.range_us.reserve(options.read_operations);
    results.segments_us.reserve(options.read_operations);
    std::exception_ptr writer_error;
    std::mutex writer_error_mutex;
    std::thread concurrent_writer;
    std::uint64_t concurrent_writer_objects = 0;
    if(options.duration_seconds > 0 && options.write_rate > 0)
    {
        concurrent_writer_objects = static_cast<std::uint64_t>(options.duration_seconds)
            * static_cast<std::uint64_t>(options.write_rate);
        concurrent_writer = std::thread([&] {
            try
            {
                SqliteDb writer_db(database_path);
                sqlite3 *writer_handle = writer_db.get();
                ConfigureDatabase(writer_handle, options);
                Options writer_options = options;
                writer_options.workload = "mixed";
                const std::vector<std::uint64_t> writer_sizes = ProfileSizes(writer_options);
                const std::int64_t interval_us = std::max<std::int64_t>(
                    1, 1000000 / options.write_rate);
                const TimePoint schedule_start = Clock::now();
                for(std::uint64_t i = 0; i < concurrent_writer_objects; ++i)
                {
                    std::this_thread::sleep_until(schedule_start
                        + std::chrono::microseconds(static_cast<std::int64_t>(i) * interval_us));
                    const std::uint64_t object_index = 5000000 + i;
                    const std::uint64_t object_size = writer_sizes[
                        static_cast<std::size_t>(i % writer_sizes.size())];
                    const std::string sha256 = options.hash
                        ? HashObject(object_index, object_size, writer_options) : "";
                    if(options.backend == "sqlite_blob")
                    {
                        (void)WriteSqliteObject(writer_handle, object_index, object_size,
                            sha256, writer_options);
                    }
                    else
                    {
                        (void)WriteFileBackend(writer_handle, output_dir / "raw", object_index,
                            object_size, sha256, writer_options);
                    }
                }
            }
            catch(...)
            {
                std::lock_guard<std::mutex> lock(writer_error_mutex);
                writer_error = std::current_exception();
            }
        });
    }
    try
    {
        for(std::uint64_t i = 0; i < options.read_operations; ++i)
        {
            TimePoint start = Clock::now();
            RunListQuery(db);
            results.list_us.push_back(MicrosSince(start));

            const StoredObject &object = objects[static_cast<std::size_t>(i % objects.size())];
            start = Clock::now();
            RunDetailQuery(db, object);
            results.detail_us.push_back(MicrosSince(start));

            const StoredPayload &range = range_payloads[
                static_cast<std::size_t>(i % range_payloads.size())];
            start = Clock::now();
            ReadPayload(db, output_dir, options, find_object(range.source_object_id), range);
            results.range_us.push_back(MicrosSince(start));

            const StoredPayload &segments = segment_payloads[
                static_cast<std::size_t>(i % segment_payloads.size())];
            start = Clock::now();
            ReadPayload(db, output_dir, options, find_object(segments.source_object_id), segments);
            results.segments_us.push_back(MicrosSince(start));
        }
    }
    catch(...)
    {
        if(concurrent_writer.joinable()) concurrent_writer.join();
        throw;
    }
    if(concurrent_writer.joinable()) concurrent_writer.join();
    if(writer_error) std::rethrow_exception(writer_error);

    WriteSamples(output_dir, seed_samples);
    WriteSummary(output_dir, options, seed_samples, true, {}, seed_wall_time_us);
    WriteReport(output_dir, options, seed_samples, true, {}, seed_wall_time_us);
    WriteReadResults(output_dir, options, results, object_count, true,
        concurrent_writer_objects);
    std::cout << "output_dir=" << output_dir << '\n'
              << "database=" << database_path << '\n'
              << "objects=" << object_count << '\n'
              << "read_operations=" << options.read_operations << '\n';
    return 0;
}

class Crc32
{
public:
    void Update(const unsigned char *data, std::size_t size)
    {
        const auto &table = Table();
        for(std::size_t i = 0; i < size; ++i)
        {
            value_ = table[(value_ ^ data[i]) & 0xffU] ^ (value_ >> 8);
        }
    }

    std::uint32_t Final() const
    {
        return ~value_;
    }

private:
    static const std::array<std::uint32_t, 256> &Table()
    {
        static const std::array<std::uint32_t, 256> table = [] {
            std::array<std::uint32_t, 256> result{};
            for(std::uint32_t i = 0; i < result.size(); ++i)
            {
                std::uint32_t value = i;
                for(int bit = 0; bit < 8; ++bit)
                {
                    value = (value & 1U) ? (value >> 1) ^ 0xedb88320U : value >> 1;
                }
                result[i] = value;
            }
            return result;
        }();
        return table;
    }

    std::uint32_t value_ = 0xffffffffU;
};

class ZipWriter
{
public:
    explicit ZipWriter(const fs::path &path)
        : out_(path, std::ios::binary | std::ios::trunc)
    {
        if(!out_) Fail("open ZIP export failed: " + path.string());
    }

    ~ZipWriter()
    {
        out_.close();
    }

    void BeginEntry(const std::string &name)
    {
        if(entry_open_ || name.size() > std::numeric_limits<std::uint16_t>::max())
        {
            Fail("invalid ZIP entry state or name");
        }
        const std::uint64_t offset = CurrentOffset();
        if(offset > std::numeric_limits<std::uint32_t>::max())
        {
            Fail("ZIP exceeds classic ZIP offset limit");
        }
        current_ = {name, 0, 0, static_cast<std::uint32_t>(offset)};
        Write32(0x04034b50U);
        Write16(20);
        Write16(0x0008);
        Write16(0);
        Write16(0);
        Write16(0);
        Write32(0);
        Write32(0);
        Write32(0);
        Write16(static_cast<std::uint16_t>(name.size()));
        Write16(0);
        out_.write(name.data(), static_cast<std::streamsize>(name.size()));
        if(!out_) Fail("write ZIP local header failed");
        entry_open_ = true;
        crc_ = Crc32();
        size_ = 0;
    }

    void Write(const unsigned char *data, std::size_t size)
    {
        if(!entry_open_) Fail("ZIP entry is not open");
        if(size_ > std::numeric_limits<std::uint32_t>::max() - size)
        {
            Fail("ZIP entry exceeds classic ZIP size limit");
        }
        out_.write(reinterpret_cast<const char *>(data), static_cast<std::streamsize>(size));
        if(!out_) Fail("write ZIP entry failed");
        crc_.Update(data, size);
        size_ += static_cast<std::uint32_t>(size);
    }

    void EndEntry()
    {
        if(!entry_open_) Fail("ZIP entry is not open");
        const std::uint32_t crc = crc_.Final();
        Write32(0x08074b50U);
        Write32(crc);
        Write32(size_);
        Write32(size_);
        current_.crc = crc;
        current_.size = size_;
        entries_.push_back(current_);
        entry_open_ = false;
    }

    void Finish()
    {
        if(entry_open_) Fail("cannot finish ZIP with an open entry");
        const std::uint64_t central_offset = CurrentOffset();
        for(const Entry &entry : entries_)
        {
            Write32(0x02014b50U);
            Write16(20);
            Write16(20);
            Write16(0x0008);
            Write16(0);
            Write16(0);
            Write16(0);
            Write32(entry.crc);
            Write32(entry.size);
            Write32(entry.size);
            Write16(static_cast<std::uint16_t>(entry.name.size()));
            Write16(0);
            Write16(0);
            Write16(0);
            Write16(0);
            Write32(0);
            Write32(entry.local_offset);
            out_.write(entry.name.data(), static_cast<std::streamsize>(entry.name.size()));
        }
        const std::uint64_t central_end = CurrentOffset();
        const std::uint64_t central_size = central_end - central_offset;
        if(entries_.size() > std::numeric_limits<std::uint16_t>::max()
            || central_offset > std::numeric_limits<std::uint32_t>::max()
            || central_size > std::numeric_limits<std::uint32_t>::max())
        {
            Fail("ZIP central directory exceeds classic ZIP limit");
        }
        Write32(0x06054b50U);
        Write16(0);
        Write16(0);
        Write16(static_cast<std::uint16_t>(entries_.size()));
        Write16(static_cast<std::uint16_t>(entries_.size()));
        Write32(static_cast<std::uint32_t>(central_size));
        Write32(static_cast<std::uint32_t>(central_offset));
        Write16(0);
        out_.flush();
        if(!out_) Fail("finish ZIP export failed");
    }

private:
    struct Entry
    {
        std::string name;
        std::uint32_t crc = 0;
        std::uint32_t size = 0;
        std::uint32_t local_offset = 0;
    };

    std::uint64_t CurrentOffset()
    {
        const std::streampos position = out_.tellp();
        if(position < 0) Fail("tell ZIP position failed");
        return static_cast<std::uint64_t>(position);
    }

    void Write16(std::uint16_t value)
    {
        const char bytes[2] = {
            static_cast<char>(value & 0xffU),
            static_cast<char>((value >> 8) & 0xffU)};
        out_.write(bytes, sizeof(bytes));
    }

    void Write32(std::uint32_t value)
    {
        const char bytes[4] = {
            static_cast<char>(value & 0xffU),
            static_cast<char>((value >> 8) & 0xffU),
            static_cast<char>((value >> 16) & 0xffU),
            static_cast<char>((value >> 24) & 0xffU)};
        out_.write(bytes, sizeof(bytes));
    }

    std::ofstream out_;
    std::vector<Entry> entries_;
    Entry current_;
    Crc32 crc_;
    std::uint32_t size_ = 0;
    bool entry_open_ = false;
};

void ExportObject(ZipWriter *zip,
                  sqlite3 *db,
                  const fs::path &output_dir,
                  const Options &options,
                  const StoredObject &object,
                  std::uint64_t *bytes_written)
{
    zip->BeginEntry("object_" + std::to_string(object.object_index) + ".bin");
    std::uint64_t offset = 0;
    const TimePoint rate_start = Clock::now();
    while(offset < object.object_size)
    {
        const std::uint64_t length = std::min<std::uint64_t>(
            options.blob_chunk_size, object.object_size - offset);
        const std::vector<unsigned char> bytes = ReadRawSlice(
            db, output_dir, options, object, offset, length);
        VerifySlice(object, offset, bytes, options);
        zip->Write(bytes.data(), bytes.size());
        offset += length;
        *bytes_written += length;
        if(options.export_rate_limit > 0)
        {
            const std::uint64_t target_us = (*bytes_written * 1000000ULL)
                / options.export_rate_limit;
            const auto target = rate_start + std::chrono::microseconds(
                static_cast<std::int64_t>(target_us));
            std::this_thread::sleep_until(target);
        }
    }
    zip->EndEntry();
}

struct ExportJobResult
{
    std::uint64_t bytes = 0;
    std::int64_t wall_time_us = 0;
    fs::path path;
};

std::int64_t CreateExportJob(sqlite3 *db,
                             const std::vector<StoredObject> &objects)
{
    Exec(db, "BEGIN IMMEDIATE;");
    bool active = true;
    try
    {
        const std::int64_t now_ms = WallClockMillis();
        sqlite3_stmt *job_stmt = nullptr;
        CheckSqlite(db, sqlite3_prepare_v2(db,
            "INSERT INTO interaction_export_jobs(project_id,created_by,state,filter_json,"
            "error_message,created_at_ms) VALUES(?,?,?,?,?,?);",
            -1, &job_stmt, nullptr), "prepare export job insert");
        sqlite3_bind_int64(job_stmt, 1, 1);
        sqlite3_bind_int64(job_stmt, 2, 1);
        sqlite3_bind_int(job_stmt, 3, 2);
        sqlite3_bind_text(job_stmt, 4, R"({"benchmark":true})", -1, SQLITE_STATIC);
        sqlite3_bind_text(job_stmt, 5, "", -1, SQLITE_STATIC);
        sqlite3_bind_int64(job_stmt, 6, now_ms);
        const int job_step_rc = sqlite3_step(job_stmt);
        const int job_finalize_rc = sqlite3_finalize(job_stmt);
        CheckSqlite(db, job_step_rc, "insert export job");
        CheckSqlite(db, job_finalize_rc, "finalize export job insert");
        const std::int64_t job_id = sqlite3_last_insert_rowid(db);

        sqlite3_stmt *pin_stmt = nullptr;
        CheckSqlite(db, sqlite3_prepare_v2(db,
            "INSERT INTO interaction_export_job_records(job_id,record_id) "
            "SELECT ?,id FROM interaction_records WHERE request_object_id=?;",
            -1, &pin_stmt, nullptr), "prepare export pin insert");
        for(const StoredObject &object : objects)
        {
            sqlite3_reset(pin_stmt);
            sqlite3_clear_bindings(pin_stmt);
            sqlite3_bind_int64(pin_stmt, 1, job_id);
            sqlite3_bind_int64(pin_stmt, 2, object.id);
            CheckSqlite(db, sqlite3_step(pin_stmt), "insert export pin");
        }
        CheckSqlite(db, sqlite3_finalize(pin_stmt), "finalize export pin insert");
        Exec(db, "COMMIT;");
        active = false;
        return job_id;
    }
    catch(...)
    {
        if(active) RollbackNoThrow(db);
        throw;
    }
}

void CompleteExportJob(sqlite3 *db,
                       std::int64_t job_id,
                       const fs::path &zip_path,
                       std::uint64_t bytes)
{
    Exec(db, "BEGIN IMMEDIATE;");
    bool active = true;
    try
    {
        sqlite3_stmt *stmt = nullptr;
        CheckSqlite(db, sqlite3_prepare_v2(db,
            "UPDATE interaction_export_jobs SET state=3,storage_location_id=?,"
            "object_key=?,size_bytes=?,completed_at_ms=?,expires_at_ms=? WHERE id=?;",
            -1, &stmt, nullptr), "prepare export job complete");
        const std::string key = "export/" + zip_path.filename().string();
        const std::int64_t now_ms = WallClockMillis();
        sqlite3_bind_text(stmt, 1, "local_export", -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 2, key.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int64(stmt, 3, static_cast<sqlite3_int64>(bytes));
        sqlite3_bind_int64(stmt, 4, now_ms);
        sqlite3_bind_int64(stmt, 5, now_ms + 3600000);
        sqlite3_bind_int64(stmt, 6, job_id);
        const int step_rc = sqlite3_step(stmt);
        const int finalize_rc = sqlite3_finalize(stmt);
        CheckSqlite(db, step_rc, "complete export job");
        CheckSqlite(db, finalize_rc, "finalize export job complete");
        sqlite3_stmt *pin_stmt = nullptr;
        CheckSqlite(db, sqlite3_prepare_v2(db,
            "DELETE FROM interaction_export_job_records WHERE job_id=?;",
            -1, &pin_stmt, nullptr), "prepare export pin release");
        sqlite3_bind_int64(pin_stmt, 1, job_id);
        const int pin_step_rc = sqlite3_step(pin_stmt);
        const int pin_finalize_rc = sqlite3_finalize(pin_stmt);
        CheckSqlite(db, pin_step_rc, "release export pins");
        CheckSqlite(db, pin_finalize_rc, "finalize export pin release");
        Exec(db, "COMMIT;");
        active = false;
    }
    catch(...)
    {
        if(active) RollbackNoThrow(db);
        throw;
    }
}

void WriteExportResults(const fs::path &output_dir,
                        const Options &options,
                        const std::vector<ExportJobResult> &jobs,
                        bool correctness_ok)
{
    std::uint64_t total_bytes = 0;
    std::int64_t total_wall_time = 0;
    for(const ExportJobResult &job : jobs)
    {
        total_bytes += job.bytes;
        total_wall_time += job.wall_time_us;
    }
    std::ofstream out(output_dir / "export_summary.json");
    if(!out) Fail("open export_summary.json failed");
    out << "{\n"
        << "  \"backend\": \"" << JsonEscape(options.backend) << "\",\n"
        << "  \"export_concurrency\": " << jobs.size() << ",\n"
        << "  \"export_rate_limit\": " << options.export_rate_limit << ",\n"
        << "  \"export_jobs\": " << jobs.size() << ",\n"
        << "  \"export_bytes_per_job\": " << (jobs.empty() ? 0 : jobs.front().bytes) << ",\n"
        << "  \"export_total_job_wall_time_us\": " << total_wall_time << ",\n"
        << "  \"correctness_ok\": " << (correctness_ok ? "true" : "false") << "\n"
        << "}\n";
    std::ofstream report(output_dir / "export_report.md");
    if(!report) Fail("open export_report.md failed");
    report << "# SQLite history export benchmark\n\n"
        << "- Backend: `" << options.backend << "`\n"
        << "- Export concurrency: `" << jobs.size() << "`\n"
        << "- Export rate limit: `" << options.export_rate_limit << "` bytes/s\n"
        << "- Bytes per job: `" << (jobs.empty() ? 0 : jobs.front().bytes) << "`\n"
        << "- Correctness: **" << (correctness_ok ? "PASS" : "FAIL") << "**\n";
}

int RunExport(const Options &options)
{
    const fs::path output_dir = CreateOutputDir(options);
    const fs::path database_path = output_dir / "interaction_history.sqlite";
    WriteEnvironment(output_dir, options);

    SqliteDb database(database_path);
    sqlite3 *db = database.get();
    ConfigureDatabase(db, options);
    Options seed_options = options;
    seed_options.workload = "profile";
    if(seed_options.object_count == 0) seed_options.object_count = 8;
    const std::uint64_t object_count = ObjectCount(seed_options);
    const std::vector<std::uint64_t> profile_sizes = ProfileSizes(seed_options);
    for(std::uint64_t object_index = 0; object_index < object_count; ++object_index)
    {
        const std::uint64_t object_size = profile_sizes[
            static_cast<std::size_t>(object_index % profile_sizes.size())];
        const std::string sha256 = options.hash
            ? HashObject(object_index, object_size, options) : "";
        if(options.backend == "sqlite_blob")
        {
            (void)WriteSqliteObject(db, object_index, object_size, sha256, options);
        }
        else
        {
            (void)WriteFileBackend(db, output_dir / "raw", object_index,
                object_size, sha256, options);
        }
    }
    Verify(db, output_dir, options, object_count);
    const std::vector<StoredObject> objects = LoadObjects(db);

    std::mutex error_mutex;
    std::exception_ptr first_error;
    auto record_error = [&](std::exception_ptr error) {
        std::lock_guard<std::mutex> lock(error_mutex);
        if(!first_error) first_error = error;
    };

    std::vector<Sample> writer_samples;
    std::int64_t writer_wall_time_us = 0;
    std::thread writer([&] {
        try
        {
            SqliteDb writer_db(database_path);
            sqlite3 *writer_handle = writer_db.get();
            ConfigureDatabase(writer_handle, options);
            const std::uint64_t writer_count = options.duration_seconds > 0
                && options.write_rate > 0
                ? static_cast<std::uint64_t>(options.duration_seconds)
                    * static_cast<std::uint64_t>(options.write_rate) : 30;
            Options writer_options = options;
            writer_options.workload = "mixed";
            const std::vector<std::uint64_t> writer_sizes = ProfileSizes(writer_options);
            const std::int64_t interval_us = options.write_rate > 0
                ? std::max<std::int64_t>(1, 1000000 / options.write_rate) : 0;
            const TimePoint schedule_start = Clock::now();
            const TimePoint writer_start = schedule_start;
            writer_samples.reserve(static_cast<std::size_t>(writer_count));
            for(std::uint64_t i = 0; i < writer_count; ++i)
            {
                if(interval_us > 0)
                {
                    std::this_thread::sleep_until(schedule_start
                        + std::chrono::microseconds(static_cast<std::int64_t>(i) * interval_us));
                }
                const std::uint64_t object_index = 1000000 + i;
                const std::uint64_t object_size = writer_sizes[
                    static_cast<std::size_t>(i % writer_sizes.size())];
                const TimePoint e2e_start = Clock::now();
                const std::string sha256 = options.hash
                    ? HashObject(object_index, object_size, writer_options) : "";
                const std::int64_t hash_us = MicrosSince(e2e_start);
                Sample sample;
                if(options.backend == "sqlite_blob")
                {
                    sample = WriteSqliteObject(writer_handle, object_index, object_size,
                        sha256, writer_options);
                }
                else
                {
                    sample = WriteFileBackend(writer_handle, output_dir / "raw", object_index,
                        object_size, sha256, writer_options);
                }
                sample.hash_us = hash_us;
                sample.end_to_end_us = MicrosSince(e2e_start);
                writer_samples.push_back(sample);
            }
            writer_wall_time_us = MicrosSince(writer_start);
        }
        catch(...)
        {
            record_error(std::current_exception());
        }
    });

    const std::uint64_t export_count = options.export_concurrency == 0
        ? 1 : static_cast<std::uint64_t>(options.export_concurrency);
    std::vector<ExportJobResult> jobs(export_count);
    std::vector<std::thread> exporters;
    exporters.reserve(static_cast<std::size_t>(export_count));
    for(std::uint64_t job_index = 0; job_index < export_count; ++job_index)
    {
        exporters.emplace_back([&, job_index] {
            try
            {
                SqliteDb export_job_db(database_path);
                ConfigureDatabase(export_job_db.get(), options);
                const std::int64_t export_job_id = CreateExportJob(
                    export_job_db.get(), objects);
                const fs::path zip_path = output_dir / "export"
                    / ("job_" + std::to_string(job_index) + ".zip");
                const fs::path temporary_zip = zip_path.string() + ".tmp";
                SqliteDb read_db(database_path);
                sqlite3_busy_timeout(read_db.get(), 3000);
                ZipWriter zip(temporary_zip);
                const TimePoint start = Clock::now();
                std::uint64_t bytes = 0;
                for(const StoredObject &object : objects)
                {
                    ExportObject(&zip, read_db.get(), output_dir, options, object, &bytes);
                }
                zip.Finish();
                std::error_code rename_error;
                fs::rename(temporary_zip, zip_path, rename_error);
                if(rename_error)
                {
                    Fail("publish ZIP export failed: " + rename_error.message());
                }
                CompleteExportJob(export_job_db.get(), export_job_id, zip_path, bytes);
                jobs[job_index] = {bytes, MicrosSince(start), zip_path};
            }
            catch(...)
            {
                record_error(std::current_exception());
            }
        });
    }
    for(std::thread &exporter : exporters) exporter.join();
    writer.join();
    if(first_error) std::rethrow_exception(first_error);

    WriteSamples(output_dir, writer_samples);
    WriteSummary(output_dir, options, writer_samples, true, {}, writer_wall_time_us);
    WriteReport(output_dir, options, writer_samples, true, {}, writer_wall_time_us);
    WriteExportResults(output_dir, options, jobs, true);
    std::cout << "output_dir=" << output_dir << '\n'
              << "database=" << database_path << '\n'
              << "objects=" << object_count << '\n'
              << "export_jobs=" << export_count << '\n';
    return 0;
}

struct CheckpointResult
{
    std::int64_t busy = 0;
    std::int64_t log_pages = 0;
    std::int64_t checkpointed_pages = 0;
};

CheckpointResult CheckpointTruncate(sqlite3 *db)
{
    sqlite3_stmt *stmt = nullptr;
    CheckSqlite(db, sqlite3_prepare_v2(db, "PRAGMA wal_checkpoint(TRUNCATE);",
        -1, &stmt, nullptr), "prepare WAL checkpoint");
    const int step_rc = sqlite3_step(stmt);
    if(step_rc != SQLITE_ROW)
    {
        sqlite3_finalize(stmt);
        Fail("WAL checkpoint returned no row");
    }
    CheckpointResult result;
    result.busy = sqlite3_column_int64(stmt, 0);
    result.log_pages = sqlite3_column_int64(stmt, 1);
    result.checkpointed_pages = sqlite3_column_int64(stmt, 2);
    CheckSqlite(db, sqlite3_finalize(stmt), "finalize WAL checkpoint");
    return result;
}

void DeleteOldRecords(sqlite3 *db, std::uint64_t delete_count)
{
    if(delete_count == 0) return;
    Exec(db, "BEGIN IMMEDIATE;");
    bool active = true;
    try
    {
        sqlite3_stmt *stmt = nullptr;
        CheckSqlite(db, sqlite3_prepare_v2(db,
            "DELETE FROM interaction_records WHERE id IN ("
            "SELECT id FROM interaction_records r "
            "WHERE NOT EXISTS (SELECT 1 FROM interaction_export_job_records p "
            "WHERE p.record_id=r.id) ORDER BY id LIMIT ?);",
            -1, &stmt, nullptr), "prepare retention delete");
        sqlite3_bind_int64(stmt, 1, static_cast<sqlite3_int64>(delete_count));
        const int step_rc = sqlite3_step(stmt);
        const int finalize_rc = sqlite3_finalize(stmt);
        CheckSqlite(db, step_rc, "delete expired records");
        CheckSqlite(db, finalize_rc, "finalize retention delete");
        Exec(db,
            "DELETE FROM interaction_objects WHERE id NOT IN ("
            "SELECT request_object_id FROM interaction_records WHERE request_object_id IS NOT NULL "
            "UNION "
            "SELECT response_object_id FROM interaction_records WHERE response_object_id IS NOT NULL);");
        sqlite3_stmt *audit_stmt = nullptr;
        CheckSqlite(db, sqlite3_prepare_v2(db,
            "INSERT INTO interaction_cleanup_audits(project_id,reason,actor_user_id,"
            "record_count,object_count,freed_bytes,details_json,created_at_ms) "
            "VALUES(?,?,?,?,?,?,?,?);",
            -1, &audit_stmt, nullptr), "prepare cleanup audit insert");
        sqlite3_bind_int64(audit_stmt, 1, 1);
        sqlite3_bind_int(audit_stmt, 2, 1);
        sqlite3_bind_null(audit_stmt, 3);
        sqlite3_bind_int64(audit_stmt, 4, static_cast<sqlite3_int64>(delete_count));
        sqlite3_bind_int64(audit_stmt, 5, static_cast<sqlite3_int64>(delete_count));
        sqlite3_bind_int64(audit_stmt, 6, 0);
        sqlite3_bind_text(audit_stmt, 7, R"({"benchmark":true})", -1, SQLITE_STATIC);
        sqlite3_bind_int64(audit_stmt, 8, WallClockMillis());
        const int audit_step_rc = sqlite3_step(audit_stmt);
        const int audit_finalize_rc = sqlite3_finalize(audit_stmt);
        CheckSqlite(db, audit_step_rc, "insert cleanup audit");
        CheckSqlite(db, audit_finalize_rc, "finalize cleanup audit insert");
        Exec(db, "COMMIT;");
        active = false;
    }
    catch(...)
    {
        if(active) RollbackNoThrow(db);
        throw;
    }
}

int RunCleanup(const Options &options)
{
    const fs::path output_dir = CreateOutputDir(options);
    const fs::path database_path = output_dir / "interaction_history.sqlite";
    WriteEnvironment(output_dir, options);

    SqliteDb database(database_path);
    sqlite3 *db = database.get();
    ConfigureDatabase(db, options);
    Options seed_options = options;
    seed_options.workload = "profile";
    if(seed_options.object_count == 0) seed_options.object_count = 16;
    const std::uint64_t seed_count = ObjectCount(seed_options);
    const std::vector<std::uint64_t> sizes = ProfileSizes(seed_options);
    for(std::uint64_t i = 0; i < seed_count; ++i)
    {
        const std::uint64_t size = sizes[static_cast<std::size_t>(i % sizes.size())];
        const std::string sha256 = options.hash ? HashObject(i, size, options) : "";
        if(options.backend == "sqlite_blob")
        {
            (void)WriteSqliteObject(db, i, size, sha256, options);
        }
        else
        {
            (void)WriteFileBackend(db, output_dir / "raw", i, size, sha256, options);
        }
    }
    Verify(db, output_dir, options, seed_count);
    const std::vector<StoredObject> objects_before_cleanup = LoadObjects(db);
    const std::uint64_t keep_count = std::min(options.retention_count, seed_count);
    const std::uint64_t delete_count = seed_count - keep_count;
    const std::uint64_t before_db_bytes = FileBytes(database_path);
    const std::uint64_t before_wal_bytes = FileBytes(database_path.string() + "-wal");
    const std::uint64_t before_raw_file_bytes = DirectoryBytes(output_dir / "raw");
    const std::int64_t before_freelist = QueryInt64(db, "PRAGMA freelist_count;");
    const TimePoint cleanup_start = Clock::now();
    DeleteOldRecords(db, delete_count);
    const std::int64_t cleanup_us = MicrosSince(cleanup_start);
    if(options.backend == "local_file")
    {
        for(std::uint64_t i = 0; i < delete_count; ++i)
        {
            const fs::path path = output_dir / objects_before_cleanup[
                static_cast<std::size_t>(i)].object_key;
            std::error_code error;
            if(!fs::remove(path, error) || error)
            {
                Fail("remove expired raw file failed: " + path.string() + ": " + error.message());
            }
        }
    }
    const CheckpointResult checkpoint = CheckpointTruncate(db);
    const std::uint64_t after_db_bytes = FileBytes(database_path);
    const std::uint64_t after_wal_bytes = FileBytes(database_path.string() + "-wal");
    const std::uint64_t after_raw_file_bytes = DirectoryBytes(output_dir / "raw");
    const std::int64_t after_freelist = QueryInt64(db, "PRAGMA freelist_count;");

    if(QueryInt64(db, "SELECT COUNT(*) FROM interaction_records;") != static_cast<std::int64_t>(keep_count)
        || QueryInt64(db, "SELECT COUNT(*) FROM interaction_objects;") != static_cast<std::int64_t>(keep_count)
        || QueryInt64(db, "SELECT COUNT(*) FROM interaction_payload_refs;")
            != static_cast<std::int64_t>(keep_count * 2))
    {
        Fail("retention delete left unexpected metadata counts");
    }
    Verify(db, output_dir, options, keep_count);

    const std::uint64_t new_index = seed_count + 1000;
    const std::uint64_t new_size = 64 * kKiB;
    const std::string new_hash = options.hash ? HashObject(new_index, new_size, options) : "";
    if(options.backend == "sqlite_blob")
    {
        (void)WriteSqliteObject(db, new_index, new_size, new_hash, options);
    }
    else
    {
        (void)WriteFileBackend(db, output_dir / "raw", new_index, new_size, new_hash, options);
    }
    Verify(db, output_dir, options, keep_count + 1);

    std::ofstream out(output_dir / "cleanup_summary.json");
    if(!out) Fail("open cleanup_summary.json failed");
    out << "{\n"
        << "  \"backend\": \"" << JsonEscape(options.backend) << "\",\n"
        << "  \"seed_count\": " << seed_count << ",\n"
        << "  \"retention_count\": " << keep_count << ",\n"
        << "  \"deleted_count\": " << delete_count << ",\n"
        << "  \"cleanup_us\": " << cleanup_us << ",\n"
        << "  \"before_database_bytes\": " << before_db_bytes << ",\n"
        << "  \"before_wal_bytes\": " << before_wal_bytes << ",\n"
        << "  \"before_raw_file_bytes\": " << before_raw_file_bytes << ",\n"
        << "  \"after_database_bytes\": " << after_db_bytes << ",\n"
        << "  \"after_wal_bytes\": " << after_wal_bytes << ",\n"
        << "  \"after_raw_file_bytes\": " << after_raw_file_bytes << ",\n"
        << "  \"before_freelist_pages\": " << before_freelist << ",\n"
        << "  \"after_freelist_pages\": " << after_freelist << ",\n"
        << "  \"checkpoint_busy\": " << checkpoint.busy << ",\n"
        << "  \"checkpoint_log_pages\": " << checkpoint.log_pages << ",\n"
        << "  \"checkpointed_pages\": " << checkpoint.checkpointed_pages << ",\n"
        << "  \"correctness_ok\": true\n"
        << "}\n";
    std::cout << "output_dir=" << output_dir << '\n'
              << "database=" << database_path << '\n'
              << "seed_objects=" << seed_count << '\n'
              << "deleted_objects=" << delete_count << '\n';
    return 0;
}

void SeedCrashObjects(sqlite3 *db,
                      const fs::path &output_dir,
                      const Options &options,
                      std::uint64_t count)
{
    const std::vector<std::uint64_t> sizes = {1 * kKiB, 16 * kKiB, 64 * kKiB, 256 * kKiB};
    for(std::uint64_t i = 0; i < count; ++i)
    {
        const std::uint64_t size = sizes[static_cast<std::size_t>(i % sizes.size())];
        const std::string sha256 = options.hash ? HashObject(i, size, options) : "";
        if(options.backend == "sqlite_blob")
        {
            (void)WriteSqliteObject(db, i, size, sha256, options);
        }
        else
        {
            (void)WriteFileBackend(db, output_dir / "raw", i, size, sha256, options);
        }
    }
}

[[noreturn]] void CrashChild(const fs::path &database_path,
                             const fs::path &output_dir,
                             const Options &options)
{
    SqliteDb database(database_path);
    sqlite3 *db = database.get();
    ConfigureDatabase(db, options);
    if(options.fault_point == "cleanup")
    {
        SeedCrashObjects(db, output_dir, options, 4);
        Exec(db, "BEGIN IMMEDIATE;");
        sqlite3_stmt *stmt = nullptr;
        CheckSqlite(db, sqlite3_prepare_v2(db,
            "DELETE FROM interaction_records WHERE id IN ("
            "SELECT id FROM interaction_records ORDER BY id LIMIT 2);",
            -1, &stmt, nullptr), "prepare crash cleanup");
        const int step_rc = sqlite3_step(stmt);
        const int finalize_rc = sqlite3_finalize(stmt);
        CheckSqlite(db, step_rc, "crash cleanup delete");
        CheckSqlite(db, finalize_rc, "finalize crash cleanup");
        ::_exit(102);
    }
    if(options.fault_point == "export")
    {
        SeedCrashObjects(db, output_dir, options, 2);
        const std::vector<StoredObject> objects = LoadObjects(db);
        const fs::path temporary_zip = output_dir / "export" / "crash.zip.tmp";
        SqliteDb read_db(database_path);
        sqlite3_busy_timeout(read_db.get(), 3000);
        ZipWriter zip(temporary_zip);
        zip.BeginEntry("object_0.bin");
        const std::uint64_t length = std::min<std::uint64_t>(
            options.blob_chunk_size, objects.front().object_size);
        const std::vector<unsigned char> bytes = ReadRawSlice(
            read_db.get(), output_dir, options, objects.front(), 0, length);
        VerifySlice(objects.front(), 0, bytes, options);
        zip.Write(bytes.data(), bytes.size());
        ::_exit(103);
    }

    SeedCrashObjects(db, output_dir, options, 1);
    const std::uint64_t object_index = 1;
    const std::uint64_t object_size = 256 * kKiB;
    const std::string sha256 = options.hash ? HashObject(object_index, object_size, options) : "";
    if(options.backend == "sqlite_blob")
    {
        Exec(db, "BEGIN IMMEDIATE;");
        const std::int64_t object_id = InsertBlobObject(
            db, object_index, object_size, sha256);
        SqliteBlob blob;
        CheckSqlite(db, sqlite3_blob_open(db, "main", "interaction_objects", "data",
            object_id, 1, blob.out()), "open crash blob");
        std::vector<unsigned char> chunk(static_cast<std::size_t>(
            std::min(options.blob_chunk_size, object_size)));
        FillPattern(&chunk, object_index, 0, options.seed);
        CheckSqlite(db, sqlite3_blob_write(blob.get(), chunk.data(),
            static_cast<int>(chunk.size()), 0), "write crash blob");
        if(options.fault_point == "blob-write")
        {
            ::_exit(101);
        }
        for(std::uint64_t offset = chunk.size(); offset < object_size;)
        {
            const std::size_t size = static_cast<std::size_t>(
                std::min<std::uint64_t>(chunk.size(), object_size - offset));
            chunk.resize(size);
            FillPattern(&chunk, object_index, offset, options.seed);
            CheckSqlite(db, sqlite3_blob_write(blob.get(), chunk.data(),
                static_cast<int>(chunk.size()), static_cast<int>(offset)),
                "write crash blob remainder");
            offset += size;
        }
        CheckSqlite(db, blob.Close(), "close crash blob");
        MarkBlobReady(db, object_id);
        const std::int64_t record_id = InsertRecord(db, object_id);
        InsertPayloads(db, record_id, object_id, object_size);
        if(options.fault_point == "commit")
        {
            ::_exit(101);
        }
    }
    else
    {
        if(options.fault_point == "blob-write")
        {
            (void)WriteFileObject(output_dir / "raw", object_index, object_size, options);
            ::_exit(101);
        }
        const fs::path path = WriteFileObject(output_dir / "raw", object_index, object_size, options);
        Exec(db, "BEGIN IMMEDIATE;");
        const std::int64_t object_id = InsertFileObject(db, object_index,
            object_size, sha256, "raw/" + path.filename().string());
        const std::int64_t record_id = InsertRecord(db, object_id);
        InsertPayloads(db, record_id, object_id, object_size);
        if(options.fault_point == "commit")
        {
            ::_exit(101);
        }
    }
    ::_exit(0);
}

std::uint64_t RemoveOrphanRawFiles(const fs::path &output_dir,
                                   sqlite3 *db)
{
    const std::vector<StoredObject> objects = LoadObjects(db);
    std::vector<std::string> referenced;
    referenced.reserve(objects.size());
    for(const StoredObject &object : objects)
    {
        referenced.push_back(object.object_key);
    }
    std::uint64_t removed = 0;
    std::error_code error;
    for(const fs::directory_entry &entry : fs::directory_iterator(output_dir / "raw", error))
    {
        if(error) break;
        if(!entry.is_regular_file(error)) continue;
        const std::string key = "raw/" + entry.path().filename().string();
        if(std::find(referenced.begin(), referenced.end(), key) == referenced.end())
        {
            if(fs::remove(entry.path(), error)) ++removed;
            if(error) Fail("remove crash orphan failed: " + error.message());
        }
    }
    return removed;
}

int RunCrash(const Options &options)
{
    const fs::path output_dir = CreateOutputDir(options);
    const fs::path database_path = output_dir / "interaction_history.sqlite";
    WriteEnvironment(output_dir, options);
    const pid_t child = ::fork();
    if(child < 0) Fail("fork crash workload failed: " + std::string(std::strerror(errno)));
    if(child == 0)
    {
        try
        {
            CrashChild(database_path, output_dir, options);
        }
        catch(...)
        {
            ::_exit(120);
        }
    }
    int status = 0;
    if(::waitpid(child, &status, 0) < 0)
    {
        Fail("waitpid crash workload failed: " + std::string(std::strerror(errno)));
    }
    const bool crashed = WIFEXITED(status) && WEXITSTATUS(status) != 0;
    if(!crashed)
    {
        Fail("crash child did not terminate at the injected fault point");
    }

    SqliteDb database(database_path);
    sqlite3 *db = database.get();
    ConfigureDatabase(db, options);
    const std::uint64_t expected_count = options.fault_point == "cleanup" ? 4 :
        options.fault_point == "export" ? 2 : 1;
    std::uint64_t orphan_files_removed = 0;
    if(options.backend == "local_file")
    {
        orphan_files_removed = RemoveOrphanRawFiles(output_dir, db);
    }
    Verify(db, output_dir, options, expected_count);
    if(options.fault_point == "export")
    {
        const fs::path published = output_dir / "export" / "crash.zip";
        const fs::path temporary = output_dir / "export" / "crash.zip.tmp";
        if(fs::exists(published))
        {
            Fail("crashed export published a partial ZIP");
        }
        std::error_code error;
        fs::remove(temporary, error);
        if(error) Fail("remove crashed export temporary file failed: " + error.message());
    }

    std::ofstream out(output_dir / "crash_summary.json");
    if(!out) Fail("open crash_summary.json failed");
    out << "{\n"
        << "  \"backend\": \"" << JsonEscape(options.backend) << "\",\n"
        << "  \"fault_point\": \"" << JsonEscape(options.fault_point) << "\",\n"
        << "  \"child_exit_status\": " << status << ",\n"
        << "  \"recovered_ready_objects\": " << expected_count << ",\n"
        << "  \"orphan_files_removed\": " << orphan_files_removed << ",\n"
        << "  \"published_partial_export\": false,\n"
        << "  \"correctness_ok\": true\n"
        << "}\n";
    std::cout << "output_dir=" << output_dir << '\n'
              << "database=" << database_path << '\n'
              << "fault_point=" << options.fault_point << '\n'
              << "recovered_ready_objects=" << expected_count << '\n';
    return 0;
}

int RunMixed(const Options &options)
{
    const fs::path output_dir = CreateOutputDir(options);
    const fs::path database_path = output_dir / "interaction_history.sqlite";
    WriteEnvironment(output_dir, options);

    SqliteDb database(database_path);
    sqlite3 *db = database.get();
    ConfigureDatabase(db, options);

    const std::vector<std::uint64_t> profile_sizes = ProfileSizes(options);
    const std::uint64_t object_count = ObjectCount(options);
    const std::int64_t interval_us = options.write_rate > 0
        ? std::max<std::int64_t>(1, 1000000 / options.write_rate) : 0;
    BoundedQueue queue(options.queue_capacity);
    std::vector<Sample> samples;
    samples.reserve(static_cast<std::size_t>(object_count));
    std::atomic<std::uint64_t> next_index{0};
    std::atomic<bool> stop{false};
    std::mutex error_mutex;
    std::exception_ptr first_error;
    const TimePoint run_start = Clock::now();
    const TimePoint schedule_start = run_start;

    auto record_error = [&](std::exception_ptr error) {
        {
            std::lock_guard<std::mutex> lock(error_mutex);
            if(!first_error)
            {
                first_error = error;
            }
        }
        stop.store(true);
        queue.Close();
    };

    std::thread writer([&] {
        try
        {
            WorkItem item;
            while(queue.Pop(&item))
            {
                Sample sample = options.backend == "sqlite_blob"
                    ? WriteSqliteObject(db, item.object_index, item.object_size,
                        item.sha256, options)
                    : WriteFileBackend(db, output_dir / "raw", item.object_index,
                        item.object_size, item.sha256, options);
                sample.hash_us = item.hash_us;
                sample.end_to_end_us = MicrosSince(item.enqueued_at);
                samples.push_back(sample);
            }
        }
        catch(...)
        {
            record_error(std::current_exception());
        }
    });

    std::vector<std::thread> producers;
    producers.reserve(static_cast<std::size_t>(options.producer_count));
    for(std::uint64_t producer_index = 0;
        producer_index < options.producer_count; ++producer_index)
    {
        producers.emplace_back([&, producer_index] {
            (void)producer_index;
            try
            {
                while(!stop.load())
                {
                    const std::uint64_t object_index = next_index.fetch_add(1);
                    if(object_index >= object_count)
                    {
                        break;
                    }
                    if(interval_us > 0)
                    {
                        const auto offset = std::chrono::microseconds(
                            static_cast<std::int64_t>(object_index) * interval_us);
                        std::this_thread::sleep_until(schedule_start + offset);
                    }
                    const std::uint64_t object_size = profile_sizes[
                        static_cast<std::size_t>(object_index % profile_sizes.size())];
                    const TimePoint hash_start = Clock::now();
                    const std::string sha256 = options.hash
                        ? HashObject(object_index, object_size, options) : "";
                    WorkItem item;
                    item.object_index = object_index;
                    item.object_size = object_size;
                    item.sha256 = sha256;
                    item.hash_us = MicrosSince(hash_start);
                    item.enqueued_at = Clock::now();
                    if(!queue.Push(std::move(item)))
                    {
                        break;
                    }
                }
            }
            catch(...)
            {
                record_error(std::current_exception());
            }
        });
    }

    for(std::thread &producer : producers)
    {
        producer.join();
    }
    queue.Close();
    writer.join();
    const std::int64_t write_wall_time_us = MicrosSince(run_start);

    if(first_error)
    {
        std::rethrow_exception(first_error);
    }
    if(samples.size() != object_count)
    {
        Fail("mixed workload lost objects: expected " + std::to_string(object_count)
            + ", actual " + std::to_string(samples.size()));
    }

    Verify(db, output_dir, options, object_count);
    const QueueStats queue_stats = queue.Stats(options.producer_count);
    WriteSamples(output_dir, samples);
    WriteSummary(output_dir, options, samples, true, queue_stats, write_wall_time_us);
    WriteReport(output_dir, options, samples, true, queue_stats, write_wall_time_us);
    std::cout << "output_dir=" << output_dir << '\n'
              << "database=" << database_path << '\n'
              << "objects=" << samples.size() << '\n'
              << "queue_peak=" << queue_stats.peak << '\n';
    return 0;
}

int Run(const Options &options)
{
    if(options.workload == "mixed")
    {
        return RunMixed(options);
    }
    if(options.workload == "read")
    {
        return RunRead(options);
    }
    if(options.workload == "export")
    {
        return RunExport(options);
    }
    if(options.workload == "cleanup")
    {
        return RunCleanup(options);
    }
    if(options.workload == "crash")
    {
        return RunCrash(options);
    }
    const fs::path output_dir = CreateOutputDir(options);
    const fs::path database_path = output_dir / "interaction_history.sqlite";
    WriteEnvironment(output_dir, options);

    SqliteDb database(database_path);
    sqlite3 *db = database.get();
    ConfigureDatabase(db, options);

    const std::vector<std::uint64_t> profile_sizes = ProfileSizes(options);
    const std::uint64_t object_count = ObjectCount(options);
    std::vector<Sample> samples;
    samples.reserve(static_cast<std::size_t>(object_count));
    const auto interval = options.write_rate > 0
        ? std::chrono::microseconds(std::max<std::int64_t>(1, 1000000 / options.write_rate))
        : std::chrono::microseconds(0);
    TimePoint next_write = Clock::now();
    const TimePoint run_start = next_write;

    for(std::uint64_t object_index = 0; object_index < object_count; ++object_index)
    {
        if(options.write_rate > 0 && object_index > 0)
        {
            next_write += interval;
            std::this_thread::sleep_until(next_write);
        }
        const std::uint64_t object_size = profile_sizes[
            static_cast<std::size_t>(object_index % profile_sizes.size())];
        const TimePoint e2e_start = Clock::now();
        const TimePoint hash_start = Clock::now();
        const std::string sha256 = options.hash
            ? HashObject(object_index, object_size, options) : "";
        const std::int64_t hash_us = MicrosSince(hash_start);
        Sample sample = options.backend == "sqlite_blob"
            ? WriteSqliteObject(db, object_index, object_size, sha256, options)
            : WriteFileBackend(db, output_dir / "raw", object_index, object_size, sha256, options);
        sample.hash_us = hash_us;
        sample.end_to_end_us = MicrosSince(e2e_start);
        samples.push_back(sample);
    }

    const std::int64_t write_wall_time_us = MicrosSince(run_start);
    Verify(db, output_dir, options, object_count);
    WriteSamples(output_dir, samples);
    WriteSummary(output_dir, options, samples, true, {}, write_wall_time_us);
    WriteReport(output_dir, options, samples, true, {}, write_wall_time_us);
    std::cout << "output_dir=" << output_dir << '\n'
              << "database=" << database_path << '\n'
              << "objects=" << samples.size() << '\n';
    return 0;
}

}   // namespace

int main(int argc, char **argv)
{
    try
    {
        const Options options = ParseArgs(argc, argv);
        if(options.show_help)
        {
            PrintHelp();
            return 0;
        }
        return Run(options);
    }
    catch(const std::exception &error)
    {
        std::cerr << "benchmark failed: " << error.what() << '\n';
        return 1;
    }
}
