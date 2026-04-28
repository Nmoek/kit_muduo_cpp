/**
 * @file web_test.cpp
 * @brief 测试Handler
 * @author ljk5
 * @version 1.0
 * @date 2025-07-17 17:56:35
 * @copyright Copyright (c) 2025 HIKRayin
 */

#include "web/web_test.h"
#include "net/http/http_server.h"
#include "net/http/http_servlet.h"

using namespace kit_muduo;
using namespace kit_muduo::http;

namespace kit_domain
{


void TestHandler::RegisterRoutes(std::shared_ptr<kit_muduo::http::HttpServer> server)
{
    // 这里接口重新设计 
    // 路径 -->>  执行方法
    // 路径模版 --> 执行方法 + 模版匹配方法
    // 默认 把路径加入到精准匹配中
    server->Get("/html/login.html", std::make_shared<StaticFileServlet>());
    server->Get("/css/login.css", std::make_shared<StaticFileServlet>());
    server->Get("/js/login.js", std::make_shared<StaticFileServlet>());
}
} // namespace kit_app
