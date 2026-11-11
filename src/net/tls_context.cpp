#include "net/tls_context.h"

#include <cstring>
#include <filesystem>
#include <stdexcept>
#include <cerrno>
#include <sys/socket.h>
#include <openssl/err.h>

namespace kit_muduo {
namespace {

std::runtime_error TlsError(const char* operation)
{
    const unsigned long code = ERR_get_error();
    const char* reason = ERR_reason_error_string(code);
    return std::runtime_error(std::string(operation) + ": " +
        (reason ? reason : "OpenSSL operation failed"));
}

int SelectHttp11(SSL*, const unsigned char** out, unsigned char* out_len,
    const unsigned char* in, unsigned int in_len, void*)
{
    // 未携带 ALPN 的 HTTP/1.x 客户端仍可使用；携带时必须包含 http/1.1。
    for(unsigned int offset = 0; offset < in_len;)
    {
        const unsigned int len = in[offset++];
        if(len == 0 || len > in_len - offset)
        {
            return SSL_TLSEXT_ERR_ALERT_FATAL;
        }
        if(len == 8 && std::memcmp(in + offset, "http/1.1", 8) == 0)
        {
            *out = in + offset;
            *out_len = 8;
            return SSL_TLSEXT_ERR_OK;
        }
        offset += len;
    }
    return SSL_TLSEXT_ERR_ALERT_FATAL;
}

BIO_METHOD* NoSignalSocketMethod()
{
    // TLS 对端可随时断连。仅对本次 send 禁用 SIGPIPE，不修改应用全局信号策略。
    static const std::unique_ptr<BIO_METHOD, decltype(&BIO_meth_free)> method([] {
        BIO_METHOD* value = BIO_meth_new(BIO_get_new_index() | BIO_TYPE_SOURCE_SINK, "kit TLS socket");
        if(!value) throw TlsError("HTTPS socket BIO initialization failed");
        const BIO_METHOD* socket_method = BIO_s_socket();
        BIO_meth_set_create(value, BIO_meth_get_create(socket_method));
        BIO_meth_set_destroy(value, BIO_meth_get_destroy(socket_method));
        BIO_meth_set_ctrl(value, BIO_meth_get_ctrl(socket_method));
        BIO_meth_set_read(value, BIO_meth_get_read(socket_method));
        BIO_meth_set_write(value, [](BIO* bio, const char* data, int len) {
            BIO_clear_retry_flags(bio);
            const int result = static_cast<int>(::send(BIO_get_fd(bio, nullptr), data, len, MSG_NOSIGNAL));
            if(result < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR))
                BIO_set_retry_write(bio);
            return result;
        });
        return value;
    }(), BIO_meth_free);
    return method.get();
}

} // namespace

TlsContext::TlsContext(std::unique_ptr<SSL_CTX, decltype(&SSL_CTX_free)> context)
    :context_(std::move(context))
{
}

std::shared_ptr<TlsContext> TlsContext::Create(const TlsServerConfig& config)
{
    if(config.cert_chain_file.empty() || config.private_key_file.empty())
    {
        throw std::invalid_argument("HTTPS requires work.https.cert_chain_file and work.https.private_key_file");
    }
    if(!std::filesystem::path(config.cert_chain_file).is_absolute()
        || !std::filesystem::path(config.private_key_file).is_absolute())
    {
        throw std::invalid_argument("HTTPS certificate and private key paths must be absolute");
    }

    ERR_clear_error();
    std::unique_ptr<SSL_CTX, decltype(&SSL_CTX_free)> context(
        SSL_CTX_new(TLS_server_method()), SSL_CTX_free);
    if(!context || SSL_CTX_set_min_proto_version(context.get(), TLS1_2_VERSION) != 1)
    {
        throw TlsError("HTTPS context initialization failed");
    }
    SSL_CTX_set_options(context.get(), SSL_OP_NO_COMPRESSION | SSL_OP_NO_RENEGOTIATION);
    SSL_CTX_set_verify(context.get(), SSL_VERIFY_NONE, nullptr);
    SSL_CTX_set_mode(context.get(), SSL_MODE_ENABLE_PARTIAL_WRITE);
    SSL_CTX_set_alpn_select_cb(context.get(), SelectHttp11, nullptr);
    SSL_CTX_set_default_passwd_cb(context.get(), [](char*, int, int, void*) { return 0; });

    if(SSL_CTX_use_certificate_chain_file(context.get(), config.cert_chain_file.c_str()) != 1)
    {
        throw TlsError("HTTPS certificate chain load failed");
    }
    if(SSL_CTX_use_PrivateKey_file(context.get(), config.private_key_file.c_str(), SSL_FILETYPE_PEM) != 1)
    {
        throw TlsError("HTTPS private key load failed (unencrypted PEM required)");
    }
    if(SSL_CTX_check_private_key(context.get()) != 1)
    {
        throw TlsError("HTTPS certificate and private key do not match");
    }
    return std::shared_ptr<TlsContext>(new TlsContext(std::move(context)));
}

TlsContext::SslPtr TlsContext::createSsl(int fd) const
{
    ERR_clear_error();
    SslPtr ssl(SSL_new(context_.get()), SSL_free);
    if(!ssl)
    {
        throw TlsError("HTTPS connection initialization failed");
    }
    BIO* bio = BIO_new(NoSignalSocketMethod());
    if(!bio)
    {
        throw TlsError("HTTPS socket BIO initialization failed");
    }
    BIO_set_fd(bio, fd, BIO_NOCLOSE);
    SSL_set_bio(ssl.get(), bio, bio);
    SSL_set_accept_state(ssl.get());
    return ssl;
}

} // namespace kit_muduo
