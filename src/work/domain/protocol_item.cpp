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
#include "domain/project_server.h"
#include "domain/http_protocol_item.h"
#include "domain/custom_tcp_protocol_item.h"

using namespace kit_muduo;
namespace kit_domain {

void ProtocolItemBodyView::setBody(ProtocolBodyType new_body_type, const std::vector<char>& new_body_data)
{
    body_type = new_body_type;
    body_data = std::make_shared<const std::vector<char>>(new_body_data);
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
