/**
 * @file http_protocol_item.h
 * @brief HTTP测试协议项
 * @author Kewin Li
 * @version 1.0
 * @date 2026-05-12 18:48:04
 * @copyright Copyright (c) 2026 Kewin Li
 */
#ifndef __KIT_HTTP_PROTOCOL_ITEM_H__
#define __KIT_HTTP_PROTOCOL_ITEM_H__

#include "domain/protocol_item.h"
#include "net/http/http_request.h"
#include "net/http/http_util.h"
#include "net/call_backs.h"

#include <string>


namespace kit_domain {

struct HttpItemReqHeaderCfg
{
    kit_muduo::http::HttpRequest::Method method;
    std::string path;
    std::unordered_map<std::string, std::string> headers;

    HttpItemReqHeaderCfg() = default;
    HttpItemReqHeaderCfg(const nlohmann::json &req_json);
    HttpItemReqHeaderCfg(kit_muduo::HttpRequestPtr req_cfg);

    bool fromNetHttpReq(kit_muduo::HttpRequestPtr req_cfg);

    /**
     * @brief 通过json解析，允许部分更新
     * @param req_json 
     * @return true 
     * @return false 
     */
    bool fromJson(const nlohmann::json &req_json);
    
};

struct HttpItemRespHeaderCfg
{
    kit_muduo::http::Version version;
    kit_muduo::http::StateCode state_code;
    std::unordered_map<std::string, std::string> headers;

    HttpItemRespHeaderCfg() = default;
    HttpItemRespHeaderCfg(const nlohmann::json &j);
    HttpItemRespHeaderCfg(kit_muduo::HttpResponsePtr resp_cfg);

    bool fromNetHttpResp(kit_muduo::HttpResponsePtr resp_cfg);

    bool fromJson(const nlohmann::json &resp_json);
};

class HttpProtocolItem: public ProtocolItem
{
public:
    HttpProtocolItem() = default;

    ~HttpProtocolItem() override = default;

    bool init(std::shared_ptr<Protocol> ori_protocol) override;


    bool setReqCfg(const nlohmann::json& req_json) override;
    bool setRespCfg(const nlohmann::json& resp_json) override;


    bool setReqCfg(kit_muduo::HttpRequestPtr req_cfg);
    bool setRespCfg(kit_muduo::HttpResponsePtr resp_cfg);
    void setReqCfg(const HttpItemReqHeaderCfg &req_cfg);
    void setRespCfg(const HttpItemRespHeaderCfg &resp_cfg);

    HttpItemReqHeaderCfg getReqCfg() const { return req_cfg_; }

    HttpItemRespHeaderCfg getRespCfg() const { return resp_cfg_;}

private:
    HttpItemReqHeaderCfg req_cfg_;
    HttpItemRespHeaderCfg resp_cfg_;

};


}
#endif // __KIT_HTTP_PROTOCOL_ITEM_H__