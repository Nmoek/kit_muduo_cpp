/**
 * @file project_server.h
 * @brief 测试服务实际服务器
 * @author ljk5
 * @version 1.0
 * @date 2025-08-26 14:35:41
 * @copyright Copyright (c) 2025 HIKRayin
 */
#ifndef __KIT_DOMAIN_PROJECT_SERVER_H__
#define __KIT_DOMAIN_PROJECT_SERVER_H__

#include "base/event_loop_thread.h"
#include "domain/runtime_result.h"
#include "net/call_backs.h"
#include "work/domain/type.h"
#include "net/inet_address.h"
#include "net/buffer.h"
#include "domain/custom_tcp_pattern.h"
#include "nlohmann/json.hpp"

#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

using nljson = nlohmann::json;

namespace kit_muduo{
class EventLoop;
class HttpServer;
class TcpServer;

}

namespace kit_domain {

class Protocol;
class ProtocolItem;
class CustomTcpProtocolItem;
class HttpProtocolItem;
enum class ProtocolBodyType;
class CustomTcpPattern;
struct HttpItemReqHeaderCfg;
class CustomTcpMessage;
struct CustomTcpItemCfg;

class ProjectServer
{
public:

    ProjectServer(int64_t project_id);

    virtual ~ProjectServer();

    void setProjectId(int64_t project_id) { project_id_ = project_id;}
    int64_t getProjectId() const { return project_id_; }

    void stop() { loop_thread_.quit(); }

    bool isActive() const { return loop_thread_.isRunning(); }

    kit_muduo::EventLoop *getLoop() { return loop_thread_.getLoop(); }


    virtual void start() = 0;

    virtual const kit_muduo::InetAddress& getBindAddr() const = 0;

    virtual RuntimeResult<void> AddProtocolItem(std::shared_ptr<ProtocolItem> ori_protocol) = 0;
    virtual RuntimeResult<void> DelProtocolItem(int64_t protocol_id) = 0;

    virtual RuntimeResult<std::shared_ptr<ProtocolItem>> GetProtocolItem(int64_t protocol_id) = 0;

    virtual RuntimeResult<void> UpdateReqCfgProtocolItem(int64_t protocol_id, const nljson &req_cfg_json) = 0;

    virtual RuntimeResult<void> UpdateRespCfgProtocolItem(int64_t protocol_id, const nljson &resp_cfg_json) = 0;


    virtual RuntimeResult<void> UpdateReqBodyProtocolItem(int64_t protocol_id, const ProtocolBodyType body_type, const std::vector<char> &body_data) = 0;

    virtual RuntimeResult<void> UpdateRespBodyProtocolItem(int64_t protocol_id, const ProtocolBodyType body_type, const std::vector<char> &body_data) = 0;


protected:
    /// @brief 测试服务id
    int64_t project_id_;
    /// @brief 事件循环线程
    kit_muduo::EventLoopThread loop_thread_;
    
};


class HttpProjectServer: 
    public ProjectServer, public std::enable_shared_from_this<ProjectServer>
{
public:
    struct HttpRuntimeItem
    {
        std::shared_ptr<HttpProtocolItem> item{nullptr};
        uint64_t route_id;
    };

    HttpProjectServer(int64_t project_id);

    void start() override;

    const kit_muduo::InetAddress& getBindAddr() const override;

    RuntimeResult<void> AddProtocolItem(std::shared_ptr<ProtocolItem> ori_protocol) override;

    RuntimeResult<void> DelProtocolItem(int64_t protocol_id) override;

    RuntimeResult<std::shared_ptr<ProtocolItem>> GetProtocolItem(int64_t protocol_id) override;

    RuntimeResult<void> UpdateReqCfgProtocolItem(int64_t protocol_id, const nljson& req_cfg_json) override;

    RuntimeResult<void> UpdateRespCfgProtocolItem(int64_t protocol_id, const nljson& resp_cfg_json) override;

    RuntimeResult<void> UpdateReqBodyProtocolItem(int64_t protocol_id, const ProtocolBodyType body_type, const std::vector<char> &req_body_data) override;

    RuntimeResult<void> UpdateRespBodyProtocolItem(int64_t protocol_id, const ProtocolBodyType body_type, const std::vector<char> &resp_body_data) override;


private:

    RuntimeResult<void> ReplaceReqCfgProtocolItem(const HttpRuntimeItem& http_run_item,  const HttpItemReqHeaderCfg &new_req_cfg);

    inline bool isSameRoute(const HttpItemReqHeaderCfg &old_cfg, const HttpItemReqHeaderCfg &new_cfg);


    void HttpProjectProcess(int32_t protocol_id,kit_muduo::TcpConnectionPtr conn, kit_muduo::HttpContextPtr ctx);


private:
    kit_muduo::HttpServerPtr http_server_;

    /// @brief 测试服务上依附的配置好的测试项
    std::unordered_map<int64_t, HttpRuntimeItem> http_items_;
    std::mutex mtx_;

};


class CustomTcpProjectServer : public ProjectServer 
{
public:
    struct CustomTcpRuntimeItem
    {
        std::shared_ptr<CustomTcpProtocolItem> item{nullptr};
        std::string function_code_value;
    };

    /**
     * @brief 构造函数
     * @param project_id 项目ID
     * @param tcp_server TCP服务器指针
     */
    CustomTcpProjectServer(int64_t project_id, const CustomTcpPatternType pattern_type, const std::vector<char> &info);

    void start() override;

    const kit_muduo::InetAddress& getBindAddr() const override;

    RuntimeResult<void> AddProtocolItem(std::shared_ptr<ProtocolItem> ori_protocol) override;

    RuntimeResult<void> DelProtocolItem(int64_t protocol_id) override;

    RuntimeResult<std::shared_ptr<ProtocolItem>> GetProtocolItem(int64_t protocol_id) override;

    RuntimeResult<void> UpdateReqCfgProtocolItem(int64_t protocol_id, const nljson &req_cfg_json) override;

    RuntimeResult<void> UpdateRespCfgProtocolItem(int64_t protocol_id, const nljson &resp_cfg_json) override;

    RuntimeResult<void> UpdateReqBodyProtocolItem(int64_t protocol_id, const ProtocolBodyType body_type, const std::vector<char>& req_body_data) override;

    RuntimeResult<void> UpdateRespBodyProtocolItem(int64_t protocol_id, const ProtocolBodyType body_type, const std::vector<char>& resp_body_data) override;

    RuntimeResult<void> setPatternInfo(const std::shared_ptr<CustomTcpPattern> pattern);


    std::shared_ptr<CustomTcpPattern> getPatternInfo();

    // 通过请求的功能码来反向索引 配置的数据
    std::shared_ptr<CustomTcpProtocolItem> findByFuncCode(const std::string&func_code);

private:
    void onConnect(kit_muduo::TcpConnectionPtr conn);
    void onMessage(kit_muduo::TcpConnectionPtr conn, kit_muduo::Buffer *buf, kit_muduo::TimeStamp receiveTime);

    RuntimeResult<void> ReplaceReqCfgProtocolItem(const CustomTcpRuntimeItem& tcp_run_item,  const CustomTcpItemCfg &new_req_cfg);

    // 自定义TCP服务器完整消息处理函数 
    void handleRequest(kit_muduo::TcpConnectionPtr conn, std::shared_ptr<CustomTcpMessage> req);

private:
    /// @brief 实际运行TCP服务器
    kit_muduo::TcpServerPtr tcp_server_;
    /// @brief 格式信息
    std::shared_ptr<CustomTcpPattern> pattern_info_;
    /// @brief 格式信息锁
    std::mutex pattern_info_mtx_;


    /// @brief tcp 协议项列表
    std::unordered_map<int64_t, CustomTcpRuntimeItem> tcp_items_;
    /// @brief 基于请求功能码的协议项映射表
    // 和协议列表共用一把锁
    std::unordered_map<std::string, int64_t> func_codes2ids_;
    std::mutex mtx_;

};

}
#endif
