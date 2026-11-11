#ifndef KIT_DOMAIN_HTTPS_PROJECT_SERVER_H_
#define KIT_DOMAIN_HTTPS_PROJECT_SERVER_H_

#include "domain/http_project_server.h"

namespace kit_muduo {
class TlsContext;
}

namespace kit_domain {

/** @brief 装配 TLS 连接并复用 HTTP 协议项、路由及交互捕获。 */
class HttpsProjectServer : public HttpProjectServer
{
public:
    HttpsProjectServer(int64_t project_id, std::shared_ptr<RuntimeLease> lease_loop,
        const kit_muduo::InetAddress& addr, std::shared_ptr<kit_muduo::TlsContext> context);
    ~HttpsProjectServer() override;

protected:
    ProtocolType protocolType() const override { return ProtocolType::kHttps; }
};

} // namespace kit_domain
#endif
