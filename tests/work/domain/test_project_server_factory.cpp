/**
 * @file test_project_server_factory.cpp
 * @brief 测试服务实例工厂测试
 * @author Kewin Li
 * @version 1.0
 * @date 2026-05-08 20:20:03
 * @copyright Copyright (c) 2026 Kewin Li
 */
#include "base/time_stamp.h"
#include "domain/project.h"
#include "domain/project_server.h"
#include "domain/project_server_factory.h"
#include "net/event_loop.h"
#include "net/inet_address.h"

#include "gtest/gtest.h"

#include <chrono>
#include <future>
#include <memory>
#include <string>

using namespace kit_muduo;
using namespace kit_domain;

namespace {

constexpr auto kLoopCallbackTimeout = std::chrono::seconds(2);

const std::string kNoLengthPatternInfo = R"({
    "least_byte_len": 4,
    "special_fields": {
        "start_magic_num_field": {
            "name": "起始字符",
            "idx": 0,
            "byte_pos": 0,
            "byte_len": 2,
            "type": "STR",
            "value": "H023A"
        },
        "function_code_field": {
            "name": "功能码",
            "idx": 1,
            "byte_pos": 2,
            "byte_len": 2,
            "type": "STR",
            "value": ""
        }
    }
})";

Project MakeBaseProject(int64_t project_id, ProtocolType protocol_type)
{
    return Project{
        .m_id = project_id,
        .m_name = "factory_lifecycle_test",
        .m_mode = ProjectMode::ServerMode,
        .m_protocolType = protocol_type,
        .m_listenPort = 0,
        .m_targetIp = "",
        .m_userId = 1,
        .m_status = ProjectStatus::ON_STATUS,
        .m_patternType = CustomTcpPatternType::NO_LENGTH_DEP,
        .m_patternInfo = std::vector<char>(kNoLengthPatternInfo.begin(), kNoLengthPatternInfo.end()),
        .m_ctime = TimeStamp::Now()
    };
}

void AssertLoopStillOwnedByProjectServer(const std::shared_ptr<ProjectServer>& pj_server)
{
    ASSERT_NE(pj_server, nullptr);

    EventLoop *loop = pj_server->getLoop();
    ASSERT_NE(loop, nullptr);
    EXPECT_EQ(loop, pj_server->getLoop());

    const InetAddress bind_addr = pj_server->getBindAddr();
    EXPECT_GT(bind_addr.toPort(), 0);

    auto done = std::make_shared<std::promise<bool>>();
    auto future = done->get_future();
    loop->queueInLoop([done, loop] {
        done->set_value(loop->isInLoopThread());
    });

    ASSERT_EQ(future.wait_for(kLoopCallbackTimeout), std::future_status::ready)
        << "factory 返回后 runtime loop 没有执行投递任务，可能仍由工厂局部 EventLoopThread 托管";
    EXPECT_TRUE(future.get());
}

} // namespace

/**
 * 测试思路：
 *   Factory::Create(HTTP Project)
 *        |
 *        v
 *   HttpProjectServer 持有 EventLoopThread
 *        |
 *        v
 *   getLoop()->queueInLoop(promise.set_value)
 *
 * 示例：创建 project_id=101、端口=0 的 HTTP 测试服务。工厂返回后再投递
 * 一个 promise 回调到 runtime loop；如果 R1 仍然用工厂局部线程对象承载
 * loop 生命周期，create() 返回时线程已退出，这个回调不会在 2 秒内完成。
 */
TEST(TestProjectServerFactory, CreateHttpServerKeepsLoopAliveAfterFactoryReturns)
{
    Project p = MakeBaseProject(101, ProtocolType::HTTP_PROTOCOL);

    auto pj_server = ProjectServerFactory::Create(p);
    ASSERT_NE(std::dynamic_pointer_cast<HttpProjectServer>(pj_server), nullptr);
    AssertLoopStillOwnedByProjectServer(pj_server);
}

/**
 * 测试思路：
 *   Factory::Create(Custom TCP Project)
 *        |
 *        v
 *   CustomTcpProjectServer 解析 pattern_info 并持有 EventLoopThread
 *        |
 *        v
 *   工厂返回后 runtime loop 仍能执行异步任务
 *
 * 示例：创建 project_id=102、NO_LENGTH_DEP 格式的自定义 TCP 测试服务。
 * 这里不依赖固定监听端口，也不发真实 TCP 报文，只验证 R1 的生命周期边界：
 * ProjectServer 存活期间 loop 必须继续运行。
 */
TEST(TestProjectServerFactory, CreateCustomTcpServerKeepsLoopAliveAfterFactoryReturns)
{
    Project p = MakeBaseProject(102, ProtocolType::CUSTOM_TCP_PROTOCOL);

    auto pj_server = ProjectServerFactory::Create(p);
    ASSERT_NE(std::dynamic_pointer_cast<CustomTcpProjectServer>(pj_server), nullptr);
    AssertLoopStillOwnedByProjectServer(pj_server);
}
