/**
 * @file custom_tcp_project_server.h
 * @brief 测试服务实际 自定义TCP服务器
 * @author Kewin Li
 * @version 1.0
 * @date 2026-07-08 02:08:06
 * @copyright Copyright (c) 2026 Kewin Li
 */
#ifndef __CUSTOM_TCP_PROJECT_SERVER_H__
#define __CUSTOM_TCP_PROJECT_SERVER_H__ 


#include "domain/custom_tcp_context.h"
#include "domain/project_server.h"
#include "net/call_backs.h"

#include <unordered_set>

namespace kit_domain {


class CustomTcpProjectServer : public ProjectServer 
{
public:
    struct CustomTcpRuntimeItem
    {
        int64_t protocol_id{0};
        std::string function_code_value;
        std::shared_ptr<CustomTcpProtocolItem> item{nullptr};
        ProcessCallback cb;
    };

    /**
     * @brief 构造函数
     * @param project_id 项目ID
     * @param tcp_server TCP服务器指针
     */
    CustomTcpProjectServer(int64_t project_id, const std::vector<char> &info, std::shared_ptr<RuntimeLease> lease_loop);

    ~CustomTcpProjectServer() override;

    const kit_muduo::InetAddress& getBindAddr() const override;

    RuntimeResult<void> AddProtocolItem(std::shared_ptr<ProtocolItem> ori_protocol) override;

    RuntimeResult<void> DelProtocolItem(int64_t protocol_id) override;

    RuntimeResult<std::shared_ptr<ProtocolItem>> GetProtocolItem(int64_t protocol_id) override;

    RuntimeResult<void> UpdateReqCfgProtocolItem(int64_t protocol_id, const nljson &req_cfg_json) override;

    RuntimeResult<void> UpdateRespCfgProtocolItem(int64_t protocol_id, const nljson &resp_cfg_json) override;

    RuntimeResult<void> UpdateBodyProtocolItem(int64_t protocol_id, ProtocolSide side, const ProtocolBodyType body_type, const std::vector<char> &body_data) override;

    RuntimeResult<void> UpdateReqBodyProtocolItem(int64_t protocol_id, const ProtocolBodyType body_type, const std::vector<char>& req_body_data) override;

    RuntimeResult<void> UpdateRespBodyProtocolItem(int64_t protocol_id, const ProtocolBodyType body_type, const std::vector<char>& resp_body_data) override;

    std::shared_ptr<CustomTcpPattern> GetPatternInfo() override;

    RuntimeResult<void> setPatternInfo(const std::shared_ptr<CustomTcpPattern> pattern);

    // 通过请求的功能码来反向索引 配置的数据
    ProcessCallback findCBByFuncCode(const std::string&func_code);


private:
    void onConnect(kit_muduo::TcpConnectionPtr conn);
    void onMessage(kit_muduo::TcpConnectionPtr conn, kit_muduo::Buffer *buf, kit_muduo::TimeStamp receiveTime);

    RuntimeResult<void> ReplaceReqCfgProtocolItem(CustomTcpRuntimeItem& tcp_run_item,  const CustomTcpItemCfg &new_req_cfg);

    // 自定义TCP服务器完整消息处理函数 
    void CustomTcpProcess(std::shared_ptr<CustomTcpProtocolItem> tcp_item, kit_muduo::TcpConnectionPtr conn, CustomTcpContextPtr ctx);


    ProcessCallback findCBByFuncCodeUnLock(const std::string&func_code);

    void sendAndObserve(kit_muduo::TcpConnectionPtr conn,
        CustomTcpContextPtr ctx,
        std::shared_ptr<CustomTcpProtocolItem> tcp_item,
        InteractionResult result,
        std::string message,
        bool is_shutdown);

    ProtocolInteractionObservation buildCustomTcpObservation(CustomTcpContextPtr ctx,
        const std::string &peer_addr,
        InteractionResult result,
        std::shared_ptr<CustomTcpProtocolItem> tcp_item,
        const std::string &message);

private:
    void closeAllProtocolInteractionCaches() override;


private:
    /// @brief 格式信息
    std::shared_ptr<CustomTcpPattern> pattern_info_;
    /// @brief 格式信息锁
    std::mutex pattern_info_mtx_;


    /// @brief tcp 协议项列表
    std::unordered_map<int64_t, CustomTcpRuntimeItem> tcp_items_;
    std::mutex mtx_;

};





}
#endif // __CUSTOM_TCP_PROJECT_SERVER_H__