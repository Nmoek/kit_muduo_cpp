/**
 * @file http_project_server.h
 * @brief 测试服务实际 HTTP服务器
 * @author Kewin Li
 * @version 1.0
 * @date 2026-07-08 02:06:15
 * @copyright Copyright (c) 2026 Kewin Li
 */
#ifndef __HTTP_PROJECT_SERVER_H__
#define __HTTP_PROJECT_SERVER_H__


#include "net/inet_address.h"
#include "net/tcp_server.h"
#include "domain/project_server.h"
#include "net/call_backs.h"
#include "net/http/http_servlet.h"

namespace kit_muduo::http {
class HttpServletDispatch;
}

namespace kit_domain {




class HttpProjectServer: public ProjectServer
{
public:
    struct HttpRuntimeItem
    {
        std::shared_ptr<HttpProtocolItem> item{nullptr};
        uint64_t route_id;
    };

    HttpProjectServer(int64_t project_id, std::shared_ptr<RuntimeLease> lease_loop, const kit_muduo::InetAddress &addr);

    ~HttpProjectServer() override;


    const kit_muduo::InetAddress& getBindAddr() const override;

    RuntimeResult<void> AddProtocolItem(std::shared_ptr<ProtocolItem> ori_protocol) override;

    RuntimeResult<void> DelProtocolItem(int64_t protocol_id) override;

    RuntimeResult<std::shared_ptr<ProtocolItem>> GetProtocolItem(int64_t protocol_id) override;

    RuntimeResult<void> UpdateReqCfgProtocolItem(int64_t protocol_id, const nljson& req_cfg_json) override;

    RuntimeResult<void> UpdateRespCfgProtocolItem(int64_t protocol_id, const nljson& resp_cfg_json) override;

    RuntimeResult<void> UpdateBodyProtocolItem(int64_t protocol_id, ProtocolSide side, const ProtocolBodyType body_type, const std::vector<char> &body_data) override;

    RuntimeResult<void> UpdateReqBodyProtocolItem(int64_t protocol_id, const ProtocolBodyType body_type, const std::vector<char> &req_body_data) override;

    RuntimeResult<void> UpdateRespBodyProtocolItem(int64_t protocol_id, const ProtocolBodyType body_type, const std::vector<char> &resp_body_data) override;

    std::shared_ptr<CustomTcpPattern> GetPatternInfo() override;

private:
    void onConnect(kit_muduo::TcpConnectionPtr conn);
    void onMessage(kit_muduo::TcpConnectionPtr conn, kit_muduo::Buffer *buf, kit_muduo::TimeStamp receiveTime);

    void handleRequest(kit_muduo::TcpConnectionPtr conn, kit_muduo::HttpContextPtr ctx);

    void sendAndObserve(kit_muduo::TcpConnectionPtr conn,
        kit_muduo::HttpContextPtr ctx,
        std::shared_ptr<HttpProtocolItem> http_item,
        InteractionResult result,
        const std::string &message,
        bool close_after_send);

    ProtocolInteractionObservation buildHttpObservation( kit_muduo::HttpContextPtr ctx,
        const std::string &peer_addr,
        InteractionResult result,
        std::shared_ptr<HttpProtocolItem> http_item,
        const std::string &message);

    RuntimeResult<void> ReplaceReqCfgProtocolItem(const HttpRuntimeItem& http_run_item,  const HttpItemReqHeaderCfg &new_req_cfg);

    inline bool isSameRoute(const HttpItemReqHeaderCfg &old_cfg, const HttpItemReqHeaderCfg &new_cfg);

    std::shared_ptr<HttpProtocolItem> findRuntimeItem(int64_t protocol_id) const;

    void HttpProjectProcess(std::shared_ptr<HttpProtocolItem> http_item, kit_muduo::TcpConnectionPtr conn, kit_muduo::HttpContextPtr ctx);

private:
    void closeAllProtocolInteractionCaches() override;

private:
    std::shared_ptr<kit_muduo::http::HttpServletDispatch> dispatch_;

    /// @brief 测试服务上依附的配置好的测试项
    std::unordered_map<int64_t, HttpRuntimeItem> http_items_;
    std::mutex mtx_;

};



}
#endif //__HTTP_PROJECT_SERVER_H__