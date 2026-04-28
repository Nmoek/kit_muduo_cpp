/**
 * @file web_test.h
 * @brief 测试Handler
 * @author ljk5
 * @version 1.0
 * @date 2025-07-17 17:57:12
 * @copyright Copyright (c) 2025 HIKRayin
 */

#ifndef __KIT_WEB_TEST_H__
#define __KIT_WEB_TEST_H__

#include <memory>

namespace kit_muduo {
namespace http {
    class HttpServer;
}
}

namespace kit_domain {

class TestHandler
{
public:
    TestHandler() = default;
    ~TestHandler() = default;

    void RegisterRoutes(std::shared_ptr<kit_muduo::http::HttpServer> server);

};
    
} // namespace kit_domain
#endif //__KIT_WEB_TEST_H__