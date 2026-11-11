#ifndef KIT_NET_TLS_CONNECTION_H_
#define KIT_NET_TLS_CONNECTION_H_

#include "net/tcp_connection.h"

namespace kit_muduo {

class TlsContext;

/** @brief 在连接所属 EventLoop 中执行服务端 TLS 握手、加密收发和关闭。 */
class TlsConnection : public TcpConnection
{
public:
    TlsConnection(EventLoop* loop, const std::string& name, int32_t sockfd,
        const InetAddress& peer_addr, const InetAddress& local_addr,
        std::shared_ptr<TlsContext> context);
    ~TlsConnection() override;

protected:
    void establishTransport() override;
    void handleRead(TimeStamp receive_time) override;
    void handleWrite() override;
    void sendInLoop(const void* message, size_t len) override;
    void shutdownInLoop() override;
    void closeTransport() override;
    void onSocketError(int error) override;

private:
    struct TlsState;
    void handleTlsEvent(TimeStamp receive_time, bool writable);
    void readTls(TimeStamp receive_time);
    void writeTls();
    void shutdownTls();
    void updateTlsEvents();
    void waitTls(int result, int& wait, const char* operation);

    std::unique_ptr<TlsState> tls_;
};

} // namespace kit_muduo
#endif
