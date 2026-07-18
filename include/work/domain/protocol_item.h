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
#include "domain/type.h"

#include <memory>
#include <string>

namespace kit_muduo::http {
struct RouteResult;
}

namespace kit_domain {

class Protocol;

struct ProtocolItemBodyView
{
    ProtocolBodyType body_type{ProtocolBodyType::kNone};
    std::shared_ptr<const std::vector<char>> body_data{std::make_shared<const std::vector<char>>()};

    ProtocolItemBodyView() = default;
    ProtocolItemBodyView(ProtocolBodyType body_type, const std::vector<char>& body_data)
    {
        setBody(body_type, body_data);
    }

    void setBody(ProtocolBodyType new_body_type, const std::vector<char>& new_body_data);
};

class ProtocolItem
{
public:
    using Ptr = std::shared_ptr<ProtocolItem>;

    virtual ~ProtocolItem() = default;

    virtual bool init(std::shared_ptr<Protocol> ori_protocol) = 0;
    virtual bool setReqCfg(const nlohmann::json& req_json) = 0;
    virtual bool setRespCfg(const nlohmann::json& resp_json) = 0;

    void setReqBody(const ProtocolBodyType body_type, const std::vector<char> &body_data);
    void setRespBody(const ProtocolBodyType body_type, const std::vector<char> &body_data);

    ProtocolItemBodyView getReqBodyView() const;
    ProtocolItemBodyView getRespBodyView() const;

    void setId(int64_t id);
    int64_t getId() const;

    std::string getName() const;

    int64_t getProjectId() const;

    bool isEndian() const;

    std::shared_ptr<InteractionRecordCache> cache() const { return record_cache_; }

protected:
    void initBase(const Protocol& p);

protected:
    int64_t id_;
    std::string name_;         // 测试协议名称
    ProtocolType type_;         // 测试协议类型
    int64_t project_id_;    // 所属测试服务Id
    bool  is_endian_;     // 是否大小端转换

    /// @brief 请求Body快照
    ProtocolItemBodyView req_body_view_;
    /// @brief 响应Body快照
    ProtocolItemBodyView resp_body_view_;
    /// @brief 协议项交互实时流缓存
    std::shared_ptr<InteractionRecordCache> record_cache_;

    /// @brief TODO 校验过程  粗略校验？ 精细校验?
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
