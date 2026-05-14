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
#include "net/net_data_converter.h"

#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <string>

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
using PatternFieldPtr = std::shared_ptr<CustomTcpPatternFieldBase>;

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




class CustomTcpServerSuite : public ::testing::Test
{
protected:
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
        // 创建自定义TCP服务器必须带解析格式，否则无法解析。
        // R1 之后 runtime loop 和 TcpServer 生命周期由 CustomTcpProjectServer 自己持有。
        return std::make_shared<CustomTcpProjectServer>(p.m_id, p.m_patternType, p.m_patternInfo);
    }
};


static int tcp_send(const std::vector<char>& input, CustomTcpContextPtr ctx, const InetAddress &server_addr) 
{
    int client_fd;
    // struct sockaddr_in server_addr;

    // 1. 创建socket
    if ((client_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) 
    {
        TEST_ERROR() << "socket creation failed " 
                    << errno << ":" 
                    << strerror(errno) << std::endl;
        return -1;
    }
    
    // 2. 配置服务器地址
    // server_addr.sin_family = AF_INET;
    // server_addr.sin_port = htons(SERVER_PORT);
    
    // // 将IP地址从字符串转换为网络地址
    // if (inet_pton(AF_INET, SERVER_IP, &server_addr.sin_addr) <= 0) 
    // {
    //     TEST_ERROR() << "invalid address/address not supported " 
    //                 << errno << ":" 
    //                 << strerror(errno) << std::endl;
    //     return -1;
    // }
    
    // 3. 连接到服务器
    if (connect(client_fd, (struct sockaddr *)server_addr.getSockAddr(), sizeof(struct sockaddr)) < 0) {
        TEST_ERROR() << "connection failed " 
                    << errno << ":" 
                    << strerror(errno) << std::endl;
        return -1;
    }


    // 4. 发送数据
    send(client_fd, input.data(), input.size(), 0);


    // 5. 接收响应
    Buffer buf;
    int32_t savedErrno = 0;
    int valread = buf.readFd(client_fd, &savedErrno);
    if(valread <= 0)
    {
        TEST_ERROR() << "read failed " 
        << savedErrno << ":" 
        << strerror(savedErrno) << std::endl;
        close(client_fd);

        return -1;
    }
    const auto& resp_data = buf.resetAllAsData();
    
    std::fstream f("/mnt/nfs/proxy_bin/resp.bin", std::ios::out |std::ios::trunc | std::ios::binary);
    if(f.is_open())
    {
        printf("write resp.bin ok!\n");
        f.write(resp_data.data(), resp_data.size());
        f.flush();
        f.close();
    }


    // (void)ctx->parseResponse(buf, TimeStamp());

    // 6. 关闭连接
    close(client_fd);
    
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
    project.m_protocolType = ProtocolType::CUSTOM_TCP_PROTOCOL;
    project.m_listenPort = 8888;
    project.m_targetIp = "";
    project.m_userId = 0;
    project.m_status = ProjectStatus::ON_STATUS;
    project.m_active = ProjectStatus::ON_STATUS;
    project.m_patternType = CustomTcpPatternType::BODY_LENGTH_DEP;
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
    req_cfg["function_code_filed_value"] = func_code;

    nljson resp_cfg = nljson::parse(resp_cfg1);
    resp_cfg["function_code_filed_value"] = func_code == "H0100" ? "H1080" : "H2080";

    kit_domain::Protocol protocol;
    protocol.m_id = protocol_id;
    protocol.m_name = "d9_tcp_pc_" + std::to_string(protocol_id);
    protocol.m_type = ProtocolType::CUSTOM_TCP_PROTOCOL;
    protocol.m_projectId = project_id;
    protocol.m_status = ProtocolStatus::ACTIVE;
    protocol.m_reqBodyType = ProtocolBodyType::JSON_BODY_TYPE;
    protocol.m_respBodyType = ProtocolBodyType::JSON_BODY_TYPE;
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
                .m_protocolType = ProtocolType::CUSTOM_TCP_PROTOCOL,
                .m_listenPort = 8888,
                .m_targetIp = "",
                .m_userId = 0,
                .m_status = ProjectStatus::ON_STATUS,
                .m_patternType = CustomTcpPatternType::BODY_LENGTH_DEP,
                .m_patternInfo = std::vector<char>(pattern_json_str1.begin(), pattern_json_str1.end()),
                TimeStamp::Now(),
            },
            kit_domain::Protocol{
                .m_id = 1,
                .m_name = "test_pc",
                .m_type = ProtocolType::CUSTOM_TCP_PROTOCOL,
                .m_projectId = 1,
                .m_status = ProtocolStatus::ACTIVE,
                .m_reqBodyType = ProtocolBodyType::JSON_BODY_TYPE,
                .m_respBodyType = ProtocolBodyType::JSON_BODY_TYPE,
                .m_reqBodyDataStatus = 0,
                .m_respBodyDataStatus = 0,
                .m_reqCfg = nljson::parse(req_cfg1),
                .m_respCfg = nljson::parse(resp_cfg1),
                .m_reqBodyData = {},
                .m_respBodyData = std::move(std::vector<char>(resp_body1.begin(), resp_body1.end())),
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
                .m_protocolType = ProtocolType::CUSTOM_TCP_PROTOCOL,
                .m_listenPort = 8888,
                .m_targetIp = "",
                .m_userId = 0,
                .m_status = ProjectStatus::ON_STATUS,
                .m_patternType = CustomTcpPatternType::TOTAL_LENGTH_DEP,
                .m_patternInfo = std::vector<char>(pattern_json_str2_1.begin(), pattern_json_str2_1.end()),
                TimeStamp::Now(),
            },
            kit_domain::Protocol{
                .m_id = 1,
                .m_name = "test_pc",
                .m_type = ProtocolType::CUSTOM_TCP_PROTOCOL,
                .m_projectId = 1,
                .m_status = ProtocolStatus::ACTIVE,
                .m_reqBodyType = ProtocolBodyType::JSON_BODY_TYPE,
                .m_respBodyType = ProtocolBodyType::JSON_BODY_TYPE,
                .m_reqBodyDataStatus = 0,
                .m_respBodyDataStatus = 0,
                // 临时兼容 D1-D6 后的 Protocol cfg 类型调整: m_reqCfg/m_respCfg 已是 JSON。
                // 后续正式清理旧测试数据时，可统一移除这些历史 vector<char> 初始化路径。
                .m_reqCfg = nljson::parse(req_cfg2_1),
                .m_respCfg = nljson::parse(resp_cfg2_1),
                .m_reqBodyData = {},
                .m_respBodyData = std::move(std::vector<char>(resp_body2_1.begin(), resp_body2_1.end())),
                .m_isEndian = true,
 
                .m_ctime = TimeStamp::Now(),
                .m_utime = TimeStamp::Now(),
            },
            [](std::vector<char> &req) {
                ReqBuilderHelper1(req, nljson::parse(R"({"key2": "val2"})"));
            }
        },
        {
            "3. 头中有报文总长度-郑州邮政",
            kit_domain::Project{
                .m_id = 1,
                .m_name = "test",
                .m_mode = ProjectMode::ServerMode,
                .m_protocolType = ProtocolType::CUSTOM_TCP_PROTOCOL,
                .m_listenPort = 8888,
                .m_targetIp = "",
                .m_userId = 0,
                .m_status = ProjectStatus::ON_STATUS,
                .m_patternType = CustomTcpPatternType::TOTAL_LENGTH_DEP,
                .m_patternInfo = std::vector<char>(pattern_json_str2_2.begin(), pattern_json_str2_2.end()),
                TimeStamp::Now(),
            },
            kit_domain::Protocol{
                .m_id = 1,
                .m_name = "test_pc",
                .m_type = ProtocolType::CUSTOM_TCP_PROTOCOL,
                .m_projectId = 1,
                .m_status = ProtocolStatus::ACTIVE,
                .m_reqBodyType = ProtocolBodyType::BINARY_BODY_TYPE,
                .m_respBodyType = ProtocolBodyType::BINARY_BODY_TYPE,
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
                .m_protocolType = ProtocolType::CUSTOM_TCP_PROTOCOL,
                .m_listenPort = 8888,
                .m_targetIp = "",
                .m_userId = 0,
                .m_status = ProjectStatus::ON_STATUS,
                .m_patternType = CustomTcpPatternType::NO_LENGTH_DEP,
                .m_patternInfo = std::vector<char>(pattern_json_str3.begin(), pattern_json_str3.end()),
                TimeStamp::Now(),
            },
            kit_domain::Protocol{
                .m_id = 1,
                .m_name = "test_pc",
                .m_type = ProtocolType::CUSTOM_TCP_PROTOCOL,
                .m_projectId = 1,
                .m_status = ProtocolStatus::ACTIVE,
                .m_reqBodyType = ProtocolBodyType::BINARY_BODY_TYPE,
                .m_respBodyType = ProtocolBodyType::BINARY_BODY_TYPE,
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
        "least_byte_len": 14,
        "special_fields": {
            "start_magic_num_field": {
                "name": "起始标识",
                "idx": 0,
                "byte_pos": 0,
                "byte_len": 4,
                "type": "INT32",
                "value": "H23232323"
            },
            "function_code_field": {
                "name": "功能码",
                "idx": 3,
                "byte_pos": 4,
                "byte_len": 2,
                "type": "UINT16",
                "value": ""
            },
            "body_length_field": {
                "name": "报文体长度",
                "idx": 4,
                "byte_pos": 6,
                "byte_len": 8,
                "type": "UINT64",
                "value": ""
            }
        }
    })";
    const std::string req_cfg_partial_body = R"({"function_code_filed_value":"H0100","common_fields":[]})";
    const std::string resp_cfg_partial_body = R"({"function_code_filed_value":"H1080","common_fields":[]})";

    auto server = std::make_shared<CustomTcpProjectServer>(
        1001,
        CustomTcpPatternType::BODY_LENGTH_DEP,
        std::vector<char>(pattern_json_partial_body.begin(), pattern_json_partial_body.end()));

    auto pc = std::make_shared<kit_domain::Protocol>(kit_domain::Protocol{
        .m_id = 1001,
        .m_name = "partial_body_test_pc",
        .m_type = ProtocolType::CUSTOM_TCP_PROTOCOL,
        .m_projectId = 1001,
        .m_status = ProtocolStatus::ACTIVE,
        .m_reqBodyType = ProtocolBodyType::JSON_BODY_TYPE,
        .m_respBodyType = ProtocolBodyType::JSON_BODY_TYPE,
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
    EXPECT_TRUE(context->parseRequest(buf, now));
    EXPECT_EQ(context->state(), CustomTcpContext::kExpectBody);
    EXPECT_FALSE(context->gotAll());
    EXPECT_TRUE(context->request()->body().toString().empty());

    // 第2段: body 还不完整, 状态仍应停留在 kExpectBody, 等待更多数据。
    buf.append(body_part1.data(), body_part1.size());
    EXPECT_TRUE(context->parseRequest(buf, now));
    EXPECT_EQ(context->state(), CustomTcpContext::kExpectBody);
    EXPECT_FALSE(context->gotAll());
    EXPECT_TRUE(context->request()->body().toString().empty());

    // 第3段: 补齐剩余 body, 这时应该完整解析成功。
    buf.append(body_part2.data(), body_part2.size());
    EXPECT_TRUE(context->parseRequest(buf, now));
    EXPECT_EQ(context->state(), CustomTcpContext::kGotAll);
    EXPECT_TRUE(context->gotAll());
    EXPECT_EQ(context->request()->body().toString(), expect_body);
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
          | UpdateReqCfg(function_code_filed_value = H0200)
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
    ASSERT_NE(server->findByFuncCode("H0100"), nullptr);
    EXPECT_EQ(server->findByFuncCode("H0100")->getId(), 3001);
    EXPECT_EQ(server->findByFuncCode("H0200"), nullptr);

    nljson new_req_cfg = nljson::parse(req_cfg1);
    new_req_cfg["function_code_filed_value"] = "H0200";

    auto update_result = server->UpdateReqCfgProtocolItem(3001, new_req_cfg);
    ASSERT_TRUE(update_result.ok()) << update_result.error.toMsg();

    EXPECT_EQ(server->findByFuncCode("H0100"), nullptr);
    auto new_item = server->findByFuncCode("H0200");
    ASSERT_NE(new_item, nullptr);
    EXPECT_EQ(new_item->getId(), 3001);
    EXPECT_EQ(new_item->getReqCfg().function_code_filed_value, "H0200");
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
    new_req_cfg["function_code_filed_value"] = "H0200";

    auto update_result = server->UpdateReqCfgProtocolItem(3101, new_req_cfg);
    ASSERT_FALSE(update_result.ok());
    EXPECT_EQ(update_result.error.toInt(), RuntimeError::kFuncCodeConflict);

    auto old_func_item = server->findByFuncCode("H0100");
    ASSERT_NE(old_func_item, nullptr);
    EXPECT_EQ(old_func_item->getId(), 3101);
    EXPECT_EQ(old_func_item->getReqCfg().function_code_filed_value, "H0100");

    auto conflict_func_item = server->findByFuncCode("H0200");
    ASSERT_NE(conflict_func_item, nullptr);
    EXPECT_EQ(conflict_func_item->getId(), 3102);
    EXPECT_EQ(conflict_func_item->getReqCfg().function_code_filed_value, "H0200");
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
        ProtocolBodyType::JSON_BODY_TYPE,
        new_body);
    ASSERT_TRUE(update_result.ok()) << update_result.error.toMsg();

    auto after = server->findByFuncCode("H0100");
    ASSERT_NE(after, nullptr);
    EXPECT_EQ(after->getId(), 3201);
    EXPECT_EQ(after->getReqCfg().function_code_filed_value, before_req_cfg.function_code_filed_value);
    EXPECT_EQ(after->getReqCfg().headers.size(), before_req_cfg.headers.size());
    EXPECT_NE(after->getReqBodyView().body_data, before_req_body.body_data);
    EXPECT_EQ(*after->getReqBodyView().body_data, new_body);
    EXPECT_EQ(after->getRespBodyView().body_data, before_resp_body.body_data);
}



TEST_F(CustomTcpServerSuite, ClientSend)
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
            .m_protocolType = ProtocolType::CUSTOM_TCP_PROTOCOL,
            .m_listenPort = 8888,
            .m_targetIp = "",
            .m_userId = 0,
            .m_status = ProjectStatus::ON_STATUS,
            .m_patternType = CustomTcpPatternType::BODY_LENGTH_DEP,
            .m_patternInfo = std::vector<char>(pattern_json_str1.begin(), pattern_json_str1.end()),
            TimeStamp::Now(),
        };
        kit_domain::Protocol pc{
            .m_id = 1,
            .m_name = "test_pc",
            .m_type = ProtocolType::CUSTOM_TCP_PROTOCOL,
            .m_projectId = 1,
            .m_status = ProtocolStatus::ACTIVE,
            .m_reqBodyType = ProtocolBodyType::JSON_BODY_TYPE,
            .m_respBodyType = ProtocolBodyType::JSON_BODY_TYPE,
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
