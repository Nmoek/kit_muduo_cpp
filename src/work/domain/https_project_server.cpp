#include "domain/https_project_server.h"
#include "net/tls_connection.h"

#include <stdexcept>

namespace kit_domain {

HttpsProjectServer::HttpsProjectServer(int64_t project_id, std::shared_ptr<RuntimeLease> lease_loop,
    const kit_muduo::InetAddress& addr, std::shared_ptr<kit_muduo::TlsContext> context)
    :HttpProjectServer(project_id, std::move(lease_loop), addr, "pj" + std::to_string(project_id) + "https")
{
    if(!context)
        throw std::invalid_argument("HttpsProjectServer requires a TLS context");
    tcp_server_.setConnectionFactory([context = std::move(context)](kit_muduo::EventLoop* loop,
        const std::string& name, int32_t fd, const kit_muduo::InetAddress& peer_addr,
        const kit_muduo::InetAddress& local_addr) {
        return std::make_shared<kit_muduo::TlsConnection>(loop, name, fd, peer_addr, local_addr, context);
    });
}

HttpsProjectServer::~HttpsProjectServer()
{
    // 在派生类型仍有效时停止回调，交互记录继续使用 HTTPS 标识。
    stop();
}

} // namespace kit_domain
