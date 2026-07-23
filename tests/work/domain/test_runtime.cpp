/**
 * @file test_runtime.cpp
 * @brief 运行态事件循环池与运行态服务行为测试
 * @author Kewin Li
 * @version 1.0
 * @date 2026-05-26 02:33:32
 * @copyright Copyright (c) 2026 Kewin Li
 */
#include "gtest/gtest.h"
#include "../../test_log.h"
#include "domain/runtime_loop_pool.h"
#include "domain/runtime_result.h"
#include "domain/http_project_server.h"
#include "domain/project_server.h"
#include "domain/protocol_interaction_hub.h"
#include "domain/protocol_interaction_publisher.h"
#include "domain/protocol.h"
#include "domain/protocol_item.h"
#include "domain/http_protocol_item.h"
#include "net/http/http_request.h"
#include "net/http/multiform.h"
#include "base/time_stamp.h"

#include <arpa/inet.h>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cerrno>
#include <cstring>
#include <memory>
#include <mutex>
#include <netinet/in.h>
#include <stdexcept>
#include <string>
#include <sys/socket.h>
#include <sys/time.h>
#include <thread>
#include <unistd.h>
#include <vector>

using namespace kit_domain;
using namespace kit_muduo::http;

struct RuntimeTestFdGuard
{
    explicit RuntimeTestFdGuard(int32_t input_fd = -1)
        :fd(input_fd)
    {}

    ~RuntimeTestFdGuard()
    {
        if(fd >= 0)
        {
            ::close(fd);
        }
    }

    int32_t fd;
};

static int32_t ConnectLoopback(uint16_t port)
{
    for(int32_t i = 0; i < 50; ++i)
    {
        int32_t fd = ::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
        if(fd < 0)
        {
            return -1;
        }

        timeval timeout;
        timeout.tv_sec = 2;
        timeout.tv_usec = 0;
        ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
        ::setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));

        sockaddr_in addr;
        std::memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_port = ::htons(port);
        addr.sin_addr.s_addr = ::htonl(INADDR_LOOPBACK);

        if(::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0)
        {
            return fd;
        }

        ::close(fd);
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }

    return -1;
}

static bool SendAll(int32_t fd, const std::string &data)
{
    const char *cur = data.data();
    size_t left = data.size();
    while(left > 0)
    {
        ssize_t n = ::send(fd, cur, left, 0);
        if(n < 0)
        {
            if(errno == EINTR)
            {
                continue;
            }
            return false;
        }

        cur += n;
        left -= static_cast<size_t>(n);
    }

    return true;
}

static std::string ReadAll(int32_t fd)
{
    std::string data;
    char buf[4096];
    while(true)
    {
        ssize_t n = ::recv(fd, buf, sizeof(buf), 0);
        if(n > 0)
        {
            data.append(buf, static_cast<size_t>(n));
            continue;
        }
        if(n == 0)
        {
            break;
        }
        if(errno == EINTR)
        {
            continue;
        }
        break;
    }

    return data;
}

static std::vector<char> Chars(const std::string &text)
{
    return std::vector<char>(text.begin(), text.end());
}

static kit_muduo::InetAddress RuntimeServerAddress()
{
    return kit_muduo::InetAddress(0, "127.0.0.1");
}

class RuntimeInteractionCollector
{
public:
    void OnRecord(const InteractionRecord &record)
    {
        {
            std::lock_guard<std::mutex> lock(mtx_);
            records_.push_back(record);
        }
        cv_.notify_all();
    }

    bool WaitForRecordCount(size_t expected_count,
                            std::chrono::milliseconds timeout = std::chrono::milliseconds(3000))
    {
        std::unique_lock<std::mutex> lock(mtx_);
        return cv_.wait_for(lock, timeout, [this, expected_count]() {
            return records_.size() >= expected_count;
        });
    }

    std::vector<InteractionRecord> Records() const
    {
        std::lock_guard<std::mutex> lock(mtx_);
        return records_;
    }

private:
    mutable std::mutex mtx_;
    std::condition_variable cv_;
    std::vector<InteractionRecord> records_;
};

struct RuntimeInteractionPipeline
{
    RuntimeInteractionPipeline(int64_t project_id,
                               int64_t protocol_id,
                               bool include_project_notice = true)
        : hub(std::make_shared<ProtocolInteractionHub>())
        , collector(std::make_shared<RuntimeInteractionCollector>())
        , publisher(
            std::vector<std::shared_ptr<InteractionSink>>{
                std::static_pointer_cast<InteractionSink>(hub),
            },
            ProtocolInteractionPublisherConfig{
                .queue_capacity = 16,
                .stop_drain_timeout = 1000,
                .capture_options = InteractionCaptureOptions{},
            })
    {
        (void)project_id;
        (void)protocol_id;
        (void)include_project_notice;
        publisher.start();
    }

    void Subscribe(
        int64_t project_id,
        int64_t protocol_id,
        std::shared_ptr<InteractionRecordCache> protocol_cache,
        std::shared_ptr<InteractionRecordCache> project_cache,
        bool include_project_notice)
    {
        protocol_cache_ = std::move(protocol_cache);
        project_cache_ = std::move(project_cache);
        ASSERT_NE(protocol_cache_, nullptr);
        ASSERT_NE(project_cache_, nullptr);
        subscription = hub->subscribeWithCatchUp(
            InteractionSubscribeFilter{
                .project_id = project_id,
                .protocol_id = protocol_id,
                .include_project_notice = include_project_notice,
            },
            InteractionRecordCacheContainer{
                protocol_cache_, project_cache_, std::nullopt, std::nullopt,
                std::nullopt, std::nullopt},
            [collector = collector](const InteractionRecord &record) {
                collector->OnRecord(record);
            });
        ASSERT_TRUE(subscription.ok());
    }

    ~RuntimeInteractionPipeline()
    {
        publisher.stop();
        if(subscription.subscription.subscriber_id > 0)
        {
            hub->unsubcribe(subscription.subscription.subscriber_id);
        }
    }

    ProjectServer::ObserveCallback Callback()
    {
        return [this](ProtocolInteractionObservation obs) {
            publisher.publish(std::move(obs));
        };
    }

    std::shared_ptr<ProtocolInteractionHub> hub;
    std::shared_ptr<RuntimeInteractionCollector> collector;
    ProtocolInteractionPublisher publisher;
    SubscribeWithCatchUpResult subscription;
    std::shared_ptr<InteractionRecordCache> protocol_cache_;
    std::shared_ptr<InteractionRecordCache> project_cache_;
};

static nljson HttpReqCfg(const std::string &method,
                         const std::string &path,
                         const nljson &headers)
{
    return nljson{
        {"method", method},
        {"path", path},
        {"headers", headers},
    };
}

static nljson HttpRespCfg(const std::string &status_code,
                          const nljson &headers)
{
    return nljson{
        {"status_code", status_code},
        {"headers", headers},
    };
}

static std::shared_ptr<Protocol> MakeHttpProtocol(
        int64_t protocol_id,
        int64_t project_id,
        const std::string &path,
        const std::vector<char> &req_body = {},
        const std::vector<char> &resp_body = {'o', 'k'},
        ProtocolBodyType req_body_type = ProtocolBodyType::kJson,
        ProtocolBodyType resp_body_type = ProtocolBodyType::kJson)
{
    auto protocol = std::make_shared<Protocol>();
    protocol->m_id = protocol_id;
    protocol->m_name = "http_runtime_pc_" + std::to_string(protocol_id);
    protocol->m_type = ProtocolType::kHttp;
    protocol->m_projectId = project_id;
    protocol->m_status = ProtocolStatus::kValid;
    protocol->m_reqBodyType = req_body_type;
    protocol->m_respBodyType = resp_body_type;
    protocol->m_reqBodyDataStatus = req_body.empty() ? 0 : 1;
    protocol->m_respBodyDataStatus = resp_body.empty() ? 0 : 1;
    protocol->m_reqCfg = HttpReqCfg("GET", path, nljson{{"X-Old", "1"}});
    protocol->m_respCfg = HttpRespCfg("200", nljson{{"Content-Type", "application/json"}});
    protocol->m_reqBodyData = req_body;
    protocol->m_respBodyData = resp_body;
    protocol->m_isEndian = true;
    protocol->m_ctime = kit_muduo::TimeStamp::Now();
    protocol->m_utime = kit_muduo::TimeStamp::Now();
    return protocol;
}

static std::shared_ptr<HttpProtocolItem> GetHttpRuntimeItem(
        const std::shared_ptr<HttpProjectServer> &server,
        int64_t protocol_id)
{
    auto result = server->GetProtocolItem(protocol_id);
    if(!result.ok())
    {
        return nullptr;
    }
    return std::dynamic_pointer_cast<HttpProtocolItem>(result.val);
}

static std::string BuildHttpRequest(const std::string &method,
                                    const std::string &path,
                                    const std::string &body = {},
                                    const std::string &content_type = {})
{
    std::string request = method + " " + path + " HTTP/1.1\r\n"
        "Host: 127.0.0.1\r\n"
        "Connection: close\r\n";

    if(!content_type.empty())
    {
        request += "Content-Type: " + content_type + "\r\n";
    }
    if(!body.empty())
    {
        request += "Content-Length: " + std::to_string(body.size()) + "\r\n";
    }

    request += "\r\n";
    request += body;
    return request;
}

/*
测试思路：
构造 1 个 slot 的 pool。
acquire 一个 lease。
验证 lease 非空、loop 非空、activeCount 为 1。
lease reset 后 activeCount 回到 0。
再次 acquire 成功，说明 slot 已归还。

示例：
  slot[0]: 0(free) --acquire uid=1--> token=1(busy)
  slot[0]: token=1 --release--> 2(free) --acquire uid=2--> token=3(busy)
*/
TEST(TestRuntimeLoopPool, AcquireReleaseBasic)
{
    RuntimeLoopPool pool(1);
    auto result = pool.acquire(1LL);
    ASSERT_TRUE(result.ok());
    ASSERT_NE(result.val, nullptr);
    ASSERT_NE(result.val->loop(), nullptr);
    ASSERT_EQ(pool.activeCount(), 1);

    ASSERT_EQ(result.val->slotIndex(), 0);
    ASSERT_EQ(result.val->token(), 1);

    const auto* pj_id = std::get_if<int64_t>(&result.val->uid());
    ASSERT_NE(pj_id, nullptr);
    ASSERT_EQ(*pj_id, 1LL);

    result.val->release();
    ASSERT_EQ(pool.activeCount(), 0);

    result = pool.acquire(2LL);
    ASSERT_TRUE(result.ok());
    ASSERT_NE(result.val, nullptr);
    ASSERT_NE(result.val->loop(), nullptr);
    ASSERT_EQ(pool.activeCount(), 1);

    ASSERT_EQ(result.val->slotIndex(), 0);
    ASSERT_EQ(result.val->token(), 3);

    pj_id = std::get_if<int64_t>(&result.val->uid());
    ASSERT_NE(pj_id, nullptr);
    ASSERT_EQ(*pj_id, 2LL);
}


/*
测试思路：
构造 1 个 slot 的 pool。
第一次 acquire 成功。
第二次 acquire 返回 kRuntimeLoopPoolExhausted。
第一个 lease 释放后再次 acquire 成功。

示例：
  capacity=1
  acquire(A) -> ok
  acquire(B) -> exhausted
  release(A)
  acquire(B) -> ok
*/
TEST(TestRuntimeLoopPool, PoolExhausted)
{
    RuntimeLoopPool pool(1);
    auto result = pool.acquire(1);
    ASSERT_TRUE(result.ok());
    ASSERT_NE(result.val, nullptr);

    auto result2 = pool.acquire(2);
    ASSERT_FALSE(result2.ok());
    ASSERT_EQ(result2.error.toInt(), RuntimeError::kRuntimeLoopPoolExhausted);

    result.val->release();
    result2 = pool.acquire(2);
    ASSERT_TRUE(result2.ok());
    ASSERT_NE(result2.val, nullptr);
}

/*
测试思路：
acquire 一个 lease。
手动调用 release() 两次。
activeCount 只减少一次。
后续 acquire 仍然正常。

示例：
  lease.release()
  lease.release()
  activeCount: 1 -> 0 -> 0
*/
TEST(TestRuntimeLoopPool, ReleaseIsIdempotent)
{
    RuntimeLoopPool pool(2);
    auto result = pool.acquire(1);
    ASSERT_TRUE(result.ok());
    ASSERT_NE(result.val, nullptr);
    ASSERT_EQ(pool.activeCount(), 1);

    result.val->release();
    ASSERT_EQ(pool.activeCount(), 0);
    result.val->release();
    ASSERT_EQ(pool.activeCount(), 0);

    result = pool.acquire(2);
    ASSERT_TRUE(result.ok());
    ASSERT_NE(result.val, nullptr);

    const auto *pj_id = std::get_if<int64_t>(&result.val->uid());
    ASSERT_NE(pj_id, nullptr);
    ASSERT_EQ(*pj_id, 2LL);
}


/*
测试思路：
acquire lease A，记录 token A。
release lease A。
acquire lease B，确认 token B 不等于 token A。
再调用 lease A 的 release，不能释放 lease B。
此时 pool 应仍然耗尽，直到 lease B release。

示例：
  A token=1 -> release -> slot state=2
  B token=3 -> old A release(CAS 1->2) failed
  acquire(C) -> exhausted
*/
TEST(TestRuntimeLoopPool, StaleLeaseCannotReleaseNewOwner)
{
    RuntimeLoopPool pool(1);

    auto result = pool.acquire(1);
    ASSERT_TRUE(result.ok());
    ASSERT_NE(result.val, nullptr);
    ASSERT_EQ(pool.activeCount(), 1);
    uint64_t tokenA = result.val->token();

    result.val->release();
    ASSERT_EQ(pool.activeCount(), 0);

    auto result2 = pool.acquire(2);
    ASSERT_TRUE(result2.ok());
    ASSERT_NE(result2.val, nullptr);
    ASSERT_EQ(pool.activeCount(), 1);
    ASSERT_NE(tokenA, result2.val->token());

    result.val->release();
    ASSERT_EQ(pool.activeCount(), 1);

    auto result3 = pool.acquire(3);
    ASSERT_FALSE(result3.ok());
    ASSERT_EQ(result3.error.toInt(), RuntimeError::kRuntimeLoopPoolExhausted);

    result2.val->release();
    ASSERT_EQ(pool.activeCount(), 0);
}


/*
测试思路：
分别用 int64_t、UUID 字符串、雪花 ID 字符串 acquire。
验证 lease 中 uid 保存正确。
验证 slot CAS 逻辑不依赖 uid 类型。

示例：
  acquire(1LL)
  acquire("123e4567-e89b-12d3-a456-426614174000")
  release
  acquire("1469598103934665600")
*/
TEST(TestRuntimeLoopPool, BusinessUidIsMetadataOnly)
{
    RuntimeLoopPool pool(2);
    auto r1 = pool.acquire(1LL);
    ASSERT_TRUE(r1.ok());
    ASSERT_NE(r1.val, nullptr);
    ASSERT_EQ(pool.activeCount(), 1);

    const auto *v1 = std::get_if<int64_t>(&r1.val->uid());
    ASSERT_NE(v1, nullptr);
    ASSERT_EQ(*v1, 1LL);

    const std::string uuid = "123e4567-e89b-12d3-a456-426614174000";
    auto r2 = pool.acquire(uuid);
    ASSERT_TRUE(r2.ok());
    ASSERT_NE(r2.val, nullptr);
    ASSERT_EQ(pool.activeCount(), 2);

    const auto *v2 = std::get_if<std::string>(&r2.val->uid());
    ASSERT_NE(v2, nullptr);
    ASSERT_EQ(*v2, uuid);

    r1.val->release();
    ASSERT_EQ(pool.activeCount(), 1);

    const std::string snowflake = "1469598103934665600";
    auto r3 = pool.acquire(snowflake);
    ASSERT_TRUE(r3.ok());
    ASSERT_NE(r3.val, nullptr);
    ASSERT_EQ(pool.activeCount(), 2);

    const auto *v3 = std::get_if<std::string>(&r3.val->uid());
    ASSERT_NE(v3, nullptr);
    ASSERT_EQ(*v3, snowflake);
}

/*
测试思路：
构造 pool 后调用 Shutdown。
再次 acquire 返回 kRuntimeLoopPoolStopped。

示例：
  pool.shutdown()
  pool.acquire(1) -> kRuntimeLoopPoolStopped
*/
TEST(TestRuntimeLoopPool, ShutdownRejectsAcquire)
{
    RuntimeLoopPool pool(10);
    ASSERT_EQ(pool.capacity(), 10);
    pool.shutdown();
    auto r = pool.acquire(1);
    ASSERT_FALSE(r.ok());
    ASSERT_EQ(r.error.toInt(), RuntimeError::kRuntimeLoopPoolStopped);
}

/*
测试思路：
构造 pool 后连续调用两次 shutdown。
第二次 shutdown 应是幂等 no-op，不应打印错误语义或破坏内部状态。
随后 acquire 仍然稳定返回 kRuntimeLoopPoolStopped。

示例：
  pool.shutdown()
  pool.shutdown()
  pool.acquire(1) -> kRuntimeLoopPoolStopped
*/
TEST(TestRuntimeLoopPool, ShutdownIsIdempotent)
{
    RuntimeLoopPool pool(1);
    pool.shutdown();
    pool.shutdown();

    auto r = pool.acquire(1);
    ASSERT_FALSE(r.ok());
    ASSERT_EQ(r.error.toInt(), RuntimeError::kRuntimeLoopPoolStopped);
}


/*
测试思路：
构造多个 slot，并启动更多 worker 线程并发 acquire/release。
每个 worker 成功拿到 lease 后，用 lease->slotIndex() 反查 slot，并把该 slot 的外部占用计数 +1。
如果 RuntimeLoopPool 的 CAS 租约状态正确，同一时刻同一个 slot 的外部占用计数只能是 1。
worker 在持有 lease 期间短暂 yield/sleep，扩大并发交错窗口；释放 lease 前先把外部占用计数 -1，再 reset lease 触发 RAII release。

示例：
  slot[0] token: 0(free) -> 1(worker A busy) -> 2(free) -> 3(worker B busy)
  并发检查：
  worker A 持有 slot[0] 时，slot_holders[0] == 1；
  如果 worker B 同时拿到 slot[0]，slot_holders[0] 会变成 2，测试记录 concurrent_violation。
*/
TEST(TestRuntimeLoopPool, ConcurrentAcquireRelease)
{
    constexpr size_t kSlotCount = 4;
    constexpr int kWorkerCount = 16;
    constexpr int kIterations = 200;

    RuntimeLoopPool pool(kSlotCount);

    std::vector<std::atomic_int> slot_holders(kSlotCount);
    for(auto &holder : slot_holders)
    {
        holder.store(0);
    }

    std::atomic_int ready_count{0};
    std::atomic_bool start{false};
    std::atomic_int concurrent_violation{0};
    std::atomic_int acquire_error{0};
    std::atomic_int acquire_timeout{0};
    std::atomic_int null_loop_error{0};
    std::atomic_int bad_slot_error{0};
    std::atomic_int bad_token_error{0};
    std::atomic_int completed_count{0};

    std::vector<std::thread> workers;
    workers.reserve(kWorkerCount);

    for(int worker_id = 0; worker_id < kWorkerCount; ++worker_id)
    {
        workers.emplace_back([&, worker_id]() {
            ready_count.fetch_add(1);
            while(!start.load())
            {
                std::this_thread::yield();
            }

            for(int iter = 0; iter < kIterations; ++iter)
            {
                std::shared_ptr<RuntimeLease> lease;
                const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);

                while(!lease && std::chrono::steady_clock::now() < deadline)
                {
                    auto result = pool.acquire(std::string("worker-")
                        + std::to_string(worker_id)
                        + "-iter-"
                        + std::to_string(iter));

                    if(result.ok())
                    {
                        lease = result.val;
                    }
                    else if(result.error.toInt() == RuntimeError::kRuntimeLoopPoolExhausted)
                    {
                        std::this_thread::yield();
                    }
                    else
                    {
                        acquire_error.fetch_add(1);
                        break;
                    }
                }

                if(!lease)
                {
                    acquire_timeout.fetch_add(1);
                    continue;
                }

                if(nullptr == lease->loop())
                {
                    null_loop_error.fetch_add(1);
                }

                const size_t slot_index = lease->slotIndex();
                if(slot_index >= kSlotCount)
                {
                    bad_slot_error.fetch_add(1);
                    lease->release();
                    continue;
                }

                if((lease->token() & 1U) == 0)
                {
                    bad_token_error.fetch_add(1);
                }

                const int holders = slot_holders[slot_index].fetch_add(1) + 1;
                if(holders != 1)
                {
                    concurrent_violation.fetch_add(1);
                }

                if(((worker_id + iter) % 4) == 0)
                {
                    std::this_thread::sleep_for(std::chrono::microseconds(50));
                }
                else
                {
                    std::this_thread::yield();
                }

                const int remains = slot_holders[slot_index].fetch_sub(1) - 1;
                if(remains != 0)
                {
                    concurrent_violation.fetch_add(1);
                }

                lease->release();
                completed_count.fetch_add(1);
            }
        });
    }

    while(ready_count.load() < kWorkerCount)
    {
        std::this_thread::yield();
    }
    start.store(true);

    for(auto &worker : workers)
    {
        worker.join();
    }

    ASSERT_EQ(acquire_error.load(), 0);
    ASSERT_EQ(acquire_timeout.load(), 0);
    ASSERT_EQ(null_loop_error.load(), 0);
    ASSERT_EQ(bad_slot_error.load(), 0);
    ASSERT_EQ(bad_token_error.load(), 0);
    ASSERT_EQ(concurrent_violation.load(), 0);
    ASSERT_EQ(completed_count.load(), kWorkerCount * kIterations);
    ASSERT_EQ(pool.activeCount(), 0);

    for(size_t i = 0; i < kSlotCount; ++i)
    {
        ASSERT_EQ(slot_holders[i].load(), 0);
    }
}

/*
测试思路：
1. 构造 HTTP 运行态协议项，初始路由为 GET /d9/http/header。
2. 只更新 req headers，method/path 保持不变。
3. 更新后断言 req cfg 中 header 已替换，但 req/resp body view 的共享指针没有变化。

示例：
  [route: GET /d9/http/header] + [req body A] + [resp body B]
                    |
                    | UpdateReqCfg(headers)
                    v
  [route: GET /d9/http/header] + [req body A] + [resp body B]
*/
TEST(HttpProjectRuntimeSuite, UpdateReqHeadersKeepsRouteAndBodyViews)
{
    RuntimeLoopPool pool(1);
    auto result = pool.acquire(9001LL);
    ASSERT_TRUE(result.ok());
    ASSERT_NE(result.val, nullptr);

    auto server = std::make_shared<HttpProjectServer>(9001, result.val, RuntimeServerAddress());
    auto protocol = MakeHttpProtocol(101, 9001, "/d9/http/header", {'r', 'e', 'q'}, {'r', 'e', 's', 'p'});
    auto item = ProtocolItemFactory::Create(protocol, server);
    ASSERT_NE(item, nullptr);

    auto add_result = server->AddProtocolItem(item);
    ASSERT_TRUE(add_result.ok()) << add_result.error.toMsg();

    auto before = GetHttpRuntimeItem(server, 101);
    ASSERT_NE(before, nullptr);
    const auto before_req_body = before->getReqBodyView();
    const auto before_resp_body = before->getRespBodyView();

    auto update_result = server->UpdateReqCfgProtocolItem(
        101,
        HttpReqCfg("GET", "/d9/http/header", nljson{{"X-New", "2"}}));

    ASSERT_TRUE(update_result.ok()) << update_result.error.toMsg();
    auto after = GetHttpRuntimeItem(server, 101);
    ASSERT_NE(after, nullptr);

    const auto req_cfg = after->getReqCfg();
    EXPECT_EQ(req_cfg.method.toInt(), HttpRequest::Method::kGet);
    EXPECT_EQ(req_cfg.path, "/d9/http/header");
    ASSERT_EQ(req_cfg.headers.count("X-New"), 1u);
    EXPECT_EQ(req_cfg.headers.at("X-New"), "2");
    EXPECT_EQ(req_cfg.headers.count("X-Old"), 0u);
    EXPECT_EQ(before_req_body.body_data, after->getReqBodyView().body_data);
    EXPECT_EQ(before_resp_body.body_data, after->getRespBodyView().body_data);
}

/*
测试思路：
1. 构造 HTTP 运行态协议项，初始路由为 GET /d9/http/slash。
2. 只把 path 更新成带重复 / 的 //d9//http/slash，语义上仍是同一路由。
3. 断言更新成功并只替换配置，不触发先 addRoute 后 removeRoute 的自冲突。

示例：
  GET /d9/http/slash
          |
          | UpdateReqCfg(GET //d9//http/slash)
          v
  GET /d9/http/slash 对外仍可按归一化路由命中
*/
TEST(HttpProjectRuntimeSuite, UpdateReqSameRouteWithRepeatedSlashDoesNotConflict)
{
    RuntimeLoopPool pool(1);
    auto result = pool.acquire(9016LL);
    ASSERT_TRUE(result.ok());
    ASSERT_NE(result.val, nullptr);

    auto server = std::make_shared<HttpProjectServer>(9016, result.val, RuntimeServerAddress());
    auto protocol = MakeHttpProtocol(151, 9016, "/d9/http/slash");

    auto add_result = server->AddProtocolItem(ProtocolItemFactory::Create(protocol, server));
    ASSERT_TRUE(add_result.ok()) << add_result.error.toMsg();
    auto update_result = server->UpdateReqCfgProtocolItem(
        151,
        HttpReqCfg("GET", "//d9//http/slash", nljson{{"X-New", "slash"}}));

    ASSERT_TRUE(update_result.ok()) << update_result.error.toMsg();
    auto after = GetHttpRuntimeItem(server, 151);
    ASSERT_NE(after, nullptr);
    EXPECT_EQ(after->getReqCfg().path, "//d9//http/slash");
    ASSERT_EQ(after->getReqCfg().headers.count("X-New"), 1u);
    EXPECT_EQ(after->getReqCfg().headers.at("X-New"), "slash");
}

/*
测试思路：
1. 构造两个 HTTP 运行态协议项：
   - 协议 201: GET /d9/http/old
   - 协议 202: GET /d9/http/conflict
2. 把协议 201 的 path 更新成 /d9/http/conflict，触发新 route 冲突。
3. 断言更新失败后，协议 201 仍保持旧 path，协议 202 仍保持自己的 path。

示例：
  pc201 -> GET /old       pc202 -> GET /conflict
      \       Update pc201 to /conflict
       \______________X route conflict
*/
TEST(HttpProjectRuntimeSuite, UpdateReqRouteConflictPreservesOldRouteAndCfg)
{
    RuntimeLoopPool pool(1);
    auto result = pool.acquire(9002LL);
    ASSERT_TRUE(result.ok());
    ASSERT_NE(result.val, nullptr);

    auto server = std::make_shared<HttpProjectServer>(9002, result.val, RuntimeServerAddress());
    auto old_protocol = MakeHttpProtocol(201, 9002, "/d9/http/old");
    auto conflict_protocol = MakeHttpProtocol(202, 9002, "/d9/http/conflict");

    auto add_old_result = server->AddProtocolItem(ProtocolItemFactory::Create(old_protocol, server));
    ASSERT_TRUE(add_old_result.ok()) << add_old_result.error.toMsg();
    auto add_conflict_result = server->AddProtocolItem(ProtocolItemFactory::Create(conflict_protocol, server));
    ASSERT_TRUE(add_conflict_result.ok()) << add_conflict_result.error.toMsg();

    auto update_result = server->UpdateReqCfgProtocolItem(
        201,
        HttpReqCfg("GET", "/d9/http/conflict", nljson{{"X-Try", "conflict"}}));

    ASSERT_FALSE(update_result.ok());
    EXPECT_EQ(update_result.error.toInt(), RuntimeError::kRouteConflict);

    auto old_item = GetHttpRuntimeItem(server, 201);
    ASSERT_NE(old_item, nullptr);
    EXPECT_EQ(old_item->getReqCfg().path, "/d9/http/old");
    EXPECT_EQ(old_item->getReqCfg().headers.count("X-Try"), 0u);

    auto conflict_item = GetHttpRuntimeItem(server, 202);
    ASSERT_NE(conflict_item, nullptr);
    EXPECT_EQ(conflict_item->getReqCfg().path, "/d9/http/conflict");
}

/*
测试思路：
1. 构造 HTTP 运行态协议项，初始路由为 GET /d9/http/move-old。
2. 更新 method/path 到 POST /d9/http/move-new。
3. 再新增一个使用旧路由 GET /d9/http/move-old 的协议项。
4. 如果新增成功，说明旧 route 已释放；同时当前协议项持有的新 method/path 已提交。

示例：
  pc301: GET /move-old
      |
      | UpdateReqCfg(POST /move-new)
      v
  pc301: POST /move-new     pc302: GET /move-old 可重新注册
*/
TEST(HttpProjectRuntimeSuite, UpdateReqRouteSuccessReleasesOldRoute)
{
    RuntimeLoopPool pool(1);
    auto result = pool.acquire(9003LL);
    ASSERT_TRUE(result.ok());
    ASSERT_NE(result.val, nullptr);

    auto server = std::make_shared<HttpProjectServer>(9003, result.val, RuntimeServerAddress());
    auto protocol = MakeHttpProtocol(301, 9003, "/d9/http/move-old");

    auto add_result = server->AddProtocolItem(ProtocolItemFactory::Create(protocol, server));
    ASSERT_TRUE(add_result.ok()) << add_result.error.toMsg();

    auto update_result = server->UpdateReqCfgProtocolItem(
        301,
        HttpReqCfg("POST", "/d9/http/move-new", nljson{{"X-New", "route"}}));
    ASSERT_TRUE(update_result.ok()) << update_result.error.toMsg();

    auto moved_item = GetHttpRuntimeItem(server, 301);
    ASSERT_NE(moved_item, nullptr);
    EXPECT_EQ(moved_item->getReqCfg().method.toInt(), HttpRequest::Method::kPost);
    EXPECT_EQ(moved_item->getReqCfg().path, "/d9/http/move-new");

    auto old_route_protocol = MakeHttpProtocol(302, 9003, "/d9/http/move-old");
    auto add_old_route_result = server->AddProtocolItem(ProtocolItemFactory::Create(old_route_protocol, server));
    ASSERT_TRUE(add_old_route_result.ok()) << add_old_route_result.error.toMsg();

    auto old_route_item = GetHttpRuntimeItem(server, 302);
    ASSERT_NE(old_route_item, nullptr);
    EXPECT_EQ(old_route_item->getReqCfg().path, "/d9/http/move-old");
}

/*
测试思路：
1. 构造 HTTP 运行态协议项，记录 req cfg、resp cfg 和 req body view 指针。
2. 只调用 UpdateReqBodyProtocolItem 替换请求 body。
3. 断言 req/resp cfg 不变，req body view 指针发生替换且数据变成新值。

示例：
  [req cfg H] + [resp cfg R] + [req body old]
                         |
                         | UpdateReqBody(new)
                         v
  [req cfg H] + [resp cfg R] + [req body new]
*/
TEST(HttpProjectRuntimeSuite, UpdateReqBodyOnlyReplacesReqBodyView)
{
    RuntimeLoopPool pool(1);
    auto result = pool.acquire(9004LL);
    ASSERT_TRUE(result.ok());
    ASSERT_NE(result.val, nullptr);

    auto server = std::make_shared<HttpProjectServer>(9004, result.val, RuntimeServerAddress());
    auto protocol = MakeHttpProtocol(401, 9004, "/d9/http/body", {'o', 'l', 'd'}, {'r', 'e', 's', 'p'});

    auto add_result = server->AddProtocolItem(ProtocolItemFactory::Create(protocol, server));
    ASSERT_TRUE(add_result.ok()) << add_result.error.toMsg();

    auto before = GetHttpRuntimeItem(server, 401);
    ASSERT_NE(before, nullptr);
    const auto before_req_cfg = before->getReqCfg();
    const auto before_resp_cfg = before->getRespCfg();
    const auto before_req_body = before->getReqBodyView();
    const auto before_resp_body = before->getRespBodyView();

    const std::vector<char> new_body{'n', 'e', 'w', '-', 'b', 'o', 'd', 'y'};
    auto update_result = server->UpdateReqBodyProtocolItem(
        401,
        ProtocolBodyType::kJson,
        new_body);

    ASSERT_TRUE(update_result.ok()) << update_result.error.toMsg();
    auto after = GetHttpRuntimeItem(server, 401);
    ASSERT_NE(after, nullptr);

    EXPECT_EQ(after->getReqCfg().method.toInt(), before_req_cfg.method.toInt());
    EXPECT_EQ(after->getReqCfg().path, before_req_cfg.path);
    EXPECT_EQ(after->getReqCfg().headers, before_req_cfg.headers);
    EXPECT_EQ(after->getRespCfg().state_code.toInt(), before_resp_cfg.state_code.toInt());
    EXPECT_EQ(after->getRespCfg().headers, before_resp_cfg.headers);
    EXPECT_NE(after->getReqBodyView().body_data, before_req_body.body_data);
    EXPECT_EQ(*after->getReqBodyView().body_data, new_body);
    EXPECT_EQ(after->getRespBodyView().body_data, before_resp_body.body_data);
}

/*
测试思路：
1. ProtocolItemBodyView 是协议无关的运行态 body 快照，只保存业务 body_type 和 body bytes。
2. HTTP media type 和 codec format 都是 body_type 的派生语义，不应缓存在共享运行态快照中。
3. 该用例不发网络请求，只验证快照保存事实字段，派生值由转换 helper 现场得到。

示例：
  req_body_type=json -> body_view.body_type=json
  ProtocolBodyTypeToHttpContentMeta(json) -> application/json
  ProtocolBodyTypeToContentCodecFormat(json) -> kJson
*/
TEST(HttpProjectRuntimeSuite, BodyViewStoresProtocolBodyOnlyAndDerivesHttpMetadata)
{
    RuntimeLoopPool pool(1);
    auto result = pool.acquire(9005LL);
    ASSERT_TRUE(result.ok());
    ASSERT_NE(result.val, nullptr);

    auto server = std::make_shared<HttpProjectServer>(9005, result.val, RuntimeServerAddress());
    auto protocol = MakeHttpProtocol(501, 9005, "/d9/http/body-meta", {'{', '}'}, {'o', 'k'});
    auto item = ProtocolItemFactory::Create(protocol, server);
    ASSERT_NE(item, nullptr);

    auto add_result = server->AddProtocolItem(item);
    ASSERT_TRUE(add_result.ok()) << add_result.error.toMsg();

    auto runtime_item = GetHttpRuntimeItem(server, 501);
    ASSERT_NE(runtime_item, nullptr);
    auto body_view = runtime_item->getReqBodyView();
    EXPECT_EQ(body_view.body_type, ProtocolBodyType::kJson);
    EXPECT_EQ(ProtocolBodyTypeToContentCodecFormat(body_view.body_type), ContentCodecFormat::kJson);
    EXPECT_EQ(ProtocolBodyTypeToHttpContentMeta(body_view.body_type).known_type, KnownMediaType::kApplicationJson);
    EXPECT_EQ(ProtocolBodyTypeToHttpContentMeta(body_view.body_type).media_type, "application/json");

    const std::vector<char> text_body{'h', 'e', 'l', 'l', 'o'};
    auto update_result = server->UpdateReqBodyProtocolItem(501, ProtocolBodyType::kText, text_body);
    ASSERT_TRUE(update_result.ok()) << update_result.error.toMsg();

    body_view = runtime_item->getReqBodyView();
    EXPECT_EQ(body_view.body_type, ProtocolBodyType::kText);
    EXPECT_EQ(ProtocolBodyTypeToContentCodecFormat(body_view.body_type), ContentCodecFormat::kText);
    EXPECT_EQ(ProtocolBodyTypeToHttpContentMeta(body_view.body_type).known_type, KnownMediaType::kTextPlain);
    EXPECT_EQ(ProtocolBodyTypeToHttpContentMeta(body_view.body_type).media_type, "text/plain");
    EXPECT_EQ(*body_view.body_data, text_body);
}

/*
测试思路：
1. 在 HttpProjectServer::start() 前注入 observe callback，保证服务启动后的第一条命中请求不会丢 observation。
2. 通过真实 loopback HTTP 请求命中 GET /d9/http/live-match。
3. 断言客户端收到真实 200 响应，同时 Publisher/Hub 收到 scope=protocol/result=matched 的 record。
4. record 中 request/response 的 header、body、body kind 都来自真实收发数据，而不是只来自协议配置。

示例：
  client -> GET /d9/http/live-match body={"actual":true}
         -> HttpProjectServer sendAndObserve
         -> Publisher -> Hub -> test collector
  collector 收到 record(protocol_id=701, request.body.text={"actual":true})
*/
TEST(HttpProjectRuntimeSuite, RuntimePublishesMatchedObservationThroughPublisherHub)
{
    RuntimeLoopPool pool(1);
    auto result = pool.acquire(9101LL);
    ASSERT_TRUE(result.ok());
    ASSERT_NE(result.val, nullptr);

    auto server = std::make_shared<HttpProjectServer>(9101, result.val, RuntimeServerAddress());
    RuntimeInteractionPipeline pipeline(9101, 701, true);
    server->setObserveCallback(pipeline.Callback());

    const std::string configured_req_body = R"({"expected":true})";
    const std::string configured_resp_body = R"({"accepted":true})";
    auto protocol = MakeHttpProtocol(
        701,
        9101,
        "/d9/http/live-match",
        Chars(configured_req_body),
        Chars(configured_resp_body));
    auto add_result = server->AddProtocolItem(ProtocolItemFactory::Create(protocol, server));
    ASSERT_TRUE(add_result.ok()) << add_result.error.toMsg();
    auto matched_item = GetHttpRuntimeItem(server, 701);
    ASSERT_NE(matched_item, nullptr);
    pipeline.Subscribe(9101, 701, matched_item->cache(), server->cache(), true);

    server->start();

    RuntimeTestFdGuard client_fd(ConnectLoopback(server->getBindAddr().toPort()));
    ASSERT_GE(client_fd.fd, 0);

    const std::string actual_req_body = R"({"actual":true})";
    ASSERT_TRUE(SendAll(client_fd.fd, BuildHttpRequest(
        "GET",
        "/d9/http/live-match",
        actual_req_body,
        "application/json")));

    const std::string response = ReadAll(client_fd.fd);
    EXPECT_NE(response.find("HTTP/1.1 200 OK\r\n"), std::string::npos) << response;
    EXPECT_NE(response.find(configured_resp_body), std::string::npos) << response;

    ASSERT_TRUE(pipeline.collector->WaitForRecordCount(1));
    EXPECT_TRUE(server->stop());

    const auto records = pipeline.collector->Records();
    ASSERT_EQ(records.size(), 1U);
    const auto &record = records.front();

    EXPECT_EQ(record.seq, 1U);
    EXPECT_EQ(record.scope, InteractionScope::kProtocol);
    EXPECT_EQ(record.project_id, 9101);
    EXPECT_EQ(record.protocol_id, 701);
    EXPECT_EQ(record.protocol_type, ProtocolType::kHttp);
    EXPECT_EQ(record.result, InteractionResult::kMatched);
    EXPECT_EQ(record.error_message, "service handle ok");

    EXPECT_EQ(record.request.meta["method"], "GET");
    EXPECT_EQ(record.request.meta["path"], "/d9/http/live-match");
    EXPECT_NE(record.request.head_text.find("GET /d9/http/live-match HTTP/1.1"), std::string::npos);
    EXPECT_EQ(record.request.body.kind, InteractionPayloadKind::kJson);
    EXPECT_EQ(record.request.body.expect_kind, InteractionPayloadKind::kJson);
    EXPECT_EQ(record.request.body.text, actual_req_body);

    EXPECT_EQ(record.response.meta["status_code"], 200);
    EXPECT_NE(record.response.head_text.find("HTTP/1.1 200 OK\r\n"), std::string::npos);
    EXPECT_EQ(record.response.body.kind, InteractionPayloadKind::kJson);
    EXPECT_EQ(record.response.body.expect_kind, InteractionPayloadKind::kJson);
    EXPECT_EQ(record.response.body.text, configured_resp_body);
}

/*
测试思路：
1. 启动没有注册任何路由的 HttpProjectServer。
2. 发送真实 GET /d9/http/missing，请求应由 NotFound404Servlet 产生 404 响应。
3. 因为没有协议项命中，实时链路应发布 scope=project/protocol_id=0/result=route_not_found。
4. 订阅者需要 include_project_notice=true 才能收到该 record。

示例：
  GET /d9/http/missing
      |
      v
  response: 404
  record: scope=project, protocol_id=0, result=route_not_found
*/
TEST(HttpProjectRuntimeSuite, RuntimePublishesRouteNotFoundProjectNotice)
{
    RuntimeLoopPool pool(1);
    auto result = pool.acquire(9102LL);
    ASSERT_TRUE(result.ok());
    ASSERT_NE(result.val, nullptr);

    auto server = std::make_shared<HttpProjectServer>(9102, result.val, RuntimeServerAddress());
    RuntimeInteractionPipeline pipeline(9102, 702, true);
    server->setObserveCallback(pipeline.Callback());
    auto protocol_cache = std::make_shared<InteractionRecordCache>(
        InteractionRecordCacheKey{InteractionScope::kProtocol, 9102, 702});
    pipeline.Subscribe(9102, 702, protocol_cache, server->cache(), true);
    server->start();

    RuntimeTestFdGuard client_fd(ConnectLoopback(server->getBindAddr().toPort()));
    ASSERT_GE(client_fd.fd, 0);
    ASSERT_TRUE(SendAll(client_fd.fd, BuildHttpRequest("GET", "/d9/http/missing")));

    const std::string response = ReadAll(client_fd.fd);
    EXPECT_NE(response.find("HTTP/1.1 404 Not Found\r\n"), std::string::npos) << response;

    ASSERT_TRUE(pipeline.collector->WaitForRecordCount(1));
    EXPECT_TRUE(server->stop());

    const auto records = pipeline.collector->Records();
    ASSERT_EQ(records.size(), 1U);
    const auto &record = records.front();

    EXPECT_EQ(record.scope, InteractionScope::kProject);
    EXPECT_EQ(record.project_id, 9102);
    EXPECT_EQ(record.protocol_id, 0);
    EXPECT_EQ(record.protocol_type, ProtocolType::kHttp);
    EXPECT_EQ(record.result, InteractionResult::kRouteNotFound);
    EXPECT_EQ(record.request.meta["method"], "GET");
    EXPECT_EQ(record.request.meta["path"], "/d9/http/missing");
    EXPECT_EQ(record.response.meta["status_code"], 404);
    EXPECT_NE(record.response.head_text.find("HTTP/1.1 404 Not Found\r\n"), std::string::npos);
}

/*
测试思路：
1. 注册 GET /d9/http/method-only 协议项。
2. 客户端对同一路径发送 POST，请求路径存在但 method 不允许。
3. 断言真实响应是 405，并且响应头带 Allow: GET。
4. 实时链路应发布 scope=project/result=method_not_allowed/protocol_id=0 的 project notice。

示例：
  route table: GET /d9/http/method-only
  request:     POST /d9/http/method-only
       |
       v
  response Allow: GET
  record.result = method_not_allowed
*/
TEST(HttpProjectRuntimeSuite, RuntimePublishesMethodNotAllowedProjectNotice)
{
    RuntimeLoopPool pool(1);
    auto result = pool.acquire(9103LL);
    ASSERT_TRUE(result.ok());
    ASSERT_NE(result.val, nullptr);

    auto server = std::make_shared<HttpProjectServer>(9103, result.val, RuntimeServerAddress());
    RuntimeInteractionPipeline pipeline(9103, 703, true);
    server->setObserveCallback(pipeline.Callback());

    auto protocol = MakeHttpProtocol(703, 9103, "/d9/http/method-only", {}, Chars(R"({"ok":true})"));
    auto add_result = server->AddProtocolItem(ProtocolItemFactory::Create(protocol, server));
    ASSERT_TRUE(add_result.ok()) << add_result.error.toMsg();
    auto method_item = GetHttpRuntimeItem(server, 703);
    ASSERT_NE(method_item, nullptr);
    pipeline.Subscribe(9103, 703, method_item->cache(), server->cache(), true);
    server->start();

    RuntimeTestFdGuard client_fd(ConnectLoopback(server->getBindAddr().toPort()));
    ASSERT_GE(client_fd.fd, 0);
    ASSERT_TRUE(SendAll(client_fd.fd, BuildHttpRequest("POST", "/d9/http/method-only")));

    const std::string response = ReadAll(client_fd.fd);
    EXPECT_NE(response.find("HTTP/1.1 405 Method Not Allowed\r\n"), std::string::npos) << response;
    EXPECT_NE(response.find("Allow: GET\r\n"), std::string::npos) << response;

    ASSERT_TRUE(pipeline.collector->WaitForRecordCount(1));
    EXPECT_TRUE(server->stop());

    const auto records = pipeline.collector->Records();
    ASSERT_EQ(records.size(), 1U);
    const auto &record = records.front();

    EXPECT_EQ(record.scope, InteractionScope::kProject);
    EXPECT_EQ(record.project_id, 9103);
    EXPECT_EQ(record.protocol_id, 0);
    EXPECT_EQ(record.protocol_type, ProtocolType::kHttp);
    EXPECT_EQ(record.result, InteractionResult::kMethodNotAllowed);
    EXPECT_EQ(record.request.meta["method"], "POST");
    EXPECT_EQ(record.request.meta["path"], "/d9/http/method-only");
    EXPECT_EQ(record.response.meta["status_code"], 405);
    EXPECT_NE(record.response.head_text.find("Allow: GET"), std::string::npos);
}

/*
测试思路：
1. 向 HTTP 运行态服务发送无法解析的请求行，触发 llhttp parse error。
2. 服务端应保持原 HTTP 行为，返回真实 400 响应并关闭连接。
3. 实时链路应发布 scope=project/result=parse_error/protocol_id=0。
4. 因为请求头都没有解析完成，request 侧应通过 raw_packet 展示有限原始报文。

示例：
  bytes: "BAD REQUEST\r\n\r\n"
      |
      v
  response: 400
  record.request.raw_packet.raw_hex 包含 BAD REQUEST 的十六进制前缀
*/
TEST(HttpProjectRuntimeSuite, RuntimePublishesParseErrorRawPacketAndKeeps400Response)
{
    RuntimeLoopPool pool(1);
    auto result = pool.acquire(9104LL);
    ASSERT_TRUE(result.ok());
    ASSERT_NE(result.val, nullptr);

    auto server = std::make_shared<HttpProjectServer>(9104, result.val, RuntimeServerAddress());
    RuntimeInteractionPipeline pipeline(9104, 704, true);
    server->setObserveCallback(pipeline.Callback());
    auto protocol_cache = std::make_shared<InteractionRecordCache>(
        InteractionRecordCacheKey{InteractionScope::kProtocol, 9104, 704});
    pipeline.Subscribe(9104, 704, protocol_cache, server->cache(), true);
    server->start();

    RuntimeTestFdGuard client_fd(ConnectLoopback(server->getBindAddr().toPort()));
    ASSERT_GE(client_fd.fd, 0);
    ASSERT_TRUE(SendAll(client_fd.fd, "BAD REQUEST\r\n\r\n"));

    const std::string response = ReadAll(client_fd.fd);
    EXPECT_NE(response.find("HTTP/1.1 400 Bad Request\r\n"), std::string::npos) << response;

    ASSERT_TRUE(pipeline.collector->WaitForRecordCount(1));
    EXPECT_TRUE(server->stop());

    const auto records = pipeline.collector->Records();
    ASSERT_EQ(records.size(), 1U);
    const auto &record = records.front();

    EXPECT_EQ(record.scope, InteractionScope::kProject);
    EXPECT_EQ(record.project_id, 9104);
    EXPECT_EQ(record.protocol_id, 0);
    EXPECT_EQ(record.protocol_type, ProtocolType::kHttp);
    EXPECT_EQ(record.result, InteractionResult::kParseError);
    EXPECT_EQ(record.error_message, "http request parse error");
    ASSERT_TRUE(record.request.raw_packet.has_value());
    EXPECT_GT(record.request.raw_packet->size, 0U);
    EXPECT_NE(record.request.raw_packet->raw_hex.find("H42 41 44"), std::string::npos);
    EXPECT_EQ(record.response.meta["status_code"], 400);
}

/*
测试思路：
1. observe callback 模拟 Publisher/Hub 链路异常，收到 observation 后直接抛出异常。
2. ProjectServer::emitObserve 应捕获异常，不能影响协议响应线程继续 send。
3. 客户端仍应收到协议项配置的 200 响应和 body。

示例：
  sendAndObserve -> emitObserve(callback throws)
                     |
                     v
                  catch exception
  conn->send(HTTP 200 {"callback":"ignored"})
*/
TEST(HttpProjectRuntimeSuite, RuntimeObserveCallbackExceptionDoesNotChangeResponse)
{
    RuntimeLoopPool pool(1);
    auto result = pool.acquire(9105LL);
    ASSERT_TRUE(result.ok());
    ASSERT_NE(result.val, nullptr);

    auto server = std::make_shared<HttpProjectServer>(9105, result.val, RuntimeServerAddress());
    std::atomic_int callback_count{0};
    server->setObserveCallback([&callback_count](ProtocolInteractionObservation) {
        callback_count.fetch_add(1);
        throw std::runtime_error("observe callback failed");
    });

    const std::string configured_resp_body = R"({"callback":"ignored"})";
    auto protocol = MakeHttpProtocol(
        705,
        9105,
        "/d9/http/callback-throws",
        {},
        Chars(configured_resp_body));
    auto add_result = server->AddProtocolItem(ProtocolItemFactory::Create(protocol, server));
    ASSERT_TRUE(add_result.ok()) << add_result.error.toMsg();
    server->start();

    RuntimeTestFdGuard client_fd(ConnectLoopback(server->getBindAddr().toPort()));
    ASSERT_GE(client_fd.fd, 0);
    ASSERT_TRUE(SendAll(client_fd.fd, BuildHttpRequest("GET", "/d9/http/callback-throws")));

    const std::string response = ReadAll(client_fd.fd);
    EXPECT_NE(response.find("HTTP/1.1 200 OK\r\n"), std::string::npos) << response;
    EXPECT_NE(response.find(configured_resp_body), std::string::npos) << response;
    EXPECT_EQ(callback_count.load(), 1);
    EXPECT_TRUE(server->stop());
}

/*
测试思路：
1. 配置 response body 为 multipart 描述，而不是已经编码好的 HTTP body。
2. 真实 runtime 响应必须生成固定 boundary、Content-Type 参数和合法 multipart 字节流。
3. 同一条请求还要通过 interaction publisher，确认文本 part 进入附件元数据，图片 part 进入 sidecar。

示例：
  response config(fields=[message, avatar])
      |
      v
  HTTP response multipart/form-data
      |
      v
  record.response.body.attachments + binary_sidecars
*/
TEST(HttpProjectRuntimeSuite, RuntimeEncodesMultipartResponseAndCapturesAttachments)
{
    constexpr int64_t project_id = 9106;
    constexpr int64_t protocol_id = 706;
    constexpr const char *kBoundary = "KitProtocolFormBoundary";

    RuntimeLoopPool pool(1);
    auto result = pool.acquire(project_id);
    ASSERT_TRUE(result.ok());
    ASSERT_NE(result.val, nullptr);

    auto server = std::make_shared<HttpProjectServer>(
        project_id, result.val, RuntimeServerAddress());
    RuntimeInteractionPipeline pipeline(project_id, protocol_id, true);
    server->setObserveCallback(pipeline.Callback());

    const std::string response_descriptor = R"({
        "fields": [
            {"name":"message","type":"text","value":"hello multipart"},
            {"name":"avatar","type":"file","filename":"avatar.png",
             "content_type":"image/png","data_base64":"iVBORw=="}
        ]
    })";
    auto protocol = MakeHttpProtocol(
        protocol_id,
        project_id,
        "/d9/http/multipart-response",
        {},
        Chars(response_descriptor),
        ProtocolBodyType::kNone,
        ProtocolBodyType::kMultiForm);
    auto add_result = server->AddProtocolItem(ProtocolItemFactory::Create(protocol, server));
    ASSERT_TRUE(add_result.ok()) << add_result.error.toMsg();

    auto runtime_item = GetHttpRuntimeItem(server, protocol_id);
    ASSERT_NE(runtime_item, nullptr);
    pipeline.Subscribe(project_id, protocol_id, runtime_item->cache(), server->cache(), true);
    server->start();

    RuntimeTestFdGuard client_fd(ConnectLoopback(server->getBindAddr().toPort()));
    ASSERT_GE(client_fd.fd, 0);
    ASSERT_TRUE(SendAll(client_fd.fd, BuildHttpRequest("GET", "/d9/http/multipart-response")));

    const std::string response = ReadAll(client_fd.fd);
    EXPECT_NE(response.find("HTTP/1.1 200 OK\r\n"), std::string::npos) << response;
    EXPECT_NE(response.find("Content-Type: multipart/form-data; boundary=") , std::string::npos)
        << response;

    const auto header_end = response.find("\r\n\r\n");
    ASSERT_NE(header_end, std::string::npos);
    const std::string wire_body = response.substr(header_end + 4);
    const auto form = kit_muduo::http::MultiForm::parse(
        reinterpret_cast<const uint8_t *>(wire_body.data()),
        wire_body.size(),
        kBoundary);
    ASSERT_TRUE(form.contains("message"));
    ASSERT_TRUE(form.contains("avatar"));
    EXPECT_EQ(form.at("message").strs(), "hello multipart");
    EXPECT_EQ(form.at("avatar").filename, "avatar.png");
    EXPECT_EQ(form.at("avatar").meta.media_type, "image/png");
    EXPECT_EQ(form.at("avatar").data, (std::vector<uint8_t>{0x89, 'P', 'N', 'G'}));

    ASSERT_TRUE(pipeline.collector->WaitForRecordCount(1));
    EXPECT_TRUE(server->stop());

    const auto records = pipeline.collector->Records();
    ASSERT_EQ(records.size(), 1U);
    const auto &record = records.front();
    EXPECT_EQ(record.response.body.kind, InteractionPayloadKind::kMultiForm);
    EXPECT_EQ(record.response.body.expect_kind, InteractionPayloadKind::kMultiForm);
    ASSERT_EQ(record.response.body.attachments.size(), 2U);
    ASSERT_EQ(record.binary_sidecars.size(), 1U);
    ASSERT_NE(record.binary_sidecars.front().bytes, nullptr);
    EXPECT_EQ(*record.binary_sidecars.front().bytes,
              (std::vector<uint8_t>{0x89, 'P', 'N', 'G'}));

    bool found_text = false;
    bool found_image = false;
    for(const auto &attachment : record.response.body.attachments)
    {
        if(attachment.kind == InteractionPayloadKind::kText)
        {
            found_text = true;
            EXPECT_EQ(attachment.text, "hello multipart");
            EXPECT_FALSE(attachment.binary_available);
        }
        if(attachment.kind == InteractionPayloadKind::kImage)
        {
            found_image = true;
            EXPECT_TRUE(attachment.binary_available);
            EXPECT_EQ(attachment.size, 4U);
            EXPECT_EQ(attachment.flag.rfind("response.body.multiform.", 0), 0U);
        }
    }
    EXPECT_TRUE(found_text);
    EXPECT_TRUE(found_image);
}

/*
测试思路：
1. HTTP project runtime 是协议测试平台，默认要求请求 Content-Type 精确命中协议项配置的 media type。
2. 配置 req_body_type=json 时，期望 media_type 是 application/json；application/problem+json 虽然 codec 也是 JSON，但不应命中协议项。
3. 通过真实 loopback HTTP 请求触发 runtime handler，断言响应明确区分为 media type mismatch，而不是 body parse error。

示例：
  protocol cfg: req_body_type=json, req_body={}
  request: Content-Type=application/problem+json, body={}
        |
        v
  {"code":-200,"message":"media type mismatch"}
*/
TEST(HttpProjectRuntimeSuite, DISABLED_RuntimeStrictMatchRejectsProblemJsonForConfiguredJsonBody)
{
    RuntimeLoopPool pool(1);
    auto result = pool.acquire(9006LL);
    ASSERT_TRUE(result.ok());
    ASSERT_NE(result.val, nullptr);

    auto server = std::make_shared<HttpProjectServer>(9006, result.val, RuntimeServerAddress());
    auto protocol = MakeHttpProtocol(601, 9006, "/d9/http/strict-json", {'{', '}'}, {'{', '}'});
    auto add_result = server->AddProtocolItem(ProtocolItemFactory::Create(protocol, server));
    ASSERT_TRUE(add_result.ok()) << add_result.error.toMsg();

    server->start();

    RuntimeTestFdGuard client_fd(ConnectLoopback(server->getBindAddr().toPort()));
    ASSERT_GE(client_fd.fd, 0);

    const std::string body = "{}";
    const std::string request =
        "GET /d9/http/strict-json HTTP/1.1\r\n"
        "Host: 127.0.0.1\r\n"
        "Content-Type: application/problem+json\r\n"
        "Content-Length: " + std::to_string(body.size()) + "\r\n"
        "Connection: close\r\n"
        "\r\n" +
        body;
    ASSERT_TRUE(SendAll(client_fd.fd, request));

    const std::string response = ReadAll(client_fd.fd);
    EXPECT_NE(response.find("HTTP/1.1 200 OK\r\n"), std::string::npos) << response;
    EXPECT_NE(response.find("\"code\":-200"), std::string::npos) << response;
    EXPECT_NE(response.find("\"message\":\"media type mismatch\""), std::string::npos) << response;
    EXPECT_EQ(response.find("body parse error"), std::string::npos) << response;

    EXPECT_TRUE(server->stop());
}
