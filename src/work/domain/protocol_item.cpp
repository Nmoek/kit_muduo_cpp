/**
 * @file protocol_item.cpp
 * @brief  测试服务实际协议项
 * @author ljk5
 * @version 1.0
 * @date 2025-08-26 16:19:53
 * @copyright Copyright (c) 2025 HIKRayin
 */
#include "domain/type.h"
#include "net/http/http_util.h"
#include "web/web_log.h"
#include "domain/protocol_item.h"
#include "domain/protocol.h"
#include "domain/project_server.h"
#include "domain/http_protocol_item.h"
#include "domain/custom_tcp_protocol_item.h"

using namespace kit_muduo;
using namespace kit_muduo::http;



namespace kit_domain {


int64_t ProtocolItem::getId() const { return id_; }

std::string ProtocolItem::getName() const { return name_; }

int64_t ProtocolItem::getProjectId() const { return project_id_; }

bool ProtocolItem::isEndian() const { return is_endian_; }


void ProtocolItem::setReqBody(const ProtocolBodyType body_type, const std::vector<char> &body_data)
{
    req_body_view_.body_type = ProtocolBodyTypeToContentType(body_type);
    req_body_view_.body_data = std::make_shared<const std::vector<char>>(body_data);
}

void ProtocolItem::setRespBody(const ProtocolBodyType body_type, const std::vector<char> &body_data)
{
    resp_body_view_.body_type = ProtocolBodyTypeToContentType(body_type);
    resp_body_view_.body_data = std::make_shared<const std::vector<char>>(body_data);
}

ProtocolItemBodyView ProtocolItem::getReqBodyView() const { return req_body_view_; }
ProtocolItemBodyView ProtocolItem::getRespBodyView() const { return resp_body_view_; }



std::shared_ptr<ProtocolItem> ProtocolItemFactory::Create(std::shared_ptr<Protocol> ori_protocol, std::shared_ptr<ProjectServer> pj_server)
{
    switch (ori_protocol->m_type) 
    {
        // 创建HTTP协议项
        // TODO 后续HTTPS可以单独分出去
        case ProtocolType::HTTP_PROTOCOL:
        case ProtocolType::HTTPS_PROTOCOL: 
        {
            auto p = std::make_shared<HttpProtocolItem>();
            if(!p || !p->init(ori_protocol))
            {
                break;
            }
            return p;
        }
        // 创建自定义TCP协议项
        case ProtocolType::CUSTOM_TCP_PROTOCOL: 
        {
            // 自定义TCP需要额外传入格式信息
            auto tcp_server = std::dynamic_pointer_cast<CustomTcpProjectServer>(pj_server);
            auto p = std::make_shared<CustomTcpProtocolItem>(tcp_server->getPatternInfo());
            if(!p || !p->init(ori_protocol))
            {
                break;
            }

            return p;
        }
        
        case ProtocolType::UNKNOWN_PROTOCOL:
        default: 
        {
            return nullptr;
        }
    }
    return nullptr;
}


}