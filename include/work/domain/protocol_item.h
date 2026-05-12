/**
 * @file protocol_item.h
 * @brief 测试服务实际协议项
 * @author ljk5
 * @version 1.0
 * @date 2025-08-26 16:12:28
 * @copyright Copyright (c) 2025 HIKRayin
 */

#ifndef __KIT_DOMAIN_PROTOCOL_ITEM_H__
#define __KIT_DOMAIN_PROTOCOL_ITEM_H__

#include "domain/project_server.h"
#include "net/call_backs.h"
#include "domain/type.h"

#include <memory>
#include <string>
#include <vector>
#include <stdexcept>

namespace kit_muduo::http {
struct RouteResult;
}

namespace kit_domain {

class Protocol;
class CustomTcpMessage;
class ProjectServer;
class CustomTcpPattern;

class ProtocolItem
{
public:
    using Ptr = std::shared_ptr<ProtocolItem>;

    virtual ~ProtocolItem() =default;

    virtual bool init(std::shared_ptr<Protocol> ori_protocol) = 0;

    virtual bool setReqCfg(const nlohmann::json& req_json) = 0;
    virtual bool setRespCfg(const nlohmann::json& resp_json) = 0;
    virtual void setReqBody(const ProtocolBodyType body_type, const std::vector<char> &body_data) = 0;
    virtual void setRespBody(const ProtocolBodyType body_type, const std::vector<char> &body_data) = 0;

    int64_t getId() const;

    std::string getName() const;

    int64_t getProjectId() const;

    void setRegRouteId(int64_t route_id);

    int64_t getRegRouteId() const;

    bool isEndian() const;

protected:
    int64_t id_;
    std::string name_;         // 测试协议名称
    ProtocolType type_;         // 测试协议类型
    int64_t project_id_;    // 所属测试服务Id
    bool  is_endian_;     // 是否大小端转换
    /// @brief 原始协议数据
    // std::shared_ptr<Protocol> ori_protocol_;
    /// @brief 已注册到路由层的ID
    int64_t reg_route_id_;
    /// @brief 校验过程  粗略校验？ 精细校验?

};


class HttpProtocolItem: public ProtocolItem
{
public:
    HttpProtocolItem();

    HttpProtocolItem(kit_muduo::HttpRequestPtr req_cfg, kit_muduo::HttpResponsePtr resp_cfg_);

    ~HttpProtocolItem() override = default;

    bool init(std::shared_ptr<Protocol> ori_protocol) override;


    bool setReqCfg(const nlohmann::json& req_json) override;
    bool setRespCfg(const nlohmann::json& resp_json) override;
    void setReqBody(const ProtocolBodyType body_type, const std::vector<char> &body_data) override;
    void setRespBody(const ProtocolBodyType body_type, const std::vector<char> &body_data) override;


    void setReqCfg(kit_muduo::HttpRequestPtr req_cfg);
    void setRespCfg(kit_muduo::HttpResponsePtr resp_cfg);

    kit_muduo::HttpRequestPtr getReq() const { return req_cfg_; }
    kit_muduo::HttpResponsePtr  getResp() const { return resp_cfg_;}

    std::string getReqPath() const;
private:
    // TODO: 实际的业务数据 需要和 配置数据进行拆分
    kit_muduo::HttpRequestPtr req_cfg_;
    
    kit_muduo::HttpResponsePtr resp_cfg_;
};

class CustomTcpProtocolItem: public ProtocolItem
{
public:
    explicit CustomTcpProtocolItem(std::shared_ptr<CustomTcpPattern> tcp_pattern);

    CustomTcpProtocolItem(std::shared_ptr<CustomTcpPattern> pattern, std::shared_ptr<CustomTcpMessage>req_cfg, std::shared_ptr<CustomTcpMessage> resp_cfg);

    bool init(std::shared_ptr<Protocol> ori_protocol) override;

    bool setReqCfg(const nlohmann::json& req_json) override;
    bool setRespCfg(const nlohmann::json& resp_json) override;
    void setReqBody(const ProtocolBodyType body_type, const std::vector<char> &body_data) override;
    void setRespBody(const ProtocolBodyType body_type, const std::vector<char> &body_data) override;

    void setReqCfg(std::shared_ptr<CustomTcpMessage> req_cfg) { req_cfg_msg_ = req_cfg; }
    void setRespCfg(std::shared_ptr<CustomTcpMessage> resp_cfg) { resp_cfg_msg_ = resp_cfg; }

    ~CustomTcpProtocolItem() override = default;

    std::shared_ptr<CustomTcpMessage> getReq() const { return req_cfg_msg_; }
    std::shared_ptr<CustomTcpMessage>  getResp() const { return resp_cfg_msg_; }

private:
    /// @brief 自定义tcp格式
    std::shared_ptr<CustomTcpPattern> tcp_pattern_;
    /// @brief 配置的接收解析tcp报文
    std::shared_ptr<CustomTcpMessage> req_cfg_msg_;
    /// @brief 配置的组装发送tcp响应报文
    std::shared_ptr<CustomTcpMessage> resp_cfg_msg_;
};


/**
 * @brief ProtocolItem工厂类，用于创建不同的ProtocolItem实例
 */
class ProtocolItemFactory
{
public:
    /**
     * @brief 根据配置协议类型创建对应的ProtocolItemr实例
     * @param ori_protocol 配置协议原始数据
     * @return 返回ProtocolItem的shared_ptr
     */
    static std::shared_ptr<ProtocolItem> Create(std::shared_ptr<Protocol> ori_protocol, std::shared_ptr<ProjectServer> pj_server);

};

}
#endif  // __KIT_DOMAIN_PROTOCOL_ITEM_H__