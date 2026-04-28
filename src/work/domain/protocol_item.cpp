/**
 * @file protocol_item.cpp
 * @brief  测试服务实际协议项
 * @author ljk5
 * @version 1.0
 * @date 2025-08-26 16:19:53
 * @copyright Copyright (c) 2025 HIKRayin
 */
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


ProtocolItem::ProtocolItem(std::shared_ptr<Protocol> ori_protocol)
    :ori_protocol_(ori_protocol)
{

}

ProtocolItem::~ProtocolItem() {}


int64_t ProtocolItem::getId() const { return ori_protocol_->m_id; }

std::string ProtocolItem::getName() const { return ori_protocol_->m_name; }

int64_t ProtocolItem::getProjectId() const { return ori_protocol_->m_projectId; }


bool ProtocolItem::isEndian() const { return ori_protocol_->m_isEndian; }

HttpProtocolItem::HttpProtocolItem(std::shared_ptr<Protocol> ori_protocol)
    :ProtocolItem(ori_protocol)
{
    if(!ori_protocol_)
        throw std::invalid_argument("origin protocol is null!");

    req_cfg_ = std::make_shared<HttpRequest>();
    resp_cfg_ = std::make_shared<HttpResponse>();

    const nljson& req_cfg_root = ori_protocol_->m_reqCfg;
    const nljson& resp_cfg_root = ori_protocol_->m_reqCfg;

    /*****请求****/
    /**TODO 配置这部分按TCP的做法 整个进行json自定义转换**/
    req_cfg_->setMethod(HttpRequest::Method::FromString(req_cfg_root.value("method", "")));

    req_cfg_->setPath(req_cfg_root.value("path", ""));
    req_cfg_->setHeaders(req_cfg_root.value("headers", std::unordered_map<std::string, std::string>()));
    

    req_cfg_->setBody(Body(ProtocolBodyTypeToContentType(ori_protocol_->m_reqBodyType), ori_protocol_->m_reqBodyData));
    
    /*****响应****/
    // int32_t status_code = resp_cfg_root.value("status_code", 0); 
    // 状态码 协议版本 暂时不配
    resp_cfg_->setVersion(Version::kHttp11);
    resp_cfg_->setStateCode(StateCode::k200Ok);

    resp_cfg_->setHeaders(resp_cfg_root.value("headers", std::unordered_map<std::string, std::string>()));

    
    resp_cfg_->setBody(Body(ProtocolBodyTypeToContentType(ori_protocol_->m_respBodyType), ori_protocol_->m_respBodyData));
}

HttpProtocolItem::HttpProtocolItem(HttpRequestPtr req_cfg, HttpResponsePtr resp_cfg)
    :ProtocolItem(nullptr)
    ,req_cfg_(req_cfg)
    ,resp_cfg_(resp_cfg)
{

}

std::string HttpProtocolItem::getReqPath() const { return req_cfg_->path(); }

CustomTcpProtocolItem::CustomTcpProtocolItem(std::shared_ptr<Protocol> ori_protocol, std::shared_ptr<CustomTcpPattern> pattern)
    :ProtocolItem(ori_protocol)
{
    if(!ori_protocol_ || !pattern)
        throw std::invalid_argument("param is null!");

    const nljson &j_req = ori_protocol_->m_reqCfg;

    const nljson &j_resp = ori_protocol_->m_respCfg;
    
    /*注意：实践可以发现 只有从对端收到数据需要校验的那一边(收到请求边/收到响应边)才需要提前知道格式!
    
    只要是服务器回发处理的流程根本不需要格式, 只需要能够外部输入即可, 由用户自己保障 格式正确性 + 数据正确性即可!
    */
    int32_t req_header_field_num = pattern->getSpecialFields().size() + j_req["common_fields"].size();

    // 对外发送的cfg都需要用户自己全部配置
    int32_t resp_header_field_num = pattern->getSpecialFields().size() + j_resp["common_fields"].size();

    //!!!! int32_t resp_header_field_num = j_req["common_fields"].size();

    // 数量计算有问题！需要重新考虑一种机制
    // req_cfg_msg_ = std::make_shared<CustomTcpMessage>(req_header_field_num);
    // resp_cfg_msg_ = std::make_shared<CustomTcpMessage>(resp_header_field_num);

    req_cfg_msg_ = std::make_shared<CustomTcpMessage>(20);
    resp_cfg_msg_ = std::make_shared<CustomTcpMessage>(20);

    PC_F_ERROR("%d, %d \n", req_header_field_num, resp_header_field_num);

    /****接收****/
    // 填充普通字段
    for(auto &obj : j_req["common_fields"])
    {
        auto cfg_field = CustomTcpPatternFieldFactory::Create(obj);
        if(!cfg_field)
        {
            PC_F_WARN("config field is null! %s\n", obj.dump(4).c_str());
            continue;
        }

        PC_F_ERROR("Field: name[%s], idx[%d], byte_pos[%d], byte_len[%d], value[%s]\n", 
            cfg_field->name().c_str(),cfg_field->idx(), cfg_field->byte_pos(), cfg_field->byte_len(), cfg_field->toHexString().c_str());
        

        cfg_field->setSepcial(false);
        req_cfg_msg_->addField(cfg_field->idx(), cfg_field);
    }
    
    // copy特殊字段并且填充
    for(auto &f : pattern->getSpecialFields())
    {
        auto special_field = f ? f->clone() : nullptr;
        if(!special_field)
        {
            PC_F_WARN("special field is null!\n");
            continue;
        }

        special_field->setSepcial(true);
        req_cfg_msg_->addField(special_field->idx(), special_field);
    }

    // 填充功能码字段
    std::string func_code_str = j_req.value("function_code_filed_value", "");

    PC_DEBUG() << "req function_code_filed_value: " << func_code_str << std::endl;
    req_cfg_msg_->setFunctionCodeFieldValue(func_code_str);

    req_cfg_msg_->getField(pattern->functionCodeField()->idx())->fromHexString(func_code_str);
    
    /** 调试打印 **/
    for(int i = 0;i < req_cfg_msg_->getFieldNums(); ++i)
    {
        auto field = req_cfg_msg_->getField(i);
        if(field)
        {
            PC_F_ERROR("Field: name[%s], idx[%d], byte_pos[%d], byte_len[%d], value[%s]\n", 
                field->name().c_str(),field->idx(), field->byte_pos(), field->byte_len(), field->toHexString().c_str());
        }
        else
        {
            PC_F_ERROR("Field: %d is null! \n", i);
        }

    }
    /** 调试打印 **/

    req_cfg_msg_->setBody(Body(ProtocolBodyTypeToContentType(ori_protocol_->m_reqBodyType), ori_protocol_->m_reqBodyData));

    /****响应****/
    for(auto &obj : j_resp["common_fields"])
    {
        auto cfg_field = CustomTcpPatternFieldFactory::Create(obj);
        if(!cfg_field)
        {
            PC_F_WARN("config field is null! %s\n", obj.dump(4).c_str());
            continue;
        }
        cfg_field->setSepcial(false);
        resp_cfg_msg_->addField(cfg_field->idx(), cfg_field);
    }

    /*!!! 对外发送的报文 特殊字段不进行获取 */
#if 1
    for(auto &f : pattern->getSpecialFields())
    {
        auto special_field = f ? f->clone() : nullptr;
        if(!special_field)
        {
            PC_F_WARN("special field is null!\n");
            continue;
        }
        special_field->setSepcial(true);
        resp_cfg_msg_->addField(special_field->idx(), special_field);
    }
#endif

    /** 调试打印 **/
    for(int i = 0;i < resp_cfg_msg_->getFieldNums(); ++i)
    {
        auto field = resp_cfg_msg_->getField(i);
        if(field)
        {
            PC_F_ERROR("Field: name[%s], idx[%d], byte_pos[%d], byte_len[%d], value[%s]\n", 
                field->name().c_str(),field->idx(), field->byte_pos(), field->byte_len(), field->toHexString().c_str());
        }
        else
        {
            PC_F_ERROR("Field: %d is null! \n", i);
        }

    }
    /** 调试打印 **/

    // 响应的功能码值填充
    func_code_str = j_resp.value("function_code_filed_value", "");
    PC_DEBUG() << "resp function_code_filed_value: " << func_code_str << std::endl;

    resp_cfg_msg_->setFunctionCodeFieldValue(func_code_str);

    resp_cfg_msg_->getField(pattern->functionCodeField()->idx())->fromHexString(func_code_str);
    
    

    resp_cfg_msg_->setBody(Body(ProtocolBodyTypeToContentType(ori_protocol_->m_respBodyType), ori_protocol_->m_respBodyData));

    PC_DEBUG() << "resp body:: " << resp_cfg_msg_->body().toString() << std::endl;
}


CustomTcpProtocolItem::CustomTcpProtocolItem(std::shared_ptr<CustomTcpPattern> pattern, std::shared_ptr<CustomTcpMessage> received_cfg, std::shared_ptr<CustomTcpMessage> will_send_cfg)
    :ProtocolItem(nullptr)
    ,req_cfg_msg_(received_cfg)
    ,resp_cfg_msg_(will_send_cfg)
{
    if(!pattern)
        throw std::invalid_argument("pattern is null!");

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
            return std::make_shared<HttpProtocolItem>(ori_protocol);
        }
        // 创建自定义TCP协议项
        case ProtocolType::CUSTOM_TCP_PROTOCOL: 
        {
            // TODO 传入格式信息即可 不需要传整个服务器对象
            auto tcp_server = std::dynamic_pointer_cast<CustomTcpProjectServer>(pj_server);
            return std::make_shared<CustomTcpProtocolItem>(ori_protocol, tcp_server->getPatternInfo());
        }
        
        case ProtocolType::UNKNOWN_PROTOCOL:
        default: 
        {
            return nullptr;
        }
    }
}


}