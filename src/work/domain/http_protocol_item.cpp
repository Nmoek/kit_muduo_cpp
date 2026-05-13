/**
 * @file http_protocol_item.cpp
 * @brief HTTP测试协议项
 * @author Kewin Li
 * @version 1.0
 * @date 2026-05-12 19:03:35
 * @copyright Copyright (c) 2026 Kewin Li
 */
#include "domain/http_protocol_item.h"
#include "net/http/http_request.h"
#include "net/http/http_response.h"
#include "domain/type.h"
#include "domain/protocol.h"

#include <stdexcept>
#include <vector>
#include <memory>


using namespace kit_muduo;
using namespace kit_muduo::http;

namespace kit_domain {


HttpItemReqHeaderCfg::HttpItemReqHeaderCfg(const nlohmann::json &req_json)
{
    if(!fromJson(req_json))
    {
        throw std::runtime_error("req json parse error!");
    }
}

HttpItemReqHeaderCfg::HttpItemReqHeaderCfg(HttpRequestPtr req_cfg)
{
    if(!fromNetHttpReq(req_cfg))
    {
        throw std::runtime_error("req cfg parse error!");
    }
}

bool HttpItemReqHeaderCfg::fromNetHttpReq(HttpRequestPtr req_cfg)
{
    if(!req_cfg)
    {
        PC_F_ERROR("net http req null! \n");
        return false;
    }
    method = req_cfg->method();
    path = req_cfg->path();
    headers = req_cfg->headers();
    return true;
}

bool HttpItemReqHeaderCfg::fromJson(const nlohmann::json& req_json)
{
    if(req_json.empty())
    {
        PC_F_ERROR("json is null! \n");
        return false;
    }
    auto it = req_json.find("method");
    if(it == req_json.end())
    {
        PC_F_WARN("json not found 'method' field! \n");
        return false;
    }
    method = HttpRequest::Method::FromString(it.value());
    


    it = req_json.find("path");
    if(it == req_json.end())
    {
        PC_F_WARN("json not found 'path' field! \n");
        return false;
    }
    path = std::move(it.value());
    
    
    it = req_json.find("headers");
    if(it == req_json.end())
    {
        PC_F_WARN("json not found 'headers' field! \n");
        return false;
    }
    headers = std::move(it.value());

    return true;
}


HttpItemRespHeaderCfg::HttpItemRespHeaderCfg(const nlohmann::json &resp_json)
{
    if(!fromJson(resp_json))
    {
        throw std::runtime_error("resp json parse error!");
    }
}
HttpItemRespHeaderCfg::HttpItemRespHeaderCfg(kit_muduo::HttpResponsePtr resp_cfg)
{
    if(!fromNetHttpResp(resp_cfg))
    {
        throw std::runtime_error("resp cfg parse error!");
    }
}

bool HttpItemRespHeaderCfg::fromNetHttpResp(kit_muduo::HttpResponsePtr resp_cfg)
{
    if(!resp_cfg)
    {
        PC_F_ERROR("net http resp null!");
        return false;
    }
    version = resp_cfg->version();
    state_code = resp_cfg->stateCode();
    headers = resp_cfg->headers();
    return true;
}

bool HttpItemRespHeaderCfg::fromJson(const nlohmann::json &resp_json)
{
    if(resp_json.empty())
    {
        PC_F_ERROR("json is null!");
        return false;
    }
    // version 暂时不进行配置
    version.set(Version::kHttp11);
    
    auto it = resp_json.find("status_code");
    if(it == resp_json.end())
    {
        PC_F_WARN("json not found 'status_code' field! \n");
        return false;
    }
    state_code = StateCode::FromString(it.value());
    
    
    it = resp_json.find("headers");
    if(it == resp_json.end())
    {
        PC_F_WARN("json not found 'headers' field! \n");
        return false;
    }
    headers = std::move(it.value());
    

    return true;
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
    const nljson& resp_cfg_root = ori_protocol->m_respCfg;

    /*****请求****/
    /**TODO 配置这部分按TCP的做法 整个进行json自定义转换**/
    if(!req_cfg_.fromJson(req_cfg_root))
    {
        PC_F_ERROR("req cfg json parse error! %s\n", req_cfg_root.dump().c_str());
        return false;
    }

    req_body_view_.body_type = ProtocolBodyTypeToContentType(ori_protocol->m_reqBodyType);
    req_body_view_.body_data = std::make_shared<const std::vector<char>>(ori_protocol->m_reqBodyData);
    
    /*****响应****/
    // int32_t status_code = resp_cfg_root.value("status_code", 0); 
    // 状态码 协议版本 暂时不配
    if(!resp_cfg_.fromJson(resp_cfg_root))
    {
        PC_F_ERROR("resp cfg json parse error! %s\n", resp_cfg_root.dump().c_str());
        return false;
    }
    
    resp_body_view_.body_type = ProtocolBodyTypeToContentType(ori_protocol->m_respBodyType);
    resp_body_view_.body_data = std::make_shared<const std::vector<char>>(ori_protocol->m_respBodyData);

    return true;
}



bool HttpProtocolItem::setReqCfg(kit_muduo::HttpRequestPtr req_cfg)
{
    return req_cfg_.fromNetHttpReq(req_cfg);
}

bool HttpProtocolItem::setReqCfg(const nlohmann::json& req_json)
{
    return req_cfg_.fromJson(req_json);
}

bool HttpProtocolItem::setRespCfg(kit_muduo::HttpResponsePtr resp_cfg)
{
    return resp_cfg_.fromNetHttpResp(resp_cfg);
}

bool HttpProtocolItem::setRespCfg(const nlohmann::json& resp_json)
{
    return resp_cfg_.fromJson(resp_json);
}

void HttpProtocolItem::setReqCfg(const HttpItemReqHeaderCfg &req_cfg)
{
    req_cfg_ = req_cfg;
}

void HttpProtocolItem::setRespCfg(const HttpItemRespHeaderCfg &resp_cfg)
{
    resp_cfg_ = resp_cfg;
}



}