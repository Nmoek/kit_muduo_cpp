#ifndef KIT_TESTS_SCOPED_HTTPS_CREATOR_H_
#define KIT_TESTS_SCOPED_HTTPS_CREATOR_H_

#include "domain/project_server_factory.h"

namespace kit_test {

/** @brief 为串行工厂测试注册证书配置，退出作用域时恢复默认创建器。 */
class ScopedHttpsCreator : kit_muduo::Noncopyable
{
public:
    explicit ScopedHttpsCreator(kit_muduo::TlsServerConfig config = {})
    {
        kit_domain::ProjectServerFactoryManager::getInstance().registerCreator(
            std::make_unique<kit_domain::HttpsProjectServerCreator>(std::move(config)));
    }

    ~ScopedHttpsCreator()
    {
        kit_domain::ProjectServerFactoryManager::getInstance().registerCreator(
            std::make_unique<kit_domain::HttpsProjectServerCreator>());
    }
};

} // namespace kit_test
#endif
