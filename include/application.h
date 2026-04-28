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

#include "net/http/http_server.h"
#include "net/call_backs.h"
#include "base/thread_pool.h"

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

    std::shared_ptr<kit_muduo::http::HttpServer> server() const { return _server; }

    void AddServer(int64_t projectId, std::shared_ptr<kit_domain::ProjectServer> server);

    std::shared_ptr<kit_domain::ProjectServer> FindServer(int64_t projectId);

    bool Recover(std::shared_ptr<kit_domain::ProjectSvcInterface> project_svc, std::shared_ptr<kit_domain::ProtocolSvcInterface> protocol_svc);

private:
    /// @brief http 后台服务器
    std::shared_ptr<kit_muduo::http::HttpServer> _server;
    /// @brief 全局测试服务器容器 用于后台-测试服务器通信
    std::unordered_map<int64_t, std::shared_ptr<kit_domain::ProjectServer>> _projectServers;
    /// @brief 全局测试服务器容器 锁
    std::mutex _mtx;
};



}   //kit_app
#endif