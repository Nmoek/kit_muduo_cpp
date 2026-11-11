#include "net/tls_connection.h"
#include "net/tls_context.h"
#include "net/channel.h"
#include "net/event_loop.h"
#include "net/net_log.h"

#include <algorithm>
#include <array>
#include <stdexcept>
#include <openssl/err.h>

namespace kit_muduo {

struct TlsConnection::TlsState
{
    enum { kReady, kRead, kWrite };
    explicit TlsState(std::shared_ptr<TlsContext> ctx) :context(std::move(ctx)) {}

    std::shared_ptr<TlsContext> context;
    TlsContext::SslPtr ssl{nullptr, SSL_free};
    std::shared_ptr<Timer> timer;
    std::array<char, 16384> read_buffer;
    // SSL_write WANT_* 重试必须使用相同地址、相同数据和长度。
    std::vector<char> write_buffer;
    int handshake_wait{kReady};
    int read_wait{kReady};
    int write_wait{kReady};
    int shutdown_wait{kReady};
    bool shutting_down{false};
};

TlsConnection::TlsConnection(EventLoop* loop, const std::string& name, int32_t sockfd,
    const InetAddress& peer_addr, const InetAddress& local_addr, std::shared_ptr<TlsContext> context)
    :TcpConnection(loop, name, sockfd, peer_addr, local_addr)
    ,tls_(std::make_unique<TlsState>(std::move(context)))
{
    if(!tls_->context)
        throw std::invalid_argument("TlsConnection requires a TLS context");
}

TlsConnection::~TlsConnection() = default;

void TlsConnection::establishTransport()
{
    try {
        tls_->ssl = tls_->context->createSsl(fd());
    } catch(const std::exception& error) {
        CONN_F_ERROR("TLS fd[%d] initialization failed: %s\n", fd(), error.what());
        handleClose();
        return;
    }
    std::weak_ptr<TlsConnection> weak_self = std::static_pointer_cast<TlsConnection>(shared_from_this());
    tls_->timer = getLoop()->runAfter(10000, [weak_self] {
        if(auto self = weak_self.lock())
        {
            CONN_F_WARN("TLS handshake timeout fd[%d]\n", self->fd());
            self->handleClose();
        }
    });
    handleTlsEvent(TimeStamp::Now(), false);
}

void TlsConnection::handleRead(TimeStamp receive_time)
{
    handleTlsEvent(receive_time, false);
}

void TlsConnection::handleWrite()
{
    handleTlsEvent(TimeStamp::Now(), true);
}

void TlsConnection::onSocketError(int error)
{
    if(error != 0)
        handleClose();
}

void TlsConnection::sendInLoop(const void* message, size_t len)
{
    if(_state != kConnected || len == 0)
        return;
    _outputBuffer.append(static_cast<const char*>(message), len);
    if(tls_->write_wait == TlsState::kReady && tls_->read_wait != TlsState::kWrite)
        writeTls();
    updateTlsEvents();
}

void TlsConnection::shutdownInLoop()
{
    if(_state == kDisconnected)
        return;
    _state = kDisconnecting;
    if(_outputBuffer.readableBytes() == 0)
        shutdownTls();
    updateTlsEvents();
}

void TlsConnection::waitTls(int result, int& wait, const char* operation)
{
    const int error = SSL_get_error(tls_->ssl.get(), result);
    if(error == SSL_ERROR_WANT_READ || error == SSL_ERROR_WANT_WRITE)
    {
        wait = error == SSL_ERROR_WANT_READ ? TlsState::kRead : TlsState::kWrite;
        return;
    }
    const unsigned long code = ERR_get_error();
    const char* reason = ERR_reason_error_string(code);
    CONN_F_WARN("TLS %s failed fd[%d], ssl_error[%d], reason[%s]\n",
        operation, fd(), error, reason ? reason : "peer closed or socket error");
    handleClose();
}

void TlsConnection::handleTlsEvent(TimeStamp receive_time, bool writable)
{
    if(kDisconnected == _state)
    {
        return;
    }
    if(kConnecting == _state)
    {
        ERR_clear_error();
        const int result = SSL_accept(tls_->ssl.get());
        if(result != 1)
        {
            waitTls(result, tls_->handshake_wait, "handshake");
            updateTlsEvents();
            return;
        }
        getLoop()->cancel(tls_->timer);
        tls_->timer.reset();
        notifyConnected();
        if(kDisconnected == _state)
        {
            return;
        }
    }
    if(tls_->shutting_down)
    {
        shutdownTls();
    }
    else
    {
        if(!writable && tls_->write_wait == TlsState::kRead)
        {
            writeTls();
        }
        if(kDisconnected != _state && !tls_->shutting_down &&
            (!writable || tls_->read_wait == TlsState::kWrite || SSL_pending(tls_->ssl.get()) > 0))
        {
            readTls(receive_time);
        }
        if(kDisconnected != _state && !tls_->shutting_down && writable &&
            tls_->write_wait != TlsState::kRead && tls_->read_wait != TlsState::kWrite)
        {
            writeTls();
        }
    }
    updateTlsEvents();
}

void TlsConnection::readTls(TimeStamp receive_time)
{
    // 每次最多处理 1 MiB，避免大请求独占 loop。SSL 内部有明文时主动续读。
    for(int count = 0; count < 64; ++count)
    {
        ERR_clear_error();
        const int result = SSL_read(tls_->ssl.get(), tls_->read_buffer.data(), tls_->read_buffer.size());
        if(result <= 0)
        {
            if(SSL_get_error(tls_->ssl.get(), result) == SSL_ERROR_ZERO_RETURN)
            {
                tls_->read_wait = TlsState::kReady;
                shutdownInLoop();
                return;
            }
            waitTls(result, tls_->read_wait, "read");
            return;
        }
        tls_->read_wait = TlsState::kReady;
        if(kConnected == _state)
        {
            _inputBuffer.append(tls_->read_buffer.data(), result);
            notifyMessage(receive_time);
        }
        if(kDisconnected == _state || tls_->shutting_down)
            return;
    }
    if(SSL_pending(tls_->ssl.get()) > 0)
    {
        std::weak_ptr<TlsConnection> weak_self = std::static_pointer_cast<TlsConnection>(shared_from_this());
        getLoop()->queueInLoop([weak_self, receive_time] {
            if(auto self = weak_self.lock())
                self->handleTlsEvent(receive_time, false);
        });
    }
}

void TlsConnection::writeTls()
{
    const bool had_data = _outputBuffer.readableBytes() > 0;
    for(int count = 0; count < 64 && _outputBuffer.readableBytes() > 0; ++count)
    {
        if(tls_->write_buffer.empty())
        {
            const size_t len = std::min<size_t>(16384, _outputBuffer.readableBytes());
            tls_->write_buffer.assign(_outputBuffer.peek(), _outputBuffer.peek() + len);
        }
        ERR_clear_error();
        const int result = SSL_write(tls_->ssl.get(), tls_->write_buffer.data(), tls_->write_buffer.size());
        if(result <= 0)
        {
            waitTls(result, tls_->write_wait, "write");
            return;
        }
        _outputBuffer.reset(result);
        tls_->write_buffer.clear();
        tls_->write_wait = TlsState::kReady;
    }
    if(had_data && _outputBuffer.readableBytes() == 0)
    {
        notifyWriteComplete();
        if(kDisconnecting == _state)
            shutdownTls();
    }
}

void TlsConnection::shutdownTls()
{
    if(kDisconnected == _state)
        return;
    if(!tls_->shutting_down)
    {
        tls_->shutting_down = true;
        std::weak_ptr<TlsConnection> weak_self = std::static_pointer_cast<TlsConnection>(shared_from_this());
        tls_->timer = getLoop()->runAfter(2000, [weak_self] {
            if(auto self = weak_self.lock())
                self->handleClose();
        });
    }
    ERR_clear_error();
    const int result = SSL_shutdown(tls_->ssl.get());
    if(result == 1)
        handleClose();
    else if(result == 0)
        tls_->shutdown_wait = TlsState::kRead;
    else
        waitTls(result, tls_->shutdown_wait, "shutdown");
}

void TlsConnection::updateTlsEvents()
{
    if(kDisconnected == _state)
    {
        return;
    }
    bool read = true;
    bool write = false;
    if(kConnecting == _state)
    {
        write = tls_->handshake_wait == TlsState::kWrite;
        read = !write;
    }
    else if(tls_->shutting_down)
    {
        write = tls_->shutdown_wait == TlsState::kWrite;
        read = !write;
    }
    else
    {
        write = tls_->read_wait == TlsState::kWrite ||
            (_outputBuffer.readableBytes() > 0 && tls_->write_wait != TlsState::kRead);
        read = tls_->read_wait != TlsState::kWrite || tls_->write_wait == TlsState::kRead;
    }
    if(read != _channel->isReading())
    {
        if(read) _channel->enableReading();
        else _channel->disableReading();
    }
    if(write != _channel->isWriting())
    {
        if(write) _channel->enableWriting();
        else _channel->disableWriting();
    }
}

void TlsConnection::closeTransport()
{
    if(tls_->timer)
    {
        getLoop()->cancel(tls_->timer);
        tls_->timer.reset();
    }
    tls_->ssl.reset();
    tls_->write_buffer.clear();
    _outputBuffer.resetAll();
}

} // namespace kit_muduo
