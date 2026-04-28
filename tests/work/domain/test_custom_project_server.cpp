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
#include "net/tcp_connection.h"
#include "net/tcp_server.h"
#include "base/thread.h"
#include "base/event_loop_thread.h"
#include "net/event_loop.h"
#include "domain/project_server.h"
#include "domain/project.h"
#include "domain/protocol.h"
#include "domain/protocol_item.h"
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


#define SERVER_PORT (8888)
#define SERVER_IP   "10.6.170.65"

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

        // 默认绑定到eth0 IP上
        const std::string& local_ip = InetAddress::GetInterfaceIpv4("eth0").toIp();
    
        const InetAddress& address = InetAddress(p.m_listenPort, local_ip);
    
        std::string server_name = "pj";
        server_name += std::to_string(p.m_id);
        server_name += "_tcp_server";

        auto tcp_server = std::make_shared<TcpServer>(
            &loop_, address, server_name, TcpServer::KReusePort
        );
        tcp_server->setThreadNum(0); // 使用单线程模式
        tcp_server->start();

        Thread t([this](){
            loop_.loop();
        }, std::to_string(p.m_id) + "test_loop");
        t.start();

        // 创建自定义TCP服务器 必须带解析格式  否则无法解析
        return std::make_shared<CustomTcpProjectServer>(p.m_id, tcp_server, p.m_patternType, p.m_patternInfo);

    }
    EventLoop loop_; // 生命周期是最长的
};


static int tcp_send(const std::vector<char>& input, CustomTcpContextPtr ctx) 
{
    int client_fd;
    struct sockaddr_in server_addr;

    // 1. 创建socket
    if ((client_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) 
    {
        TEST_ERROR() << "socket creation failed " 
                    << errno << ":" 
                    << strerror(errno) << std::endl;
        return -1;
    }
    
    // 2. 配置服务器地址
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(SERVER_PORT);
    
    // 将IP地址从字符串转换为网络地址
    if (inet_pton(AF_INET, SERVER_IP, &server_addr.sin_addr) <= 0) 
    {
        TEST_ERROR() << "invalid address/address not supported " 
                    << errno << ":" 
                    << strerror(errno) << std::endl;
        return -1;
    }
    
    // 3. 连接到服务器
    if (connect(client_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
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

    std::fstream f("/mnt/nfs/proxy_bin/req.bin", std::ios::out |std::ios::trunc | std::ios::binary);
    if(f.is_open())
    {
        printf("write req.bin ok!\n");
        f.write(req.data(), req.size());
    }
    f.flush();
    f.close();
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




TEST_F(CustomTcpServerSuite, DISABLED_PatternDifferent) 
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
                .m_reqCfg = std::move(std::vector<char>(req_cfg2_1.begin(), req_cfg2_1.end())),
                .m_respCfg = std::move(std::vector<char>(resp_cfg2_1.begin(), resp_cfg2_1.end())),
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
                .m_reqCfg = std::move(std::vector<char>(req_cfg2_2.begin(), req_cfg2_2.end())),
                .m_respCfg = std::move(std::vector<char>(resp_cfg2_2.begin(), resp_cfg2_2.end())),
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
                .m_reqCfg = std::move(std::vector<char>(req_cfg3.begin(), req_cfg3.end())),
                .m_respCfg = std::move(std::vector<char>(resp_cfg3.begin(), resp_cfg3.end())),
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
        auto pc = std::make_shared<kit_domain::Protocol>(c.pc);
        // 添加协议配置
        server->AddProtocolItem(ProtocolItemFactory::Create(pc, server));

        auto ctx = std::make_shared<CustomTcpContext>(server.get());
        std::vector<char> req_data;
 
        // 构造请求
        c.reqBuild(req_data);

        // 执行测试 socket直接发送
        int res = tcp_send(req_data, ctx);
        ASSERT_FALSE(res < 0);

        CUSTOM_TEST_INFO_END(c.sub_name);

        server->getLoop()->quit();
        usleep(500000);
    }
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
        std::vector<char> req_data;
 
        // 构造请求
        c.reqBuild(req_data);

        // 执行测试 socket直接发送
        int res = tcp_send(req_data, nullptr);

        ASSERT_EQ(res, c.wantRes);

        CUSTOM_TEST_INFO_END(c.sub_name);

    }

}

