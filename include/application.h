/**
 * @file application.h
 * @brief app应用服务器
 * @author ljk5
 * @version 1.0
 * @date 2025-07-17 17:44:57
 * @copyright Copyright (c) 2025 HIKRayin
 */

#ifndef __KIT_APPLICATION_H__
#define __KIT_APPLICATION_H__

#include <memory>

#include "domain/runtime_loop_pool.h"
#include "net/http/http_server.h"
#include "net/call_backs.h"

namespace kit_domain {
class ProjectServer;
class ProjectSvcInterface;
class ProtocolSvcInterface;
}

namespace kit_app {


class Application
{
public:
    Application(std::shared_ptr<kit_muduo::http::HttpServer> server);

    ~Application() = default;

    std::shared_ptr<kit_muduo::http::HttpServer> server() const { return server_; }

    void addServer(int64_t projectId, std::shared_ptr<kit_domain::ProjectServer> server);

    std::shared_ptr<kit_domain::ProjectServer> findServer(int64_t projectId);

    void delServer(int64_t projectId);

    std::shared_ptr<kit_domain::RuntimeLease> leaseLoop(kit_domain::ProjectRuntimeUid uid);

    bool recover(std::shared_ptr<kit_domain::ProjectSvcInterface> project_svc, std::shared_ptr<kit_domain::ProtocolSvcInterface> protocol_svc);

    void shutdown();

private:
    /// @brief http 后台服务器
    std::shared_ptr<kit_muduo::http::HttpServer> server_;
    /// @brief 全局测试服务器容器 用于后台-测试服务器通信
    std::unordered_map<int64_t, std::shared_ptr<kit_domain::ProjectServer>> project_servers_;
    /// @brief 全局测试服务器容器 锁
    std::mutex mtx_;
    /// @brief 全局事件循坏池
    kit_domain::RuntimeLoopPool loop_pool_;
};



}   //kit_app
#endif