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
class RuntimeControllerInterface;

}

namespace kit_app {


class Application
{
public:
    Application(std::shared_ptr<kit_muduo::http::HttpServer> server, std::shared_ptr<kit_domain::RuntimeControllerInterface> project_runtime_manager);

    ~Application() = default;

    std::shared_ptr<kit_muduo::http::HttpServer> server() const { return server_; }

    void addServer(int64_t project_id, std::shared_ptr<kit_domain::ProjectServer> server);

    std::shared_ptr<kit_domain::ProjectServer> findServer(int64_t project_id);

    void delServer(int64_t project_id);

    bool recover();

    void shutdown();

private:
    /// @brief http 后台服务器
    std::shared_ptr<kit_muduo::http::HttpServer> server_;
    /// @brief 测试服务运行管理
    std::shared_ptr<kit_domain::RuntimeControllerInterface> project_runtime_manager_;
};



}   //kit_app
#endif