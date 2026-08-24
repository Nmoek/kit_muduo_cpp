/**
 * @file web.h
 * @brief web接口 控制反转
 * @author ljk5
 * @version 1.0
 * @date 2025-07-19 02:52:44
 * @copyright Copyright (c) 2025 HIKRayin
 */
#ifndef __KIT_IOC_WEB_H__
#define __KIT_IOC_WEB_H__

#include "net/http/http_server.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>

namespace kit_muduo {
class EventLoop;

namespace http {
class HttpServer;
};// namespace http

}; // namespace kit_muduo

namespace kit_domain {
class AuthHandler;
class ProjectHandler;
class ProtocolHandler;
class UserHandler;
class ProtocolInteractionHandler;
}


namespace kit_app {

struct WebServerStartupConfig
{
    std::string host;
    uint16_t port{0};
    std::filesystem::path static_root;
    int32_t io_threads{0};
    kit_muduo::http::BusinessThreadPoolConfig business_thread_pool;
};

std::shared_ptr<kit_muduo::http::HttpServer> InitWebServer(kit_muduo::EventLoop *loop,
    kit_domain::ProjectHandler *projHdl,
    kit_domain::ProtocolHandler *protocHdl,
    kit_domain::AuthHandler *authHdl,
    kit_domain::UserHandler *userHdl,
    kit_domain::ProtocolInteractionHandler *interHdl);
    
} // namespace kit_app
#endif
