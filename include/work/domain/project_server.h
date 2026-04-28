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

#include "net/call_backs.h"
#include "work/domain/type.h"
#include "net/inet_address.h"
#include "net/buffer.h"
#include "domain/custom_tcp_pattern.h"
#include "nlohmann/json.hpp"

#include <memory>
#include <unordered_map>
#include <mutex>
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
enum class ProtocolBodyType;
class CustomTcpPattern;
class CustomTcpContext;
class CustomTcpMessage;

class ProjectServer
{
public:

    ProjectServer() = default;

    ProjectServer(int64_t project_id);

    virtual ~ProjectServer() = default;

    int64_t getProjectId() const { return _project_id; }

    virtual kit_muduo::EventLoop *getLoop() const = 0;

    virtual void AddProtocolItem(std::shared_ptr<ProtocolItem> ori_protocol) = 0;
    virtual void DelProtocolItem(int64_t protocol_id) = 0;

    virtual std::shared_ptr<ProtocolItem> GetProtocolItem(int64_t protocol_id) = 0;

    virtual void UpdateReqCfgProtocolItem(int64_t protocol_id, const nljson &req_cfg_json) = 0;

    virtual void UpdateRespCfgProtocolItem(int64_t protocol_id, const nljson &resp_cfg_json) = 0;

    virtual void UpdateReqBodyProtocolItem(int64_t protocol_id, const ProtocolBodyType body_type, const std::vector<char> &req_body_data) = 0;

    virtual void UpdateRespBodyProtocolItem(int64_t protocol_id, const ProtocolBodyType body_type, const std::vector<char> &resp_body_data) = 0;

protected:
    /// @brief 测试服务id
    int64_t _project_id;

    /// @brief 测试服务上依附的配置好的测试项
    std::unordered_map<int64_t, std::shared_ptr<ProtocolItem>> _protocol_items;
    std::mutex _mtx;
};


class HttpProjectServer: 
    public ProjectServer, public std::enable_shared_from_this<ProjectServer>
{
public:

    HttpProjectServer(int64_t project_id, kit_muduo::HttpServerPtr http_server);

    kit_muduo::EventLoop *getLoop() const override;

    void AddProtocolItem(std::shared_ptr<ProtocolItem> ori_protocol) override;

    void DelProtocolItem(int64_t protocol_id) override;

    std::shared_ptr<ProtocolItem> GetProtocolItem(int64_t protocol_id) override;

    void UpdateReqCfgProtocolItem(int64_t protocol_id, const nljson& req_cfg_json);

    void UpdateRespCfgProtocolItem(int64_t protocol_id, const nljson& resp_cfg_json);

    void UpdateReqBodyProtocolItem(int64_t protocol_id, const ProtocolBodyType body_type, const std::vector<char> &req_body_data);

    void UpdateRespBodyProtocolItem(int64_t protocol_id, const ProtocolBodyType body_type, const std::vector<char> &resp_body_data);


private:
    void HttpProjectProcess(kit_muduo::TcpConnectionPtr conn, kit_muduo::HttpContextPtr ctx);

private:
    kit_muduo::HttpServerPtr _http_server;
};


class CustomTcpProjectServer : public ProjectServer 
{
public:
    /**
     * @brief 构造函数
     * @param project_id 项目ID
     * @param tcp_server TCP服务器指针
     */
    CustomTcpProjectServer(int64_t project_id, kit_muduo::TcpServerPtr tcp_server, const CustomTcpPatternType pattern_type, const std::vector<char> &info);

    kit_muduo::EventLoop *getLoop() const override;

    void AddProtocolItem(std::shared_ptr<ProtocolItem> ori_protocol) override;

    void DelProtocolItem(int64_t protocol_id) override;

    std::shared_ptr<ProtocolItem> GetProtocolItem(int64_t protocol_id) override;

    void UpdateReqCfgProtocolItem(int64_t protocol_id, const nljson &req_cfg_json) override;

    void UpdateRespCfgProtocolItem(int64_t protocol_id, const nljson &resp_cfg_json) override;

    void UpdateReqBodyProtocolItem(int64_t protocol_id, const ProtocolBodyType body_type, const std::vector<char>& req_body_data) override;

    void UpdateRespBodyProtocolItem(int64_t protocol_id, const ProtocolBodyType body_type, const std::vector<char>& resp_body_data) override;

    void setPatternInfo(const std::shared_ptr<CustomTcpPattern> pattern);

    std::shared_ptr<CustomTcpPattern> getPatternInfo();


    // 通过请求的功能码来反向索引 配置的数据
    std::shared_ptr<CustomTcpProtocolItem> findByFuncCode(const std::string&func_code);


private:
    void onConnect(kit_muduo::TcpConnectionPtr conn);
    void onMessage(kit_muduo::TcpConnectionPtr conn, kit_muduo::Buffer *buf, kit_muduo::TimeStamp receiveTime);

    // 自定义TCP服务器完整消息处理函数 
    // TODO 还需要添加TCP上下文
    void handleRequest(kit_muduo::TcpConnectionPtr conn, std::shared_ptr<CustomTcpMessage> req);

private:
    /// @brief 实际运行TCP服务器
    kit_muduo::TcpServerPtr _tcp_server;
    /// @brief 格式信息
    std::shared_ptr<CustomTcpPattern> _pattern_info;
    /// @brief 格式信息锁
    std::mutex _pattern_info_mtx;

    /// @brief 基于请求功能码的协议项映射表
    // 和协议列表共用一把锁
    std::unordered_map<std::string, std::shared_ptr<CustomTcpProtocolItem>> _func_codes;


    /*和HTPP一样也抽象一个解析器类型出来  以免后续有新的解析方法 */


};

}
#endif
