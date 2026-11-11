#ifndef KIT_TESTS_TLS_TEST_SUPPORT_H_
#define KIT_TESTS_TLS_TEST_SUPPORT_H_

#include "net/tls_context.h"

#include <arpa/inet.h>
#include <filesystem>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <string>
#include <sys/socket.h>
#include <unistd.h>
#include <openssl/pem.h>
#include <openssl/x509v3.h>

namespace kit_test {

/** @brief 每组测试独立生成 CA/服务端证书，私钥和文件只存在于 /tmp。 */
class TestCertificates
{
public:
    TestCertificates()
    {
        char pattern[] = "/tmp/kit-https-test-XXXXXX";
        const char* directory = ::mkdtemp(pattern);
        if(!directory) throw std::runtime_error("mkdtemp failed");
        directory_ = directory;
        auto ca_key = MakeKey();
        auto key = MakeKey();
        auto ca_cert = MakeCert(ca_key.get(), nullptr, nullptr, 1);
        auto cert = MakeCert(key.get(), ca_cert.get(), ca_key.get(), 2);
        WriteKey(caKeyFile(), ca_key.get());
        WriteKey(keyFile(), key.get());
        WriteCert(caFile(), ca_cert.get());
        WriteCert(certFile(), cert.get());
        std::unique_ptr<FILE, decltype(&fclose)> chain(std::fopen(certFile().c_str(), "a"), fclose);
        Check(chain && PEM_write_X509(chain.get(), ca_cert.get()) == 1);
    }

    ~TestCertificates()
    {
        std::error_code error;
        std::filesystem::remove_all(directory_, error);
    }

    std::string caFile() const { return directory_ + "/ca.pem"; }
    std::string caKeyFile() const { return directory_ + "/ca-key.pem"; }
    std::string certFile() const { return directory_ + "/server.pem"; }
    std::string keyFile() const { return directory_ + "/server-key.pem"; }
    kit_muduo::TlsServerConfig config() const { return {certFile(), keyFile()}; }

private:
    static void Check(bool ok)
    {
        if(!ok) throw std::runtime_error("test certificate generation failed");
    }

    static std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)> MakeKey()
    {
        std::unique_ptr<EVP_PKEY_CTX, decltype(&EVP_PKEY_CTX_free)> ctx(
            EVP_PKEY_CTX_new_id(EVP_PKEY_RSA, nullptr), EVP_PKEY_CTX_free);
        Check(ctx && EVP_PKEY_keygen_init(ctx.get()) == 1 &&
            EVP_PKEY_CTX_set_rsa_keygen_bits(ctx.get(), 2048) == 1);
        EVP_PKEY* key = nullptr;
        Check(EVP_PKEY_keygen(ctx.get(), &key) == 1);
        return {key, EVP_PKEY_free};
    }

    static std::unique_ptr<X509, decltype(&X509_free)> MakeCert(
        EVP_PKEY* key, X509* ca, EVP_PKEY* ca_key, long serial)
    {
        std::unique_ptr<X509, decltype(&X509_free)> cert(X509_new(), X509_free);
        Check(cert && X509_set_version(cert.get(), 2) == 1 &&
            ASN1_INTEGER_set(X509_get_serialNumber(cert.get()), serial) == 1 &&
            X509_gmtime_adj(X509_getm_notBefore(cert.get()), -60) &&
            X509_gmtime_adj(X509_getm_notAfter(cert.get()), 86400) &&
            X509_set_pubkey(cert.get(), key) == 1);
        X509_NAME* name = X509_get_subject_name(cert.get());
        Check(X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC,
            reinterpret_cast<const unsigned char*>(ca ? "localhost" : "Kit HTTPS Test CA"), -1, -1, 0) == 1);
        Check(X509_set_issuer_name(cert.get(), ca ? X509_get_subject_name(ca) : name) == 1);
        X509V3_CTX ext_ctx;
        X509V3_set_ctx(&ext_ctx, ca ? ca : cert.get(), cert.get(), nullptr, nullptr, 0);
        auto add_ext = [&](int nid, const char* value) {
            std::unique_ptr<X509_EXTENSION, decltype(&X509_EXTENSION_free)> ext(
                X509V3_EXT_conf_nid(nullptr, &ext_ctx, nid, value), X509_EXTENSION_free);
            Check(ext && X509_add_ext(cert.get(), ext.get(), -1) == 1);
        };
        add_ext(NID_basic_constraints, ca ? "critical,CA:FALSE" : "critical,CA:TRUE");
        add_ext(NID_key_usage, ca ? "critical,digitalSignature,keyEncipherment" : "critical,keyCertSign,cRLSign");
        if(ca)
        {
            add_ext(NID_ext_key_usage, "serverAuth");
            add_ext(NID_subject_alt_name, "DNS:localhost,IP:127.0.0.1");
        }
        Check(X509_sign(cert.get(), ca_key ? ca_key : key, EVP_sha256()) > 0);
        return cert;
    }

    static void WriteKey(const std::string& path, EVP_PKEY* key)
    {
        std::unique_ptr<FILE, decltype(&fclose)> file(std::fopen(path.c_str(), "w"), fclose);
        Check(file && PEM_write_PrivateKey(file.get(), key, nullptr, nullptr, 0, nullptr, nullptr) == 1);
    }

    static void WriteCert(const std::string& path, X509* cert)
    {
        std::unique_ptr<FILE, decltype(&fclose)> file(std::fopen(path.c_str(), "w"), fclose);
        Check(file && PEM_write_X509(file.get(), cert) == 1);
    }

    std::string directory_;
};

inline int ConnectLoopback(uint16_t port, int timeout_seconds = 3)
{
    int fd = ::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if(fd < 0) throw std::runtime_error("socket failed");
    timeval timeout{timeout_seconds, 0};
    ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    ::setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if(::connect(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0)
    {
        ::close(fd);
        throw std::runtime_error("connect failed");
    }
    return fd;
}

class TlsClient
{
public:
    TlsClient(uint16_t port, const std::string& ca_file, const char* host = "localhost",
        int version = 0, const std::string& alpn = "http/1.1")
        :ctx_(SSL_CTX_new(TLS_client_method()), SSL_CTX_free)
        ,ssl_(nullptr, SSL_free)
    {
        SSL_CTX_set_verify(ctx_.get(), SSL_VERIFY_PEER, nullptr);
        if(!ca_file.empty()) SSL_CTX_load_verify_locations(ctx_.get(), ca_file.c_str(), nullptr);
        if(version)
        {
            SSL_CTX_set_min_proto_version(ctx_.get(), version);
            SSL_CTX_set_max_proto_version(ctx_.get(), version);
        }
        fd_ = ConnectLoopback(port);
        ssl_.reset(SSL_new(ctx_.get()));
        SSL_set_fd(ssl_.get(), fd_);
        SSL_set_tlsext_host_name(ssl_.get(), host);
        SSL_set1_host(ssl_.get(), host);
        if(!alpn.empty())
        {
            const std::string wire = std::string(1, static_cast<char>(alpn.size())) + alpn;
            SSL_set_alpn_protos(ssl_.get(), reinterpret_cast<const unsigned char*>(wire.data()), wire.size());
        }
    }
    ~TlsClient() { if(fd_ >= 0) ::close(fd_); }
    bool connect() { return SSL_connect(ssl_.get()) == 1; }
    SSL* ssl() const { return ssl_.get(); }
    int fd() const { return fd_; }
    bool write(const std::string& text)
    {
        size_t offset = 0;
        while(offset < text.size())
        {
            const int n = SSL_write(ssl_.get(), text.data() + offset,
                std::min<size_t>(16384, text.size() - offset));
            if(n <= 0) return false;
            offset += n;
        }
        return true;
    }
    std::string read(size_t size)
    {
        std::string result;
        char buffer[16384];
        while(result.size() < size)
        {
            const int n = SSL_read(ssl_.get(), buffer, std::min(size - result.size(), sizeof(buffer)));
            if(n <= 0) break;
            result.append(buffer, n);
        }
        return result;
    }

private:
    std::unique_ptr<SSL_CTX, decltype(&SSL_CTX_free)> ctx_;
    kit_muduo::TlsContext::SslPtr ssl_;
    int fd_{-1};
};

} // namespace kit_test
#endif
