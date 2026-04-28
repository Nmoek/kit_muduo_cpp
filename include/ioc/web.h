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

#include<memory>

namespace kit_muduo {
class EventLoop;

namespace http {

class HttpServer;

};// namespace kit_muduo
}; // namespace kit_muduo

namespace kit_domain {
class ProjectHandler;
class ProtocolHandler;
}


namespace kit_app {

std::shared_ptr<kit_muduo::http::HttpServer> InitWebServer(kit_muduo::EventLoop *loop, kit_domain::ProjectHandler *projHdl, kit_domain::ProtocolHandler *protocHdl);
    
} // namespace kit_app
#endif