/**
 * @file protocol_item.cpp
 * @brief  测试服务实际协议项
 * @author ljk5
 * @version 1.0
 * @date 2025-08-26 16:19:53
 * @copyright Copyright (c) 2025 HIKRayin
 */
#include "domain/type.h"
#include "domain/domain_log.h"
#include "domain/protocol_item.h"
#include "domain/protocol.h"
#include "domain/custom_tcp_project_server.h"
#include "domain/http_protocol_item.h"
#include "domain/custom_tcp_protocol_item.h"
#include "domain/custom_tcp_field_model.h"
#include "net/http/multiform.h"

#include <algorithm>
#include <stdexcept>

using namespace kit_muduo;
namespace kit_domain {

void ProtocolItemBodyView::setBody(ProtocolBodyType new_body_type, const std::vector<char>& new_body_data)
{
    auto new_meta = ProtocolBodyTypeToHttpContentMeta(new_body_type);
    std::vector<char> wire_body;
    if(new_body_type == ProtocolBodyType::kBinary && !new_body_data.empty())
    {
        const auto [fields, total_len] = FieldValueMapParseFromJson(nlohmann::json::parse(new_body_data));
        wire_body.reserve(total_len);
        for(const auto& [pos, field] : fields)
        {
            if(pos < wire_body.size() || field.bytes.size() > field.spec.byte_len)
            {
                throw std::invalid_argument("binary body fields overlap or value exceeds field length");
            }
            if(field.spec.byte_len > wire_body.max_size()
                || pos > wire_body.max_size() - field.spec.byte_len)
            {
                throw std::length_error("binary body field range exceeds buffer size");
            }
            wire_body.resize(pos + field.spec.byte_len, 0);
            std::copy(field.bytes.begin(), field.bytes.end(), wire_body.begin() + pos);
        }
    }
    else if(new_body_type == ProtocolBodyType::kMultiForm)
    {
        constexpr const char* kBoundary = "KitProtocolFormBoundary";
        const auto form = new_body_data.empty() ? kit_muduo::http::MultiForm()
            : kit_muduo::http::MultiForm(new_body_data);
        const auto bytes = form.serialize(kBoundary);
        wire_body.assign(bytes.begin(), bytes.end());
        kit_muduo::http::SetContentTypeParam(new_meta, "boundary", kBoundary);
    }
    else
    {
        wire_body = new_body_data;
    }

    // 转换和分配全部成功后再提交，失败时保留原有快照。
    auto new_data = std::make_shared<const std::vector<char>>(std::move(wire_body));
    body_type = new_body_type;
    meta = std::move(new_meta);
    body_data = std::move(new_data);
}

void ProtocolItem::setId(int64_t id)
{
    id_ = id;
}

int64_t ProtocolItem::getId() const { return id_; }

std::string ProtocolItem::getName() const { return name_; }

int64_t ProtocolItem::getProjectId() const { return project_id_; }

bool ProtocolItem::isEndian() const { return is_endian_; }

void ProtocolItem::initBase(const Protocol& p)
{
    id_ = p.m_id;
    name_ = p.m_name;
    type_ = p.m_type;
    project_id_ = p.m_projectId;
    is_endian_ = p.m_isEndian;

    req_body_view_.setBody(p.m_reqBodyType, p.m_reqBodyData);
    resp_body_view_.setBody(p.m_respBodyType, p.m_respBodyData);

    record_cache_ =std::make_shared<InteractionRecordCache>(InteractionRecordCacheKey{
        .scope = InteractionScope::kProtocol,
        .project_id = project_id_,
        .protocol_id = id_,
    });
}


void ProtocolItem::setReqBody(const ProtocolBodyType body_type, const std::vector<char> &body_data)
{
    req_body_view_.setBody(body_type, body_data);
}

void ProtocolItem::setRespBody(const ProtocolBodyType body_type, const std::vector<char> &body_data)
{
    resp_body_view_.setBody(body_type, body_data);
}

ProtocolItemBodyView ProtocolItem::getReqBodyView() const { return req_body_view_; }
ProtocolItemBodyView ProtocolItem::getRespBodyView() const { return resp_body_view_; }



std::shared_ptr<ProtocolItem> ProtocolItemFactory::Create(std::shared_ptr<Protocol> ori_protocol, std::shared_ptr<ProjectServer> pj_server)
{
    switch (ori_protocol->m_type) 
    {
        // 创建HTTP协议项
        // TODO 后续HTTPS可以单独分出去
        case ProtocolType::kHttp:
        case ProtocolType::kHttps: 
        {
            auto p = std::make_shared<HttpProtocolItem>();
            if(!p || !p->init(ori_protocol))
            {
                break;
            }
            return p;
        }
        // 创建自定义TCP协议项
        case ProtocolType::kCustomTcp: 
        {
            // 自定义TCP需要额外传入格式信息
            auto tcp_server = std::dynamic_pointer_cast<CustomTcpProjectServer>(pj_server);
            auto p = std::make_shared<CustomTcpProtocolItem>(tcp_server->GetPatternInfo());
            if(!p || !p->init(ori_protocol))
            {
                break;
            }

            return p;
        }
        default: 
        {
            return nullptr;
        }
    }
    return nullptr;
}


}
