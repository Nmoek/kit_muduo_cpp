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
#include "net/http/http_request.h"
#include "net/http/http_response.h"
#include "domain/custom_tcp_message.h"
#include "domain/custom_tcp_pattern.h"
#include "domain/project_server.h"

using namespace kit_muduo;
using namespace kit_muduo::http;



namespace kit_domain {


int64_t ProtocolItem::getId() const { return id_; }

std::string ProtocolItem::getName() const { return name_; }

int64_t ProtocolItem::getProjectId() const { return project_id_; }

void ProtocolItem::setRegRouteId(int64_t route_id) { reg_route_id_ = route_id; }
int64_t ProtocolItem::getRegRouteId() const { return reg_route_id_; }

bool ProtocolItem::isEndian() const { return is_endian_; }

HttpProtocolItem::HttpProtocolItem()
    :req_cfg_(std::make_shared<HttpRequest>())
    ,resp_cfg_(std::make_shared<HttpResponse>())
{

}

bool HttpProtocolItem::init(std::shared_ptr<Protocol> ori_protocol)
{
    if(!ori_protocol)
    {
        PC_F_ERROR("ori protocol data is null\n");
        return false;
    }
    // 基本信息赋值
    id_ = ori_protocol->m_id;
    name_ = ori_protocol->m_name;
    project_id_ = ori_protocol->m_projectId;
    is_endian_ = ori_protocol->m_isEndian;

    const nljson& req_cfg_root = ori_protocol->m_reqCfg;
    const nljson& resp_cfg_root = ori_protocol->m_reqCfg;

    /*****请求****/
    /**TODO 配置这部分按TCP的做法 整个进行json自定义转换**/
    if(!setReqCfg(req_cfg_root))
    {
        PC_F_ERROR("req cfg json parse error! %s\n", req_cfg_root.dump().c_str());
        return false;
    }

    req_cfg_->setBody(Body(ProtocolBodyTypeToContentType(ori_protocol->m_reqBodyType), ori_protocol->m_reqBodyData));
    
    /*****响应****/
    // int32_t status_code = resp_cfg_root.value("status_code", 0); 
    // 状态码 协议版本 暂时不配
    if(!setRespCfg(resp_cfg_root))
    {
        PC_F_ERROR("resp cfg json parse error! %s\n", resp_cfg_root.dump().c_str());
        return false;
    }
    
    resp_cfg_->setBody(Body(ProtocolBodyTypeToContentType(ori_protocol->m_respBodyType), ori_protocol->m_respBodyData));

    return true;
}

HttpProtocolItem::HttpProtocolItem(HttpRequestPtr req_cfg, HttpResponsePtr resp_cfg)
    :req_cfg_(req_cfg)
    ,resp_cfg_(resp_cfg)
{

}

void HttpProtocolItem::setReqCfg(kit_muduo::HttpRequestPtr req_cfg)
{
    req_cfg_ = req_cfg;
}

bool HttpProtocolItem::setReqCfg(const nlohmann::json& req_json)
{
    if(req_json.empty())
    {
        PC_F_ERROR("json is null! \n");
        return false;
    }
    auto it = req_json.find("method");
    if(it == req_json.end())
    {
        PC_F_ERROR("json get 'method' field error! \n");
        return false;
    }
    req_cfg_->setMethod(HttpRequest::Method::FromString(it.value()));

    it = req_json.find("path");
    if(it == req_json.end())
    {
        PC_F_ERROR("json get 'path' field error! \n");
        return false;
    }
    req_cfg_->setPath(it.value());

    
    it = req_json.find("headers");
    if(it == req_json.end())
    {
        PC_F_ERROR("json get 'headers' field error! \n");
        return false;
    }
    req_cfg_->setHeaders(it.value());

    return true;
}
void HttpProtocolItem::setRespCfg(kit_muduo::HttpResponsePtr resp_cfg)
{
    resp_cfg_ = resp_cfg;
}

bool HttpProtocolItem::setRespCfg(const nlohmann::json& resp_json)
{
    if(resp_json.empty())
    {
        return false;
    }
    // version 暂时不进行配置
    resp_cfg_->setVersion(Version::kHttp11);
    
    auto it = resp_json.find("status_code");
    if(it == resp_json.end())
    {
        PC_F_ERROR("json get 'status_code' field error! \n");
        return false;
    }
    resp_cfg_->setStateCode(StateCode::FromString(it.value()));

    
    it = resp_json.find("headers");
    if(it == resp_json.end())
    {
        PC_F_ERROR("json get 'headers' field error! \n");
        return false;
    }
    resp_cfg_->setHeaders(it.value());

    return true;
}


void HttpProtocolItem::setReqBody(const ProtocolBodyType body_type, const std::vector<char> &body_data)
{
    req_cfg_->setBody(Body(ProtocolBodyTypeToContentType(body_type), body_data));

}

void HttpProtocolItem::setRespBody(const ProtocolBodyType body_type, const std::vector<char> &body_data)
{
    resp_cfg_->setBody(Body(ProtocolBodyTypeToContentType(body_type), body_data));
}




std::string HttpProtocolItem::getReqPath() const { return req_cfg_->path(); }

CustomTcpProtocolItem::CustomTcpProtocolItem(std::shared_ptr<CustomTcpPattern> tcp_pattern)
    :tcp_pattern_(tcp_pattern)
    ,req_cfg_msg_(std::make_shared<CustomTcpMessage>())
    ,resp_cfg_msg_(std::make_shared<CustomTcpMessage>())
{

}


CustomTcpProtocolItem::CustomTcpProtocolItem(std::shared_ptr<CustomTcpPattern> tcp_pattern, std::shared_ptr<CustomTcpMessage> received_cfg, std::shared_ptr<CustomTcpMessage> will_send_cfg)
    :tcp_pattern_(tcp_pattern)
    ,req_cfg_msg_(received_cfg)
    ,resp_cfg_msg_(will_send_cfg)
{


}


bool CustomTcpProtocolItem::init(std::shared_ptr<Protocol> ori_protocol)
{
    if(!ori_protocol || !tcp_pattern_)
    {
        PC_F_ERROR("ori protocol/ pattern data is null\n");
        return false;
    }

    const nljson &j_req = ori_protocol->m_reqCfg;

    const nljson &j_resp = ori_protocol->m_respCfg;
    
    /*注意：实践可以发现 只有从对端收到数据需要校验的那一边(收到请求边/收到响应边)才需要提前知道格式!
    
    只要是服务器回发处理的流程根本不需要格式, 只需要能够外部输入即可, 由用户自己保障 格式正确性 + 数据正确性即可!
    */

    if(!setReqCfg(j_req))
    {
        PC_F_ERROR("req cfg json parse error! %s\n", j_req.dump().c_str());
        return false;
    }


    req_cfg_msg_->setBody(Body(ProtocolBodyTypeToContentType(ori_protocol->m_reqBodyType), ori_protocol->m_reqBodyData));

    if(!setRespCfg(j_resp))
    {
        PC_F_ERROR("resp cfg json parse error! %s\n", j_resp.dump().c_str());
        return false;
    }

    resp_cfg_msg_->setBody(Body(ProtocolBodyTypeToContentType(ori_protocol->m_respBodyType), ori_protocol->m_respBodyData));

    PC_DEBUG() << "resp body:: " << resp_cfg_msg_->body().toString() << std::endl;

    return true;
}

bool CustomTcpProtocolItem::setReqCfg(const nlohmann::json& req_json)
{
    if(req_json.empty())
    {
        PC_F_ERROR("json is null\n");
        return false;
    }
    auto it = req_json.find("common_fields");
    if(it == req_json.end())
    {
        PC_F_ERROR("json get 'common_fields' field error! \n");
        return false;
    }

    // 填充普通字段
    for(auto &obj : it.value())
    {
        auto cfg_field = CustomTcpPatternFieldFactory::Create(obj);
        if(!cfg_field)
        {
            PC_F_WARN("config field is null! %s\n", obj.dump().c_str());
            continue;
        }

        PC_F_ERROR("Field: name[%s], idx[%d], byte_pos[%d], byte_len[%d], value[%s]\n", 
            cfg_field->name().c_str(),cfg_field->idx(), cfg_field->byte_pos(), cfg_field->byte_len(), cfg_field->toHexString().c_str());
        

        cfg_field->setSepcial(false);
        req_cfg_msg_->addField(cfg_field);
    }
    
    // copy特殊字段并且填充
    for(auto &f : tcp_pattern_->getSpecialFields())
    {
        auto special_field = f ? f->clone() : nullptr;
        if(!special_field)
        {
            PC_F_WARN("special field is null!\n");
            continue;
        }

        special_field->setSepcial(true);
        req_cfg_msg_->addField(special_field);
    }

    // 填充功能码字段
    it = req_json.find("function_code_filed_value");
    if(it == req_json.end())
    {
        PC_F_ERROR("json get 'function_code_filed_value' field error! \n");
        return false;
    }

    std::string func_code_str = it.value();

    PC_DEBUG() << "req function_code_filed_value: " << func_code_str << std::endl;
    req_cfg_msg_->setFunctionCodeFieldValue(func_code_str);

    req_cfg_msg_->getField(tcp_pattern_->functionCodeField()->byte_pos())->fromHexString(func_code_str);
    
    /** 调试打印 **/
    for(auto &field : req_cfg_msg_->headerFields())
    {
        if(field)
        {
            PC_F_ERROR("Req Field: name[%s], idx[%d], byte_pos[%d], byte_len[%d], value[%s]\n", 
                field->name().c_str(),field->idx(), field->byte_pos(), field->byte_len(), field->toHexString().c_str());
        }
        else
        {
            PC_F_ERROR("Field is null! \n");
        }

    }
    /** 调试打印 **/
    return true;
}

bool CustomTcpProtocolItem::setRespCfg(const nlohmann::json& resp_json)
{
    if(resp_json.empty())
    {
        PC_F_ERROR("json is null\n");
        return false;
    }

    auto it = resp_json.find("common_fields");
    if(it == resp_json.end())
    {
        PC_F_ERROR("json get 'common_fields' field error! \n");
        return false;
    }
    
    for(auto &obj : resp_json["common_fields"])
    {
        auto cfg_field = CustomTcpPatternFieldFactory::Create(obj);
        if(!cfg_field)
        {
            PC_F_WARN("config field is null! %s\n", obj.dump(4).c_str());
            continue;
        }
        cfg_field->setSepcial(false);
        resp_cfg_msg_->addField(cfg_field);
    }

    /*!!! 对外发送的报文 特殊字段不进行获取 */
#if 1
    for(auto &f : tcp_pattern_->getSpecialFields())
    {
        auto special_field = f ? f->clone() : nullptr;
        if(!special_field)
        {
            PC_F_WARN("special field is null!\n");
            continue;
        }
        special_field->setSepcial(true);
        resp_cfg_msg_->addField(special_field);
    }
#endif

    /** 调试打印 **/
    /** 调试打印 **/
    for(auto &field : resp_cfg_msg_->headerFields())
    {
        if(field)
        {
            PC_F_ERROR("Resp Field: name[%s], idx[%d], byte_pos[%d], byte_len[%d], value[%s]\n", 
                field->name().c_str(),field->idx(), field->byte_pos(), field->byte_len(), field->toHexString().c_str());
        }
        else
        {
            PC_F_ERROR("Field is null! \n");
        }

    }
    /** 调试打印 **/

    // 响应的功能码值填充
    // 填充功能码字段
    it = resp_json.find("function_code_filed_value");
    if(it == resp_json.end())
    {
        PC_F_ERROR("json get 'function_code_filed_value' field error! \n");
        return false;
    }
    std::string func_code_str = it.value();

    PC_DEBUG() << "resp function_code_filed_value: " << func_code_str << std::endl;

    resp_cfg_msg_->setFunctionCodeFieldValue(func_code_str);

    resp_cfg_msg_->getField(tcp_pattern_->functionCodeField()->byte_pos())->fromHexString(func_code_str);

    return true;
}

void CustomTcpProtocolItem::setReqBody(const ProtocolBodyType body_type, const std::vector<char> &body_data)
{
    req_cfg_msg_->setBody(Body(ProtocolBodyTypeToContentType(body_type), body_data));

}

void CustomTcpProtocolItem::setRespBody(const ProtocolBodyType body_type, const std::vector<char> &body_data)
{
    resp_cfg_msg_->setBody(Body(ProtocolBodyTypeToContentType(body_type), body_data));
}


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