#ifndef KIT_NET_TLS_CONTEXT_H_
#define KIT_NET_TLS_CONTEXT_H_

#include "base/noncopyable.h"

#include <memory>
#include <string>
#include <openssl/ssl.h>

namespace kit_muduo {

struct TlsServerConfig
{
    std::string cert_chain_file;
    std::string private_key_file;
};

/** @brief 不可变服务端 TLS 配置；连接在自己的 EventLoop 中操作独立 SSL。 */
class TlsContext : Noncopyable
{
public:
    using SslPtr = std::unique_ptr<SSL, decltype(&SSL_free)>;

    // 加载失败抛出带原因的异常，不读取终端密码、不输出私钥内容。
    static std::shared_ptr<TlsContext> Create(const TlsServerConfig& config);
    SslPtr createSsl(int fd) const;

private:
    explicit TlsContext(std::unique_ptr<SSL_CTX, decltype(&SSL_CTX_free)> context);
    std::unique_ptr<SSL_CTX, decltype(&SSL_CTX_free)> context_;
};

} // namespace kit_muduo
#endif
