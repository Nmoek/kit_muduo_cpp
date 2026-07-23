/**
 * @file test_custom_project_server.cpp
 * @brief 自定义TCP服务器测试
 * @author ljk5
 * @version 1.0
 * @date 2025-11-16 21:02:23
 * @copyright Copyright (c) 2025 HIKRayin
 */

#include "../../test_log.h"
#include "./test_custom_project_server.h"
#include "domain/runtime_loop_pool.h"
#include "net/inet_address.h"
#include "net/tcp_connection.h"
#include "net/tcp_server.h"
#include "base/thread.h"
#include "base/event_loop_thread.h"
#include "net/event_loop.h"
#include "domain/project_server.h"
#include "domain/project.h"
#include "domain/protocol.h"
#include "domain/protocol_item.h"
#include "domain/custom_tcp_protocol_item.h"
#include "domain/custom_tcp_context.h"
#include "domain/custom_tcp_message.h"
#include "domain/custom_tcp_project_server.h"
#include "domain/protocol_interaction_hub.h"
#include "domain/protocol_interaction_publisher.h"
#include "net/net_data_converter.h"

#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cerrno>
#include <cstring>
#include <fstream>
#include <memory>
#include <mutex>
#include <poll.h>
#include <string>
#include <sys/socket.h>
#include <unistd.h>
#include <vector>

using namespace kit_muduo;
using namespace kit_muduo::http;
using namespace kit_domain;
using namespace testing;

#define CUSTOM_TEST_INFO_BEGIN(sub_name) do{ \
    SCOPED_TRACE("Testing: [" + sub_name + "]"); \
    std::cout << "[ RUN      ] " << "[" << sub_name << "]" << std::endl;\
}while (0)



#define CUSTOM_TEST_INFO_END(sub_name) do{\
    std::cout << "[       OK ] " << "[" << sub_name << "]" << std::endl;\
}while(0)


#define SERVER_PORT (5555)
#define SERVER_IP   "127.0.0.0"

using CustomTcpMessagePtr = std::shared_ptr<CustomTcpMessage>;
using CustomTcpContextPtr = std::shared_ptr<CustomTcpContext>;

using ReqBuildFunc = std::function<void(std::vector<char>&)>;

struct TestCases1 {
    std::string sub_name;       //子项名称
    kit_domain::Project pj;      // 服务器的配置项
    kit_domain::Protocol pc;     // 协议项的配置项
    ReqBuildFunc reqBuild;      //请求参数
};

struct TestCases2 {
    std::string sub_name;       //子项名称
    ReqBuildFunc reqBuild;      //请求参数

    int wantRes;                // 期待的结果
};

static std::string BodyString(const CustomTcpMessagePtr &message)
{
    if(!message)
    {
        return {};
    }
    const auto &body = message->bodyData();
    return std::string(body.begin(), body.end());
}

class CustomTcpInteractionCollector
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

struct CustomTcpInteractionPipeline
{
    CustomTcpInteractionPipeline(int64_t project_id,
                                 int64_t protocol_id,
                                 bool include_project_notice = true)
        : hub(std::make_shared<ProtocolInteractionHub>())
        , collector(std::make_shared<CustomTcpInteractionCollector>())
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
        if(!protocol_cache)
        {
            protocol_cache = std::make_shared<InteractionRecordCache>(
                InteractionRecordCacheKey{
                    InteractionScope::kProtocol, project_id, protocol_id});
        }
        if(!project_cache)
        {
            project_cache = std::make_shared<InteractionRecordCache>(
                InteractionRecordCacheKey{
                    InteractionScope::kProject, project_id, 0});
        }
        protocol_cache_ = std::move(protocol_cache);
        project_cache_ = std::move(project_cache);

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

    ~CustomTcpInteractionPipeline()
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
    std::shared_ptr<CustomTcpInteractionCollector> collector;
    ProtocolInteractionPublisher publisher;
    SubscribeWithCatchUpResult subscription;
    std::shared_ptr<InteractionRecordCache> protocol_cache_;
    std::shared_ptr<InteractionRecordCache> project_cache_;
};




class CustomTcpServerSuite : public ::testing::Test
{
protected:
    CustomTcpServerSuite()
        :loop_pool_(10)
    {

    }

    void SetUp() override
    {
        // auto l = KIT_LOGGER("net");
        // auto l2 = KIT_LOGGER("base");
        // auto l3 = KIT_LOGGER("web");
        // l->setLevel(LogLevel::ERROR);
        // l2->setLevel(LogLevel::ERROR);
        // l3->setLevel(LogLevel::ERROR);
        
    }

    std::shared_ptr<CustomTcpProjectServer> server_start(const kit_domain::Project &p)
    {
        auto result = loop_pool_.acquire(p.m_id);

        // 创建自定义TCP服务器必须带解析格式，否则无法解析。
        // R1 之后 runtime loop 和 TcpServer 生命周期由 CustomTcpProjectServer 自己持有。
        return std::make_shared<CustomTcpProjectServer>(
            p.m_id,
            p.m_patternInfo,
            result.val,
            InetAddress(0, "127.0.0.1"));
    }

    RuntimeLoopPool loop_pool_;
};


static int tcp_send(const std::vector<char>& input, CustomTcpContextPtr ctx, const InetAddress &server_addr) 
{
    (void)ctx;

    int client_fd = -1;
    auto close_client = [&client_fd]() {
        if(client_fd >= 0)
        {
            close(client_fd);
            client_fd = -1;
        }
    };

    // 1. 创建socket并连接。TcpServer::start() 会把 listen 投递到 runtime loop，
    //    这里短暂重试，避免测试线程抢在 listen 生效前 connect。
    constexpr int kConnectRetryTimes = 100;
    constexpr int kConnectRetrySleepUs = 1000;
    int last_errno = 0;
    for(int i = 0; i < kConnectRetryTimes; ++i)
    {
        client_fd = socket(AF_INET, SOCK_STREAM, 0);
        if(client_fd < 0)
        {
            TEST_ERROR() << "socket creation failed "
                        << errno << ":"
                        << strerror(errno) << std::endl;
            return -1;
        }

        if(connect(client_fd, (struct sockaddr *)server_addr.getSockAddr(), sizeof(struct sockaddr)) == 0)
        {
            last_errno = 0;
            break;
        }

        last_errno = errno;
        close_client();
        if(ECONNREFUSED != last_errno && EINTR != last_errno && EAGAIN != last_errno)
        {
            break;
        }
        usleep(kConnectRetrySleepUs);
    }

    if(client_fd < 0)
    {
        TEST_ERROR() << "connection failed "
                    << last_errno << ":"
                    << strerror(last_errno) << std::endl;
        return -2;
    }

    // 2. 发送完整请求，避免短写导致服务端收到半包。
    size_t sent = 0;
    while(sent < input.size())
    {
        ssize_t n = send(client_fd, input.data() + sent, input.size() - sent, 0);
        if(n > 0)
        {
            sent += static_cast<size_t>(n);
            continue;
        }

        if(n < 0 && EINTR == errno)
        {
            continue;
        }

        TEST_ERROR() << "send failed "
                    << errno << ":"
                    << strerror(errno) << std::endl;
        close_client();
        return -3;
    }

    // 3. 接收响应。先等可读，避免全套测试压力下在响应尚未写回时直接误判。
    struct pollfd read_poll;
    read_poll.fd = client_fd;
    read_poll.events = POLLIN;
    read_poll.revents = 0;
    int poll_res = poll(&read_poll, 1, 1000);
    if(poll_res <= 0)
    {
        const int poll_errno = errno;
        TEST_ERROR() << "read wait failed "
                    << poll_res << " "
                    << poll_errno << ":"
                    << strerror(poll_errno) << std::endl;
        close_client();
        return -4;
    }

    Buffer buf;
    int32_t savedErrno = 0;
    int valread = buf.readFd(client_fd, &savedErrno);
    if(valread <= 0)
    {
        TEST_ERROR() << "read failed " 
        << savedErrno << ":" 
        << strerror(savedErrno) << std::endl;
        close_client();

        return -5;
    }
    const auto& resp_data = buf.resetAllAsData();

    TEST_DEBUG() << "recv: " << kit_muduo::BytesToHexString(std::vector<uint8_t>(resp_data.begin(), resp_data.end()), " ") << std::endl;
    
    std::fstream f("resp.bin", std::ios::out |std::ios::trunc | std::ios::binary);
    if(f.is_open())
    {
        printf("write resp.bin ok!\n");
        f.write(resp_data.data(), resp_data.size());
        f.flush();
        f.close();
    }


    // 4. 关闭连接
    close_client();
    
    return 0;
}

struct CustomTcpFdGuard
{
    explicit CustomTcpFdGuard(int input_fd = -1)
        :fd(input_fd)
    {}

    ~CustomTcpFdGuard()
    {
        if(fd >= 0)
        {
            close(fd);
        }
    }

    int fd;
};

static int connect_custom_tcp_with_retry(const InetAddress &server_addr)
{
    constexpr int kConnectRetryTimes = 100;
    constexpr int kConnectRetrySleepUs = 1000;
    int last_errno = 0;

    for(int i = 0; i < kConnectRetryTimes; ++i)
    {
        int client_fd = socket(AF_INET, SOCK_STREAM, 0);
        if(client_fd < 0)
        {
            return -1;
        }

        if(connect(client_fd, (struct sockaddr *)server_addr.getSockAddr(), sizeof(struct sockaddr)) == 0)
        {
            return client_fd;
        }

        last_errno = errno;
        close(client_fd);
        if(ECONNREFUSED != last_errno && EINTR != last_errno && EAGAIN != last_errno)
        {
            break;
        }
        usleep(kConnectRetrySleepUs);
    }

    TEST_ERROR() << "connection failed "
                << last_errno << ":"
                << strerror(last_errno) << std::endl;
    return -1;
}

static bool send_all_custom_tcp(int fd, const std::vector<char> &input)
{
    size_t sent = 0;
    while(sent < input.size())
    {
        ssize_t n = send(fd, input.data() + sent, input.size() - sent, 0);
        if(n > 0)
        {
            sent += static_cast<size_t>(n);
            continue;
        }

        if(n < 0 && EINTR == errno)
        {
            continue;
        }
        return false;
    }

    return true;
}

static int tcp_send_no_response_required(const std::vector<char>& input, const InetAddress &server_addr)
{
    CustomTcpFdGuard client_fd(connect_custom_tcp_with_retry(server_addr));
    if(client_fd.fd < 0)
    {
        return -1;
    }

    if(!send_all_custom_tcp(client_fd.fd, input))
    {
        TEST_ERROR() << "send failed "
                    << errno << ":"
                    << strerror(errno) << std::endl;
        return -2;
    }

    (void)shutdown(client_fd.fd, SHUT_WR);
    return 0;
}



// 按照1字节对齐
#pragma pack(push, 1)  // 保存当前对齐

struct Pattern1 {
    int32_t         field1;  //起始标识
    uint32_t        field2;  //消息总长度
    uint32_t        field3;  //消息序列号
    uint16_t        field4;  //消息类型
    uint32_t        field5;  // 报文体长度
    uint64_t        field6;  // 报文体消息时间戳
};

// 用于回归“body 分片到达”场景的最小头部，避免 common fields 消费长度缺陷干扰状态机测试。
struct PatternPartialBody {
    int32_t         field1;  //起始标识
    uint16_t        field2;  //消息类型
    uint64_t        field3;  //报文体长度
};

/// @brief 郑州邮政 总包发送包裹小车号
struct Pattern2_2 {
    int8_t          field1;    //起始字符长度
    uint16_t        field2;    //报文总长度
    uint8_t         field3;    //功能码
    uint16_t        field4;    //线体号
    uint32_t        field5;    //包裹号
    int8_t          field6;    //LCR校验
    int8_t          field7[3]; //结束字符
};

struct Pattern2_2_Resp {
    // int8_t          field1;    //起始字符长度
    // uint16_t        field2;    //报文总长度
    // uint8_t         field3;    //功能码
    uint16_t        field4;    //线体号
    uint32_t        field5;    //包裹号
    uint32_t        field6;    //包裹ID
    int8_t          field7;    //LCR校验
    int8_t          field8[3]; //结束字符
};


#pragma pack(pop)       // 恢复之前的对齐设置

static void ReqBuilderHelper1(std::vector<char>& req, const nljson& body_root)
{
    Pattern1 pattern;

    TEST_ERROR() << "ReqBuilderHelper1::sizeof(Pattern1)::" << sizeof(Pattern1) << std::endl;

    
    // 需要通过字面量去进行赋值
    pattern.field1 = 0x23232323;
    // pattern.field1 = HexToDataConverter<int32_t>()("H11232323", false);

    TEST_ERROR() << "magic number: " << pattern.field1  << ", " << kit_muduo::DataToHexConverter<int32_t>()(pattern.field1, !KIT_IS_LOCAL_BIG_ENDIAN()) << std::endl;

    SwapToBigEndian(pattern.field1);


    pattern.field3 = 1;
    SwapToBigEndian(pattern.field3);


    // 两个字节 0x00 0x01 
    // pattern.field4 = 0b00000001; //这种赋值是错误的
    // 等价于=> pattern.field4 = 1;
    // 测试时使用字面量进行转换
    // pattern.field4 = HexToDataConverter<uint16_t>()("H1111", false);
    pattern.field4 = 0x0100;

    // 小心观测本身是有误导性的
    TEST_F_ERROR("func_code: %d, %x, %s \n", pattern.field4,pattern.field4, kit_muduo::DataToHexConverter<uint16_t>()(pattern.field4, false).c_str());

    SwapToBigEndian(pattern.field4);


    /* 报文体长度 */
    pattern.field5 = body_root.dump().size();
    /* 消息总长度 */
    pattern.field2 = sizeof(Pattern1) + pattern.field5;
    req.resize(pattern.field2);

    TEST_ERROR() << "body_len: " << pattern.field5  << ", " << kit_muduo::DataToHexConverter<int32_t>()(pattern.field5, !KIT_IS_LOCAL_BIG_ENDIAN()) << std::endl;

    TEST_ERROR()<< "total_len: " <<  pattern.field2 << ", " << kit_muduo::DataToHexConverter<int32_t>()(pattern.field2, !KIT_IS_LOCAL_BIG_ENDIAN()) << std::endl;

    SwapToBigEndian(pattern.field5);
    SwapToBigEndian(pattern.field2);

    pattern.field6 = time(nullptr) * 1000;
    SwapToBigEndian(pattern.field6);


    memcpy(req.data(), &pattern, sizeof(Pattern1));
    memcpy(req.data() + sizeof(Pattern1), body_root.dump().c_str(), body_root.dump().size());

    TEST_DEBUG() << "send: " << kit_muduo::BytesToHexString(std::vector<uint8_t>(req.begin(), req.end()), " ") << std::endl;

    std::fstream f("req.bin", std::ios::out |std::ios::trunc | std::ios::binary);
    if(f.is_open())
    {
        printf("write req.bin ok!\n");
        f.write(req.data(), req.size());
    }
    f.flush();
    f.close();
}

static void PatchPattern1FunctionCode(std::vector<char> &req, uint8_t high, uint8_t low)
{
    ASSERT_GE(req.size(), static_cast<size_t>(14));
    req[12] = static_cast<char>(high);
    req[13] = static_cast<char>(low);
}

static void ReqBuilderHelper2_1(std::vector<char>& req, const nljson& body_root)
{
    Pattern1 pattern{};
    const std::string body = body_root.dump();

    pattern.field1 = 0x23232323;
    pattern.field2 = sizeof(Pattern1) + body.size();
    pattern.field3 = 3;
    pattern.field4 = 0x0001;
    pattern.field5 = body.size();
    pattern.field6 = time(nullptr) * 1000;

    req.resize(sizeof(Pattern1) + body.size());
    memcpy(req.data(), &pattern, sizeof(Pattern1));
    memcpy(req.data() + sizeof(Pattern1), body.data(), body.size());
}

static void ReqBuilderHelperPartialBody(std::vector<char>& req, const nljson& body_root)
{
    PatternPartialBody pattern;
    const std::string body = body_root.dump();

    TEST_ERROR() << "ReqBuilderHelperPartialBody::sizeof(PatternPartialBody)::" << sizeof(PatternPartialBody) << std::endl;

    pattern.field1 = 0x23232323;
    SwapToBigEndian(pattern.field1);

    pattern.field2 = 0x0100;
    SwapToBigEndian(pattern.field2);

    pattern.field3 = body.size();
    req.resize(sizeof(PatternPartialBody) + body.size());

    TEST_ERROR() << "body_len: " << pattern.field3  << ", " << kit_muduo::DataToHexConverter<uint64_t>()(pattern.field3, !KIT_IS_LOCAL_BIG_ENDIAN()) << std::endl;

    SwapToBigEndian(pattern.field3);

    memcpy(req.data(), &pattern, sizeof(PatternPartialBody));
    memcpy(req.data() + sizeof(PatternPartialBody), body.data(), body.size());
}


static void ReqBuilderHelper2_2(std::vector<char>& req)
{
    Pattern2_2 pattern;

    TEST_ERROR() << "ReqBuilderHelper2_2::sizeof(Pattern2_2)::" << sizeof(Pattern2_2) << std::endl;

    pattern.field1 = 0x02;
    SwapToBigEndian(pattern.field1);

    /* 消息总长度 */
    pattern.field2 = sizeof(Pattern2_2);
    req.resize(pattern.field2);


    TEST_ERROR()<< "total_len: " <<  pattern.field2 << ", " << kit_muduo::DataToHexConverter<uint16_t>()(pattern.field2, false) << std::endl;

    SwapToBigEndian(pattern.field2);

    TEST_ERROR()<< "total_len: " <<  pattern.field2 << ", " << kit_muduo::DataToHexConverter<uint16_t>()(pattern.field2, false) << std::endl;

    /*功能码*/
    pattern.field3 = 0x32;

    TEST_ERROR() << "func_code: " << pattern.field3  << ", " << kit_muduo::DataToHexConverter<uint8_t>()(pattern.field3, false) << std::endl;

    SwapToBigEndian(pattern.field3);

    TEST_ERROR() << "func_code: " << pattern.field3  << ", " << kit_muduo::DataToHexConverter<uint8_t>()(pattern.field3, false) << std::endl;

    // 线体号
    pattern.field4 = 0x01;
    SwapToBigEndian(pattern.field4);

    // 包裹号
    pattern.field5 = 0x00000009;
    SwapToBigEndian(pattern.field5);

    // LCR校验
    pattern.field6 = 0x01;
    SwapToBigEndian(pattern.field6);

    pattern.field7[0] = 0x03;
    pattern.field7[1] = 0x0D;
    pattern.field7[2] = 0x0A;

    memcpy(req.data(), &pattern, sizeof(Pattern2_2));


    std::fstream f("/mnt/nfs/proxy_bin/req.bin", std::ios::out |std::ios::trunc | std::ios::binary);
    if(f.is_open())
    {
        printf("write req.bin ok!\n");
        f.write(req.data(), req.size());
    }
    f.flush();
    f.close();
}


static std::vector<char> MakeResp2_2() noexcept
{
    Pattern2_2_Resp pattern;
    std::vector<char> resp(sizeof(Pattern2_2_Resp));
 
    TEST_ERROR() << "MakeResp2_2::sizeof(Pattern2_2_Resp)::" << sizeof(Pattern2_2_Resp) << std::endl;

    // 线体号
    pattern.field4 = 0x01;
    SwapToBigEndian(pattern.field4);

    // 包裹号
    pattern.field5 = 0x00000009;
    SwapToBigEndian(pattern.field5);

    // 包裹ID
    pattern.field6 = 0x00000001;
    SwapToBigEndian(pattern.field6);

    // LCR校验
    pattern.field7 = 0x01;
    SwapToBigEndian(pattern.field7);

    pattern.field8[0] = 0x03;
    pattern.field8[1] = 0x0D;
    pattern.field8[2] = 0x0A;

    memcpy(resp.data(), &pattern, sizeof(Pattern2_2_Resp));

    std::vector<uint8_t> bytes(resp.begin(), resp.end());
    TEST_DEBUG() << "resp2_2: " << BytesToHexString(bytes) << std::endl;

    return resp;
}

static kit_domain::Project MakeBodyLengthProject(int64_t project_id)
{
    kit_domain::Project project;
    project.m_id = project_id;
    project.m_name = "d9_tcp_project";
    project.m_mode = ProjectMode::ServerMode;
    project.m_protocolType = ProtocolType::kCustomTcp;
    project.m_listenPort = 8888;
    project.m_targetIp = "";
    project.m_userId = 0;
    project.m_status = ProjectStatus::kValid;
    project.m_runtimeState = ProjectRuntimeState::kRunning;
    project.m_patternInfo = std::vector<char>(pattern_json_str1.begin(), pattern_json_str1.end());
    project.m_ctime = TimeStamp::Now();
    return project;
}

static kit_domain::Protocol MakeBodyLengthProtocol(
        int64_t protocol_id,
        int64_t project_id,
        const std::string &func_code,
        const std::vector<char> &req_body = {},
        const std::vector<char> &resp_body = std::vector<char>(resp_body1.begin(), resp_body1.end()))
{
    nljson req_cfg = nljson::parse(req_cfg1);
    req_cfg["function_code"] = func_code;

    nljson resp_cfg = nljson::parse(resp_cfg1);
    resp_cfg["function_code"] = func_code == "H0100" ? "H1080" : "H2080";

    kit_domain::Protocol protocol;
    protocol.m_id = protocol_id;
    protocol.m_name = "d9_tcp_pc_" + std::to_string(protocol_id);
    protocol.m_type = ProtocolType::kCustomTcp;
    protocol.m_projectId = project_id;
    protocol.m_status = ProtocolStatus::kValid;
    protocol.m_reqBodyType = ProtocolBodyType::kJson;
    protocol.m_respBodyType = ProtocolBodyType::kJson;
    protocol.m_reqBodyDataStatus = req_body.empty() ? 0 : 1;
    protocol.m_respBodyDataStatus = resp_body.empty() ? 0 : 1;
    protocol.m_reqCfg = req_cfg;
    protocol.m_respCfg = resp_cfg;
    protocol.m_reqBodyData = req_body;
    protocol.m_respBodyData = resp_body;
    protocol.m_isEndian = true;
    protocol.m_ctime = TimeStamp::Now();
    protocol.m_utime = TimeStamp::Now();
    return protocol;
}

static std::shared_ptr<CustomTcpProtocolItem> AddTcpRuntimeProtocol(
        const std::shared_ptr<CustomTcpProjectServer> &server,
        const kit_domain::Protocol &protocol)
{
    auto protocol_ptr = std::make_shared<kit_domain::Protocol>(protocol);
    auto item = ProtocolItemFactory::Create(protocol_ptr, server);
    if(!item)
    {
        return nullptr;
    }
    auto tcp_item = std::dynamic_pointer_cast<CustomTcpProtocolItem>(item);
    if(!tcp_item)
    {
        ADD_FAILURE() << "protocol item is not CustomTcpProtocolItem";
        return nullptr;
    }
    if(tcp_item->getId() != protocol.m_id)
    {
        ADD_FAILURE()
            << "CustomTcpProtocolItem lost protocol identity: expect id "
            << protocol.m_id << ", actual id " << tcp_item->getId();
        return nullptr;
    }

    auto add_result = server->AddProtocolItem(item);
    if(!add_result.ok())
    {
        ADD_FAILURE() << add_result.error.toMsg();
        return nullptr;
    }
    return tcp_item;
}


constexpr char kStartChar1 = 0x02;
constexpr char kStartChar2 = 0x3A;
constexpr char kEndChar1 = 0x0D;
constexpr char kEndChar2 = 0x0A;
constexpr char kFlag = '|';

static void ReqBuilderHelper3(std::vector<char>& req)
{

    std::string tmp; // 配置定长24

    tmp += kStartChar1;
    tmp += kStartChar2;

    // 功能码
    tmp += "01";
    tmp += kFlag;
    //设备类型
    tmp += "02";
    tmp += kFlag;
    //安检机站号
    tmp += "01";
    tmp += kFlag;
    //心跳编号
    tmp += "0000000001";
    tmp += kFlag;
    //结束符
    tmp += kEndChar1;
    tmp += kEndChar2;
    req.resize(tmp.size());
    
    std::copy(tmp.begin(), tmp.end(), req.begin());


    std::fstream f("/mnt/nfs/proxy_bin/req.bin", std::ios::out |std::ios::trunc | std::ios::binary);
    if(f.is_open())
    {
        printf("write req.bin ok!\n");
        f.write(req.data(), req.size());
    }
    f.flush();
    f.close();
}



// CustomTcpServerSuite.PatternDifferent
TEST_F(CustomTcpServerSuite, PatternDifferent) 
{
    TestCases1 cases[] =  {
        {
            "1. 头中有body长度",
            kit_domain::Project{
                .m_id = 1,
                .m_name = "test",
                .m_mode = ProjectMode::ServerMode,
                .m_protocolType = ProtocolType::kCustomTcp,
                .m_listenPort = 8888,
                .m_targetIp = "",
                .m_userId = 0,
                .m_status = ProjectStatus::kValid,
                .m_patternInfo = std::vector<char>(pattern_json_str1.begin(), pattern_json_str1.end()),
                .m_ctime = TimeStamp::Now(),
            },
            kit_domain::Protocol{
                .m_id = 1,
                .m_name = "test_pc",
                .m_type = ProtocolType::kCustomTcp,
                .m_projectId = 1,
                .m_status = ProtocolStatus::kValid,
                .m_reqBodyType = ProtocolBodyType::kJson,
                .m_respBodyType = ProtocolBodyType::kJson,
                .m_reqBodyDataStatus = 0,
                .m_respBodyDataStatus = 0,
                .m_reqCfg = nljson::parse(req_cfg1),
                .m_respCfg = nljson::parse(resp_cfg1),
                .m_reqBodyData = {},
                .m_respBodyData = std::vector<char>(resp_body1.begin(), resp_body1.end()),
                .m_isEndian = true,

                .m_ctime = TimeStamp::Now(),
                .m_utime = TimeStamp::Now(),
            },
            [](std::vector<char> &req) {
                ReqBuilderHelper1(req, nljson::parse(R"({"key1": "val1"})"));
            }
        },
        {
            "2. 头中有报文总长度-广州",
            kit_domain::Project{
                .m_id = 1,
                .m_name = "test",
                .m_mode = ProjectMode::ServerMode,
                .m_protocolType = ProtocolType::kCustomTcp,
                .m_listenPort = 8888,
                .m_targetIp = "",
                .m_userId = 0,
                .m_status = ProjectStatus::kValid,
                .m_patternInfo = std::vector<char>(pattern_json_str2_1.begin(), pattern_json_str2_1.end()),
                .m_ctime = TimeStamp::Now(),
            },
            kit_domain::Protocol{
                .m_id = 1,
                .m_name = "test_pc",
                .m_type = ProtocolType::kCustomTcp,
                .m_projectId = 1,
                .m_status = ProtocolStatus::kValid,
                .m_reqBodyType = ProtocolBodyType::kJson,
                .m_respBodyType = ProtocolBodyType::kJson,
                .m_reqBodyDataStatus = 0,
                .m_respBodyDataStatus = 0,
                // 临时兼容 D1-D6 后的 Protocol cfg 类型调整: m_reqCfg/m_respCfg 已是 JSON。
                // 后续正式清理旧测试数据时，可统一移除这些历史 vector<char> 初始化路径。
                .m_reqCfg = nljson::parse(req_cfg2_1),
                .m_respCfg = nljson::parse(resp_cfg2_1),
                .m_reqBodyData = {},
                .m_respBodyData = std::vector<char>(resp_body2_1.begin(), resp_body2_1.end()),
                .m_isEndian = true,
 
                .m_ctime = TimeStamp::Now(),
                .m_utime = TimeStamp::Now(),
            },
            [](std::vector<char> &req) {
                ReqBuilderHelper2_1(req, nljson::parse(R"({"key2": "val2"})"));
            }
        },
        {
            "3. 头中有报文总长度-郑州邮政",
            kit_domain::Project{
                .m_id = 1,
                .m_name = "test",
                .m_mode = ProjectMode::ServerMode,
                .m_protocolType = ProtocolType::kCustomTcp,
                .m_listenPort = 8888,
                .m_targetIp = "",
                .m_userId = 0,
                .m_status = ProjectStatus::kValid,
                .m_patternInfo = std::vector<char>(pattern_json_str2_2.begin(), pattern_json_str2_2.end()),
                .m_ctime = TimeStamp::Now(),
            },
            kit_domain::Protocol{
                .m_id = 1,
                .m_name = "test_pc",
                .m_type = ProtocolType::kCustomTcp,
                .m_projectId = 1,
                .m_status = ProtocolStatus::kValid,
                .m_reqBodyType = ProtocolBodyType::kBinary,
                .m_respBodyType = ProtocolBodyType::kBinary,
                .m_reqBodyDataStatus = 0,
                .m_respBodyDataStatus = 0,
                // 临时兼容 D1-D6 后的 Protocol cfg 类型调整: m_reqCfg/m_respCfg 已是 JSON。
                .m_reqCfg = nljson::parse(req_cfg2_2),
                .m_respCfg = nljson::parse(resp_cfg2_2),
                .m_reqBodyData = {},
                .m_respBodyData = MakeResp2_2(),
                .m_isEndian = true,
 
                .m_ctime = TimeStamp::Now(),
                .m_utime = TimeStamp::Now(),
            },
            [](std::vector<char> &req) {
                ReqBuilderHelper2_2(req);
            }
        },
        {
            "4. 头中无长度信息-峰复标准",
            kit_domain::Project{
                .m_id = 1,
                .m_name = "test",
                .m_mode = ProjectMode::ServerMode,
                .m_protocolType = ProtocolType::kCustomTcp,
                .m_listenPort = 8888,
                .m_targetIp = "",
                .m_userId = 0,
                .m_status = ProjectStatus::kValid,
                .m_patternInfo = std::vector<char>(pattern_json_str3.begin(), pattern_json_str3.end()),
                .m_ctime = TimeStamp::Now(),
            },
            kit_domain::Protocol{
                .m_id = 1,
                .m_name = "test_pc",
                .m_type = ProtocolType::kCustomTcp,
                .m_projectId = 1,
                .m_status = ProtocolStatus::kValid,
                .m_reqBodyType = ProtocolBodyType::kBinary,
                .m_respBodyType = ProtocolBodyType::kBinary,
                .m_reqBodyDataStatus = 0,
                .m_respBodyDataStatus = 0,
                // 临时兼容 D1-D6 后的 Protocol cfg 类型调整: m_reqCfg/m_respCfg 已是 JSON。
                .m_reqCfg = nljson::parse(req_cfg3),
                .m_respCfg = nljson::parse(resp_cfg3),
                .m_reqBodyData = {},
                .m_respBodyData = {},
                .m_isEndian = true,
 
                .m_ctime = TimeStamp::Now(),
                .m_utime = TimeStamp::Now(),
            },
            [](std::vector<char> &req) {
                ReqBuilderHelper3(req);
            }
        },
    };


    for(auto &c : cases)
    {

        CUSTOM_TEST_INFO_BEGIN(c.sub_name);

        auto server = server_start(c.pj);
        server->start();
        const InetAddress &server_addr = server->getBindAddr();

        auto pc = std::make_shared<kit_domain::Protocol>(c.pc);

        // 添加协议配置
        auto add_result = server->AddProtocolItem(ProtocolItemFactory::Create(pc, server));
        ASSERT_TRUE(add_result.ok()) << add_result.error.toMsg();

        auto ctx = std::make_shared<CustomTcpContext>(server.get());
        std::vector<char> req_data;
 
        // 构造请求
        c.reqBuild(req_data);

        // 执行测试 socket直接发送
        int res = tcp_send(req_data, ctx, server_addr);
        ASSERT_FALSE(res < 0);

        CUSTOM_TEST_INFO_END(c.sub_name);

        server->stop();
        usleep(100);
    }
}


/*
测试思路：
1. 构造一条 BODY_LENGTH_DEP 的完整自定义 TCP 报文。
2. 把报文拆成 3 段: header / body前半段 / body后半段。
3. 先喂 header，再喂不完整 body，最后补齐剩余 body。
4. 每一步都检查 parseRequest() 不会卡住，且状态机能从 kExpectBody 正确走到 kGotAll。

示意：
  完整报文 = [header 14B] + [body N B]
  第1次:     [header]
  第2次:     [body 1/2]
  第3次:     [body 2/2]
*/
TEST_F(CustomTcpServerSuite, buffer_partial_body_keeps_parser_state)
{
    const std::string pattern_json_partial_body = R"({
        "version": 2,
        "header_bytes": 14,
        "default_order": "big",
        "length_policy": "body_length",
        "fields": [
            {"name":"起始标识","byte_pos":0,"byte_len":4,"type":"UINT32","role":"start_magic","match":"H23232323"},
            {"name":"功能码","byte_pos":4,"byte_len":2,"type":"UINT16","role":"function_code"},
            {"name":"报文体长度","byte_pos":6,"byte_len":8,"type":"UINT64","role":"body_length"}
        ]
    })";
    const std::string req_cfg_partial_body = R"({"function_code":"H0100","fields":{}})";
    const std::string resp_cfg_partial_body = R"({"function_code":"H1080","fields":{}})";

    auto result = loop_pool_.acquire(1001);
    auto server = std::make_shared<CustomTcpProjectServer>(
        1001,
        std::vector<char>(pattern_json_partial_body.begin(), pattern_json_partial_body.end()),
        result.val,
        InetAddress(0, "127.0.0.1"));

    auto pc = std::make_shared<kit_domain::Protocol>(kit_domain::Protocol{
        .m_id = 1001,
        .m_name = "partial_body_test_pc",
        .m_type = ProtocolType::kCustomTcp,
        .m_projectId = 1001,
        .m_status = ProtocolStatus::kValid,
        .m_reqBodyType = ProtocolBodyType::kJson,
        .m_respBodyType = ProtocolBodyType::kJson,
        .m_reqBodyDataStatus = 0,
        .m_respBodyDataStatus = 0,
        .m_reqCfg = nljson::parse(req_cfg_partial_body),
        .m_respCfg = nljson::parse(resp_cfg_partial_body),
        .m_reqBodyData = {},
        .m_respBodyData = std::vector<char>(resp_body1.begin(), resp_body1.end()),
        .m_isEndian = true,
        .m_ctime = TimeStamp::Now(),
        .m_utime = TimeStamp::Now(),
    });

    auto item = ProtocolItemFactory::Create(pc, server);
    ASSERT_NE(item, nullptr);

    auto add_result = server->AddProtocolItem(item);
    ASSERT_TRUE(add_result.ok()) << add_result.error.toMsg();

    auto context = std::make_shared<CustomTcpContext>(server.get());
    const std::string expect_body = nljson::parse(R"({"key1": "val1"})").dump();
    std::vector<char> full_req;
    ReqBuilderHelperPartialBody(full_req, nljson::parse(R"({"key1": "val1"})"));

    constexpr size_t kHeaderLen = sizeof(PatternPartialBody);
    ASSERT_GT(full_req.size(), kHeaderLen);

    std::vector<char> header_chunk(full_req.begin(), full_req.begin() + kHeaderLen);
    std::vector<char> body_chunk(full_req.begin() + kHeaderLen, full_req.end());
    ASSERT_GT(body_chunk.size(), 1u);

    const size_t first_body_len = body_chunk.size() / 2;
    ASSERT_GT(first_body_len, 0u);
    ASSERT_LT(first_body_len, body_chunk.size());

    std::vector<char> body_part1(body_chunk.begin(), body_chunk.begin() + first_body_len);
    std::vector<char> body_part2(body_chunk.begin() + first_body_len, body_chunk.end());

    Buffer buf;
    const TimeStamp now = TimeStamp::Now();

    // 第1段: 只有 header, 应停在 kExpectBody, 不能误判为完成。
    buf.append(header_chunk.data(), header_chunk.size());
    EXPECT_TRUE(context->parseRequest(buf, now).ok());
    EXPECT_EQ(context->state(), CustomTcpContext::kExpectBody);
    EXPECT_FALSE(context->gotAll());
    EXPECT_TRUE(BodyString(context->request()).empty());

    // 第2段: body 还不完整, 状态仍应停留在 kExpectBody, 等待更多数据。
    buf.append(body_part1.data(), body_part1.size());
    EXPECT_TRUE(context->parseRequest(buf, now).ok());
    EXPECT_EQ(context->state(), CustomTcpContext::kExpectBody);
    EXPECT_FALSE(context->gotAll());
    EXPECT_TRUE(BodyString(context->request()).empty());

    // 第3段: 补齐剩余 body, 这时应该完整解析成功。
    buf.append(body_part2.data(), body_part2.size());
    EXPECT_TRUE(context->parseRequest(buf, now).ok());
    EXPECT_EQ(context->state(), CustomTcpContext::kGotAll);
    EXPECT_TRUE(context->gotAll());
    EXPECT_EQ(BodyString(context->request()), expect_body);
}

/*
测试思路：
1. 构造 BODY_LENGTH_DEP 的自定义 TCP 运行态服务。
2. 注册协议 3001，初始功能码为 H0100。
3. 调用 UpdateReqCfgProtocolItem 把功能码改成 H0200。
4. 断言新功能码能查到协议项，旧功能码查不到，说明 function code 索引完成替换。

示意：
  func_codes2ids_: H0100 -> pc3001
          |
          | UpdateReqCfg(function_code = H0200)
          v
  func_codes2ids_: H0200 -> pc3001

举例：
  客户端后续带 H0200 的报文应命中新协议项，带 H0100 的旧报文不应再命中。
*/
TEST_F(CustomTcpServerSuite, FunctionCodeUpdateMovesRuntimeIndex)
{
    auto project = MakeBodyLengthProject(9301);
    auto server = server_start(project);

    auto item = AddTcpRuntimeProtocol(server, MakeBodyLengthProtocol(3001, 9301, "H0100"));
    ASSERT_NE(item, nullptr);
    ASSERT_NE(server->findCBByFuncCode("H0100"), nullptr);
    EXPECT_EQ(server->findCBByFuncCode("H0200"), nullptr);

    nljson new_req_cfg = nljson::parse(req_cfg1);
    new_req_cfg["function_code"] = "H0200";

    auto update_result = server->UpdateReqCfgProtocolItem(3001, new_req_cfg);
    ASSERT_TRUE(update_result.ok()) << update_result.error.toMsg();

    EXPECT_EQ(server->findCBByFuncCode("H0100"), nullptr);
    ASSERT_NE(server->findCBByFuncCode("H0200"), nullptr);

    auto new_item_result = server->GetProtocolItem(3001);
    ASSERT_TRUE(new_item_result.ok()) << new_item_result.error.toMsg();
    auto new_item = std::dynamic_pointer_cast<CustomTcpProtocolItem>(new_item_result.val);
    ASSERT_NE(new_item, nullptr);
    EXPECT_EQ(new_item->getReqCfg().function_code, "H0200");
}

/*
测试思路：
1. 构造两个自定义 TCP 协议项：
   - 协议 3101: H0100
   - 协议 3102: H0200
2. 尝试把协议 3101 的功能码更新成 H0200。
3. 断言更新失败，并且 H0100 仍指向 3101，H0200 仍指向 3102。

示意：
  H0100 -> pc3101       H0200 -> pc3102
      \       Update pc3101 to H0200
       \______________X function code conflict

举例：
  两个协议不能共享同一个请求功能码，否则运行态无法根据报文头唯一定位协议项。
*/
TEST_F(CustomTcpServerSuite, FunctionCodeConflictPreservesOldIndex)
{
    auto project = MakeBodyLengthProject(9302);
    auto server = server_start(project);

    auto item1 = AddTcpRuntimeProtocol(server, MakeBodyLengthProtocol(3101, 9302, "H0100"));
    ASSERT_NE(item1, nullptr);
    auto item2 = AddTcpRuntimeProtocol(server, MakeBodyLengthProtocol(3102, 9302, "H0200"));
    ASSERT_NE(item2, nullptr);

    nljson new_req_cfg = nljson::parse(req_cfg1);
    new_req_cfg["function_code"] = "H0200";

    auto update_result = server->UpdateReqCfgProtocolItem(3101, new_req_cfg);
    ASSERT_FALSE(update_result.ok());
    EXPECT_EQ(update_result.error.toInt(), RuntimeError::kFuncCodeConflict);

    ASSERT_NE(server->findCBByFuncCode("H0100"), nullptr);
    ASSERT_NE(server->findCBByFuncCode("H0200"), nullptr);

    auto old_func_item_result = server->GetProtocolItem(3101);
    ASSERT_TRUE(old_func_item_result.ok()) << old_func_item_result.error.toMsg();
    auto old_func_item = std::dynamic_pointer_cast<CustomTcpProtocolItem>(old_func_item_result.val);
    ASSERT_NE(old_func_item, nullptr);
    EXPECT_EQ(old_func_item->getReqCfg().function_code, "H0100");

    auto conflict_func_item_result = server->GetProtocolItem(3102);
    ASSERT_TRUE(conflict_func_item_result.ok()) << conflict_func_item_result.error.toMsg();
    auto conflict_func_item = std::dynamic_pointer_cast<CustomTcpProtocolItem>(conflict_func_item_result.val);
    ASSERT_NE(conflict_func_item, nullptr);
    EXPECT_EQ(conflict_func_item->getReqCfg().function_code, "H0200");
}

/*
测试思路：
1. 构造 TCP 协议项，记录 req cfg 的功能码和 req/resp body view。
2. 只调用 UpdateReqBodyProtocolItem 替换请求 body。
3. 断言 function code 索引仍能命中原协议，req cfg 不变，只有 req body view 被替换。

示意：
  H0100 -> pc3201 + [req body old] + [resp body B]
                    |
                    | UpdateReqBody(new)
                    v
  H0100 -> pc3201 + [req body new] + [resp body B]

举例：
  上传新的大请求体样例时，不应该改动 function code 索引或 TCP 头部字段配置。
*/
TEST_F(CustomTcpServerSuite, ReqBodyUpdateKeepsFunctionCodeIndexAndCfg)
{
    auto project = MakeBodyLengthProject(9303);
    auto server = server_start(project);

    auto item = AddTcpRuntimeProtocol(
        server,
        MakeBodyLengthProtocol(3201, 9303, "H0100", {'o', 'l', 'd'}, {'r', 'e', 's', 'p'}));
    ASSERT_NE(item, nullptr);

    const auto before_req_cfg = item->getReqCfg();
    const auto before_req_body = item->getReqBodyView();
    const auto before_resp_body = item->getRespBodyView();

    const std::vector<char> new_body{'n', 'e', 'w', '-', 't', 'c', 'p', '-', 'b', 'o', 'd', 'y'};
    auto update_result = server->UpdateReqBodyProtocolItem(
        3201,
        ProtocolBodyType::kJson,
        new_body);
    ASSERT_TRUE(update_result.ok()) << update_result.error.toMsg();

    ASSERT_NE(server->findCBByFuncCode("H0100"), nullptr);
    auto after_result = server->GetProtocolItem(3201);
    ASSERT_TRUE(after_result.ok()) << after_result.error.toMsg();
    auto after = std::dynamic_pointer_cast<CustomTcpProtocolItem>(after_result.val);
    ASSERT_NE(after, nullptr);
    EXPECT_EQ(after->getId(), 3201);
    EXPECT_EQ(after->getReqCfg().function_code, before_req_cfg.function_code);
    EXPECT_EQ(after->getReqCfg().field_values_by_byte_pos.size(), before_req_cfg.field_values_by_byte_pos.size());
    EXPECT_NE(after->getReqBodyView().body_data, before_req_body.body_data);
    EXPECT_EQ(*after->getReqBodyView().body_data, new_body);
    EXPECT_EQ(after->getRespBodyView().body_data, before_resp_body.body_data);
}

/*
测试思路：
1. 构造 BODY_LENGTH_DEP 的 CustomTcpProjectServer，并在 start 前注入 observe callback。
2. 注册功能码 H0100 的协议项后，使用真实 loopback TCP 请求命中该功能码。
3. 断言普通 TCP 客户端收发路径成功，同时 Publisher/Hub 收到 scope=protocol/result=matched 的 record。
4. record 的 request head/body 和 response head/body 应来自真实解析出的 CustomTcpMessage。

示意：
  request: [magic][len][seq][H0100][body_len][ts] + {"key1":"val1"}
       |
       v
  record.request.meta.function_code = H0100
  record.response.meta.function_code = H1080

举例：
  这个用例固定“运行态真实收到的 body”，不是协议项配置中的 req_body 样例。
*/
TEST_F(CustomTcpServerSuite, RuntimePublishesMatchedObservationThroughPublisherHub)
{
    auto project = MakeBodyLengthProject(9401);
    auto server = server_start(project);
    CustomTcpInteractionPipeline pipeline(9401, 4001, true);
    server->setObserveCallback(pipeline.Callback());

    auto item = AddTcpRuntimeProtocol(
        server,
        MakeBodyLengthProtocol(4001, 9401, "H0100", {}, std::vector<char>(resp_body1.begin(), resp_body1.end())));
    ASSERT_NE(item, nullptr);
    pipeline.Subscribe(9401, 4001, item->cache(), server->cache(), true);

    server->start();
    const InetAddress &server_addr = server->getBindAddr();

    const nljson request_root = nljson::parse(R"({"key1":"val1"})");
    const std::string expected_request_body = request_root.dump();
    std::vector<char> req_data;
    ReqBuilderHelper1(req_data, request_root);

    ASSERT_EQ(tcp_send(req_data, nullptr, server_addr), 0);
    ASSERT_TRUE(pipeline.collector->WaitForRecordCount(1));
    EXPECT_TRUE(server->stop());

    const auto records = pipeline.collector->Records();
    ASSERT_EQ(records.size(), 1U);
    const auto &record = records.front();

    EXPECT_EQ(record.seq, 1U);
    EXPECT_EQ(record.scope, InteractionScope::kProtocol);
    EXPECT_EQ(record.project_id, 9401);
    EXPECT_EQ(record.protocol_id, 4001);
    EXPECT_EQ(record.protocol_type, ProtocolType::kCustomTcp);
    EXPECT_EQ(record.result, InteractionResult::kMatched);
    EXPECT_EQ(record.error_message, "service handle ok");

    EXPECT_EQ(record.request.meta["function_code"], "H0100");
    EXPECT_EQ(record.request.meta["body_size"], expected_request_body.size());
    EXPECT_NE(record.request.head_text.find("H0100"), std::string::npos);
    EXPECT_EQ(record.request.body.kind, InteractionPayloadKind::kJson);
    EXPECT_EQ(record.request.body.expect_kind, InteractionPayloadKind::kJson);
    EXPECT_EQ(record.request.body.text, expected_request_body);

    EXPECT_EQ(record.response.meta["function_code"], "H1080");
    EXPECT_FALSE(record.response.head_text.empty());
    EXPECT_EQ(record.response.body.kind, InteractionPayloadKind::kJson);
    EXPECT_EQ(record.response.body.expect_kind, InteractionPayloadKind::kJson);
    EXPECT_EQ(record.response.body.text, resp_body1);
}

/*
测试思路：
1. 运行态只注册 H0100 协议项。
2. 客户端发送同一格式但功能码为 H0200 的完整报文。
3. 当前 CustomTcpParseStatus::kFuncCodeNotFound 映射为 InteractionResult::kRouteNotFound，测试固定现有实现。
4. 因为解析在功能码索引处失败，当前实现按 project notice 推送，并用 raw_packet 保存原始请求。

示意：
  runtime index: H0100 -> pc4002
  request:       H0200
        |
        v
  record.scope=project, protocol_id=0, result=route_not_found

举例：
  如果后续生产代码把该分支改成 protocol_not_found，应同步调整这个用例的 result 断言。
*/
TEST_F(CustomTcpServerSuite, RuntimePublishesFunctionCodeNotFoundProjectNotice)
{
    auto project = MakeBodyLengthProject(9402);
    auto server = server_start(project);
    CustomTcpInteractionPipeline pipeline(9402, 4002, true);
    server->setObserveCallback(pipeline.Callback());

    auto item = AddTcpRuntimeProtocol(server, MakeBodyLengthProtocol(4002, 9402, "H0100"));
    ASSERT_NE(item, nullptr);
    pipeline.Subscribe(9402, 4002, item->cache(), server->cache(), true);

    server->start();
    const InetAddress &server_addr = server->getBindAddr();

    std::vector<char> req_data;
    ReqBuilderHelper1(req_data, nljson::parse(R"({"key1":"val1"})"));
    PatchPattern1FunctionCode(req_data, 0x02, 0x00);

    ASSERT_EQ(tcp_send_no_response_required(req_data, server_addr), 0);
    ASSERT_TRUE(pipeline.collector->WaitForRecordCount(1));
    EXPECT_TRUE(server->stop());

    const auto records = pipeline.collector->Records();
    ASSERT_EQ(records.size(), 1U);
    const auto &record = records.front();

    EXPECT_EQ(record.scope, InteractionScope::kProject);
    EXPECT_EQ(record.project_id, 9402);
    EXPECT_EQ(record.protocol_id, 0);
    EXPECT_EQ(record.protocol_type, ProtocolType::kCustomTcp);
    EXPECT_EQ(record.result, InteractionResult::kRouteNotFound);
    EXPECT_EQ(record.error_message, "func code not found");
    ASSERT_TRUE(record.request.raw_packet.has_value());
    EXPECT_GT(record.request.raw_packet->size, 0U);
    EXPECT_NE(record.request.raw_packet->raw_hex.find("H23 23 23 23"), std::string::npos);
}

/*
测试思路：
1. 构造一条长度完整但起始魔数错误的 CustomTcp 报文。
2. 解析器应在 header 校验阶段失败，服务端错误分支直接关闭连接，不发送业务响应。
3. 实时链路应发布 scope=project/result=parse_error/protocol_id=0。
4. request.raw_packet 应记录收到的原始 bytes 前缀，便于前端定位是哪类坏包。

示意：
  valid magic: H23232323
  request:     H24232323...
       |
       v
  record.result=parse_error, raw_packet.raw_hex 包含 H24 23 23 23
*/
TEST_F(CustomTcpServerSuite, RuntimePublishesParseErrorRawPacket)
{
    auto project = MakeBodyLengthProject(9403);
    auto server = server_start(project);
    CustomTcpInteractionPipeline pipeline(9403, 4003, true);
    server->setObserveCallback(pipeline.Callback());
    pipeline.Subscribe(9403, 4003, nullptr, server->cache(), true);
    server->start();
    const InetAddress &server_addr = server->getBindAddr();

    std::vector<char> req_data;
    ReqBuilderHelper1(req_data, nljson::parse(R"({"key1":"val1"})"));
    ASSERT_FALSE(req_data.empty());
    req_data[0] = static_cast<char>(0x24);

    ASSERT_EQ(tcp_send_no_response_required(req_data, server_addr), 0);
    ASSERT_TRUE(pipeline.collector->WaitForRecordCount(1));
    EXPECT_TRUE(server->stop());

    const auto records = pipeline.collector->Records();
    ASSERT_EQ(records.size(), 1U);
    const auto &record = records.front();

    EXPECT_EQ(record.scope, InteractionScope::kProject);
    EXPECT_EQ(record.project_id, 9403);
    EXPECT_EQ(record.protocol_id, 0);
    EXPECT_EQ(record.protocol_type, ProtocolType::kCustomTcp);
    EXPECT_EQ(record.result, InteractionResult::kParseError);
    EXPECT_EQ(record.error_message, "parse error");
    ASSERT_TRUE(record.request.raw_packet.has_value());
    EXPECT_GT(record.request.raw_packet->size, 0U);
    EXPECT_NE(record.request.raw_packet->raw_hex.find("H24 23 23 23"), std::string::npos);
}

/*
测试思路：
1. 先通过公开 helper 注册一个正常 H0100 协议项，确保请求解析和功能码索引都可命中。
2. 再通过 CustomTcpProtocolItem::setRespCfg(const CustomTcpItemCfg&) 注入字段长度不匹配的响应配置，模拟运行态响应配置损坏。
3. 请求命中协议项后，CustomTcpProcess 组装响应失败，应发布 scope=protocol/result=serialize_error。
4. 错误分支不向 TCP 客户端写响应，但实时 record 仍应保留真实 request body 和协议项归属。

示意：
  resp field byte_pos=4 期望 4 字节
       |
       +-- 测试注入 1 字节
       v
  assembleMessageFromCfg=false -> record.result=serialize_error

举例：
  该用例覆盖“响应序列化失败不冒充 matched”的运行时边界。
*/
TEST_F(CustomTcpServerSuite, RuntimePublishesSerializeErrorForBadResponseConfig)
{
    auto project = MakeBodyLengthProject(9404);
    auto server = server_start(project);
    CustomTcpInteractionPipeline pipeline(9404, 4004, true);
    server->setObserveCallback(pipeline.Callback());

    auto item = AddTcpRuntimeProtocol(server, MakeBodyLengthProtocol(4004, 9404, "H0100"));
    ASSERT_NE(item, nullptr);
    pipeline.Subscribe(9404, 4004, item->cache(), server->cache(), true);

    auto broken_resp_cfg = item->getRespCfg();
    broken_resp_cfg.field_values_by_byte_pos[4] = std::vector<uint8_t>{0x00};
    item->setRespCfg(broken_resp_cfg);

    server->start();
    const InetAddress &server_addr = server->getBindAddr();

    const nljson request_root = nljson::parse(R"({"key1":"val1"})");
    const std::string expected_request_body = request_root.dump();
    std::vector<char> req_data;
    ReqBuilderHelper1(req_data, request_root);

    ASSERT_EQ(tcp_send_no_response_required(req_data, server_addr), 0);
    ASSERT_TRUE(pipeline.collector->WaitForRecordCount(1));
    EXPECT_TRUE(server->stop());

    const auto records = pipeline.collector->Records();
    ASSERT_EQ(records.size(), 1U);
    const auto &record = records.front();

    EXPECT_EQ(record.scope, InteractionScope::kProtocol);
    EXPECT_EQ(record.project_id, 9404);
    EXPECT_EQ(record.protocol_id, 4004);
    EXPECT_EQ(record.protocol_type, ProtocolType::kCustomTcp);
    EXPECT_EQ(record.result, InteractionResult::kSerializeError);
    EXPECT_EQ(record.error_message, "serialize error");
    EXPECT_EQ(record.request.meta["function_code"], "H0100");
    EXPECT_EQ(record.request.body.kind, InteractionPayloadKind::kJson);
    EXPECT_EQ(record.request.body.text, expected_request_body);
}



TEST_F(CustomTcpServerSuite, DISABLED_ClientSend)
{
    TestCases2 cases[] = {
        {
            "1. 正常组装测试",
            [](std::vector<char> &req) {
                ReqBuilderHelper1(req, nljson::parse(R"({"key1": "val1"})"));
            },
            0
        }

    };

    for(auto &c : cases)
    {
        CUSTOM_TEST_INFO_BEGIN(c.sub_name);

        // 临时兼容: 该用例原先使用默认 InetAddress 裸连，自动化运行时没有目标服务会阻塞。
        // 在 net_data_converter 字段宽度/字节序接口正式重构前，先启动最小本地服务，
        // 让用例稳定覆盖客户端发送到自定义 TCP 服务的链路。
        kit_domain::Project pj{
            .m_id = 1,
            .m_name = "test",
            .m_mode = ProjectMode::ServerMode,
            .m_protocolType = ProtocolType::kCustomTcp,
            .m_listenPort = 8888,
            .m_targetIp = "",
            .m_userId = 0,
            .m_status = ProjectStatus::kValid,
            .m_patternInfo = std::vector<char>(pattern_json_str1.begin(), pattern_json_str1.end()),
            .m_ctime = TimeStamp::Now(),
        };
        kit_domain::Protocol pc{
            .m_id = 1,
            .m_name = "test_pc",
            .m_type = ProtocolType::kCustomTcp,
            .m_projectId = 1,
            .m_status = ProtocolStatus::kValid,
            .m_reqBodyType = ProtocolBodyType::kJson,
            .m_respBodyType = ProtocolBodyType::kJson,
            .m_reqBodyDataStatus = 0,
            .m_respBodyDataStatus = 0,
            .m_reqCfg = nljson::parse(req_cfg1),
            .m_respCfg = nljson::parse(resp_cfg1),
            .m_reqBodyData = {},
            .m_respBodyData = std::vector<char>(resp_body1.begin(), resp_body1.end()),
            .m_isEndian = true,
            .m_ctime = TimeStamp::Now(),
            .m_utime = TimeStamp::Now(),
        };

        auto server = server_start(pj);
        server->start();
        const InetAddress &addr = server->getBindAddr();

        auto protocol = std::make_shared<kit_domain::Protocol>(pc);
        auto add_result = server->AddProtocolItem(ProtocolItemFactory::Create(protocol, server));
        ASSERT_TRUE(add_result.ok()) << add_result.error.toMsg();

        std::vector<char> req_data;

        // 构造请求
        c.reqBuild(req_data);

        // 执行测试 socket直接发送
        int res = tcp_send(req_data, nullptr, addr);

        ASSERT_EQ(res, c.wantRes);

        CUSTOM_TEST_INFO_END(c.sub_name);

        server->stop();
        usleep(100);
    }

}
