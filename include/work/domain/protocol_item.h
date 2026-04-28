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

#include "net/call_backs.h"
#include "domain/type.h"

#include <memory>
#include <string>
#include <vector>
#include <stdexcept>

namespace kit_domain {

class Protocol;
class CustomTcpMessage;
class ProjectServer;
class CustomTcpPattern;

class ProtocolItem
{
public:
    using Ptr = std::shared_ptr<ProtocolItem>;

    explicit ProtocolItem(std::shared_ptr<Protocol> ori_protocol);
    
    virtual ~ProtocolItem() = 0;

    int64_t getId() const;

    std::string getName() const;

    int64_t getProjectId() const;

    bool isEndian() const;

    std::shared_ptr<Protocol> getOriProtocol() const { return ori_protocol_; }

protected:
    /// @brief 原始协议数据
    std::shared_ptr<Protocol> ori_protocol_;
    /// @brief 校验过程  粗略校验？ 精细校验?

};


class HttpProtocolItem: public ProtocolItem
{
public:
    explicit HttpProtocolItem(std::shared_ptr<Protocol> ori_protocol);

    HttpProtocolItem(kit_muduo::HttpRequestPtr req_cfg, kit_muduo::HttpResponsePtr resp_cfg_);

    ~HttpProtocolItem() override = default;

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
    explicit CustomTcpProtocolItem(std::shared_ptr<Protocol> ori_protocol, std::shared_ptr<CustomTcpPattern> pattern);

    CustomTcpProtocolItem(std::shared_ptr<CustomTcpPattern> pattern, std::shared_ptr<CustomTcpMessage>req_cfg, std::shared_ptr<CustomTcpMessage> resp_cfg);


    ~CustomTcpProtocolItem() override = default;

    std::shared_ptr<CustomTcpMessage> getReq() const { return req_cfg_msg_; }
    std::shared_ptr<CustomTcpMessage>  getResp() const { return resp_cfg_msg_; }

private:
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