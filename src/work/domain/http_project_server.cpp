/**
 * @file http_project_server.cpp
 * @brief HTTP协议测试服务器
 * @author ljk5
 * @version 1.0
 * @date 2025-10-20 17:49:01
 * @copyright Copyright (c) 2025 HIKRayin
 */

#include "web/web_log.h"
#include "domain/project_server.h"
#include "net/http/http_server.h"
#include "net/http/http_context.h"
#include "net/http/http_request.h"
#include "net/http/http_response.h"
#include "domain/protocol.h"
#include "domain/protocol_item.h"
#include "nlohmann/json.hpp"
#include "domain/type.h"

#include <functional>
#include <iostream>
#include <memory>

using namespace kit_muduo;
using namespace kit_muduo::http;
using nljson = nlohmann::json;



namespace kit_domain {

HttpProjectServer::HttpProjectServer(int64_t project_id, kit_muduo::HttpServerPtr http_server)
    :ProjectServer(project_id)
    ,_http_server(http_server)
{
 
}

kit_muduo::EventLoop *HttpProjectServer::getLoop() const 
{
    return _http_server->getLoop();
}

void HttpProjectServer::AddProtocolItem(std::shared_ptr<ProtocolItem> item)
{
    auto http_item = std::dynamic_pointer_cast<HttpProtocolItem>(item);
    if(!http_item)
    {
        PJ_F_ERROR("http protocol item is nullptr!");
        return;
    }

    // 1. 校验内容缓存
    {
        std::lock_guard<std::mutex> lock(_mtx);
        _protocol_items.emplace(item->getId(), item);
    }

    // 2. 路由注册  TODO 这里似乎没有去重,重新考虑一下怎么设计
    auto req = http_item->getReq();

    _http_server->addRoute(req->method(), req->path(),  std::bind(&HttpProjectServer::HttpProjectProcess, this, std::placeholders::_1, std::placeholders::_2));

    PJSERVER_DEBUG() << "HttpProjectServer::AddProtocolItem ok" << std::endl;

    return;
}


void HttpProjectServer::DelProtocolItem(int64_t protocol_id) 
{
    std::shared_ptr<kit_domain::ProtocolItem> item = nullptr;
    // 先删缓存
    {
        std::lock_guard<std::mutex> lock(_mtx);

        auto it = _protocol_items.find(protocol_id);
        if(it == _protocol_items.end())
        {
            PJSERVER_F_ERROR("protocol_id[%d] not found! \n", protocol_id);
            return;
        }
        item = it->second;
        _protocol_items.erase(it);
    }
    // 再删路由
    auto http_item = std::dynamic_pointer_cast<HttpProtocolItem>(item);
    if(!http_item)
    {
        PJSERVER_F_ERROR("http protocol item is nullptr! protocol_id[%d] \n", protocol_id);
        return;
    }
    
    auto req = http_item->getReq();
    _http_server->delRoute(req->path(), req->method());

    return;
}

std::shared_ptr<ProtocolItem> HttpProjectServer::GetProtocolItem(int64_t protocol_id)
{
    std::lock_guard<std::mutex> lock(_mtx);
    auto it = _protocol_items.find(protocol_id);
    return it == _protocol_items.end() ? nullptr : it->second;
}

void HttpProjectServer::UpdateReqCfgProtocolItem(int64_t protocol_id, const nljson& req_cfg_json)
{

    /*注意: 只有涉及协议头修改才需要重新配置路由 */
    std::unique_lock<std::mutex> lock(_mtx);
    
    auto it = _protocol_items.find(protocol_id);
    if(it == _protocol_items.end())
        return;

    auto http_item = std::dynamic_pointer_cast<HttpProtocolItem>(it->second);
    assert(http_item);

    // 获取原来的路径和方法
    const std::string& old_path = http_item->getReq()->path();
    const auto& old_method = http_item->getReq()->method();

    auto p = it->second->getOriProtocol();

    for(auto &item : req_cfg_json.items())
    {
        auto json_it = p->m_reqCfg.find(item.key());
        if(json_it != p->m_reqCfg.end())
        {
            p->m_reqCfg[item.key()] = item.value();
        }
        else
        {
            PJSERVER_F_ERROR("req cfg json not find! id[%ld], key[%s] \n", protocol_id, item.key().c_str());
        }
    }

    _protocol_items.erase(it);
    
    lock.unlock();

    // 删除路由
    _http_server->delRoute(old_path, old_method);

    // 重新添加路由
    AddProtocolItem(std::make_shared<HttpProtocolItem>(p));

    return;
}


void HttpProjectServer::UpdateRespCfgProtocolItem(int64_t protocol_id, const nljson& resp_cfg_json)
{
    std::lock_guard<std::mutex> lock(_mtx);
    
    auto it = _protocol_items.find(protocol_id);
    if(it == _protocol_items.end())
    {
        PJSERVER_F_ERROR("protocol_id[%d] not found! \n", protocol_id);
        return;
    }
    auto p = it->second->getOriProtocol();

    for(auto &item : resp_cfg_json.items())
    {
        auto json_it = p->m_respCfg.find(item.key());
        if(json_it != p->m_respCfg.end())
        {
            p->m_respCfg[item.key()] = item.value();
        }
        else
        {
            PJSERVER_F_ERROR("resp cfg json not find! id[%ld], key[%s] \n", protocol_id, item.key().c_str());
        }
    }

}

void HttpProjectServer::UpdateReqBodyProtocolItem(int64_t protocol_id, const ProtocolBodyType body_type, const std::vector<char> &req_body_data)
{
    std::lock_guard<std::mutex> lock(_mtx);
    auto it = _protocol_items.find(protocol_id);
    if(it == _protocol_items.end())
    {
        PJSERVER_F_ERROR("protocol_id[%d] not found! \n", protocol_id);
        return;
    }
    auto p = it->second->getOriProtocol();
    p->m_reqBodyType = body_type;
    p->m_reqBodyData = std::move(req_body_data);
    
    _protocol_items[protocol_id] = std::make_shared<HttpProtocolItem>(p);
}

void HttpProjectServer::UpdateRespBodyProtocolItem(int64_t protocol_id, const ProtocolBodyType body_type,const std::vector<char> &resp_body_data)
{
    std::lock_guard<std::mutex> lock(_mtx);
    auto it = _protocol_items.find(protocol_id);
    if(it == _protocol_items.end())
    {
        PJSERVER_F_ERROR("protocol_id[%d] not found! \n", protocol_id);
        return;
    }
    auto p = it->second->getOriProtocol();
    p->m_respBodyType = body_type;
    p->m_respBodyData = std::move(resp_body_data);
    
    _protocol_items[protocol_id] = std::make_shared<HttpProtocolItem>(p);
}

/**
 * @brief json数据比对辅助(只比对key值是否正确)
 * @param root 
 * @param cfg_root 
 * @return true 
 * @return false 
 */
static bool JsonDataVerifyHelper(const nljson& root, const nljson& cfg_root)
{
    if(root.is_primitive() && cfg_root.is_primitive())
    {
        if(root.type() != cfg_root.type())
        {
            PJSERVER_F_ERROR("body filed type is not match! [%s] -- [%s]\n", root.type_name(), cfg_root.type_name());
            return false;
        }
        return true;
    }

    // if(!root.is_object() || !cfg_root.is_object())
    // {
    //     return false;
    // }

    if(root.size() != cfg_root.size())
    {
        PJSERVER_F_ERROR("filed size is not match! [%d] -- [%d]\n", root.size(), cfg_root.size());
        return false;
    }

    auto cfg_it = cfg_root.begin();
    for(const auto &it : root.items())
    {
        // 字段名称校验 可能没有名称
        if(it.key().size() && cfg_it.key().size())
        {
            cfg_it = cfg_root.find(it.key());
            if(cfg_it == cfg_root.end())
            {
                PJSERVER_F_ERROR("body filed key is not match! [%s][%s] -- [%s][%s]\n", it.key().c_str(), it.value().type_name(),
                cfg_it.key().c_str(), cfg_it.value().type_name());

                return false;
            }
        }
        else if(it.key().empty() && cfg_it.key().empty())
        {
            PJSERVER_F_DEBUG("body filed key is empty!\n");
        }
        else
        {
            PJSERVER_F_INFO("body filed key is not match! [%s][%s] -- [%s][%s]\n", it.key().c_str(), it.value().type_name(),
            cfg_it.key().c_str(), cfg_it.value().type_name());

            return false;
        }

        // 字段类型校验
        if(it.value().type() != cfg_it.value().type())
        {
            PJSERVER_F_ERROR("body filed type is not match! [%s][%s] -- [%s][%s]\n", it.key().c_str(), it.value().type_name(),
            cfg_it.key().c_str(), cfg_it.value().type_name());

            return false;
        }

        PJSERVER_F_DEBUG("body filed: [%s][%s] -- [%s][%s]\n", 
            it.key().c_str(), it.value().type_name(),
            cfg_it.key().c_str(), cfg_it.value().type_name());
        
        if(it.value().type() == nljson::value_t::object)
        {
            if(!JsonDataVerifyHelper(it.value(), cfg_it.value()))
            {
                return false;
            }
        }
        else if(it.value().type() == nljson::value_t::array)
        {
            if(it.value().size() != cfg_it.value().size())
            {
            
                PJSERVER_F_ERROR("array size is not match! [%d] -- [%d]\n", it.value().size(), cfg_it.value().size());
                return false;
            }
            auto cfg_arr_item = cfg_it.value().begin();
            for(const auto &arr_item : it.value())
            {
                if(!JsonDataVerifyHelper(arr_item, *cfg_arr_item))
                {
                    return false;
                }
                cfg_arr_item++;
            }
        }


    }

    return true;
}

void HttpProjectServer::HttpProjectProcess(TcpConnectionPtr conn, HttpContextPtr ctx)
{
    auto req = ctx->request();
    auto resp = ctx->response();
    resp->setVersion(Version::kHttp11);
    resp->setStateCode(StateCode::k200Ok);

    // TODO 哈希算法? 采用mumurHash3
    // 输入键值格式: 
        // HTTP: GET-/api/test1
        // TCP: 定长(001)/变长(002)-头部长度
    // 这里匹配存疑？如何快速索引到要校验对象？

    // 实际请求的信息:
    //

    auto it = std::find_if(_protocol_items.begin(), _protocol_items.end(), [req](auto &it){
        auto http_item = std::dynamic_pointer_cast<HttpProtocolItem>(it.second);
        if(http_item)
        {
            PJSERVER_F_DEBUG("[%d][%s][%d] path: %s -- %s\n", 
                http_item->getId(),
                http_item->getName().c_str(),
                http_item->getProjectId(),
                http_item->getReqPath().c_str(),
                req->path().c_str());
            return http_item->getReqPath() == req->path();
        }

        return false;
    });

    if(it == _protocol_items.end())
    {
        PJSERVER_F_ERROR("url[%s] protocol item is nullptr! \n", req->path().c_str());
        
        resp->body().appendData(R"({"code": -300, "message":"inner error"})");
        
        return;
    }

    auto http_item = std::dynamic_pointer_cast<HttpProtocolItem>(it->second);

    auto req_cfg = http_item->getReq();
    auto resp_cfg = http_item->getResp();

    // 1.TODO 建立websocket 进行协议收发实时推送
    // 注意这里 http 返回的实际响应报文 和 websocket 可能是不同的内容
    // 2.TODO 校验接口统一  怎么进行统一??
    // bool ok = 校验接口(接收到的实际请求数据, 配置的期望请求数据);
    /*
        2.1 分出请求类型
        2.2 实际数据转换  期望数据转换
        2.3 分出响应类型
        2.2 实际数据转换  期望数据转换
    */
    try {
        if(!req_cfg->body().data().empty())
        {
            if(!req->body().data().empty()
                && req->body().contentType() == req_cfg->body().contentType())
            {
                // TODO 这里需要大量的模版方法模式
                if(ContentType::kJsonType == req->body().contentType()())
                {
                    PJSERVER_F_DEBUG("protocol body type is json! \n");
                    auto req_root = std::make_unique<nljson>(nljson::parse(req->body().data()));
                    PJSERVER_DEBUG() << "req body: \n" << req_root->dump(4) << std::endl;
                    auto req_cfg_root = std::make_unique<nljson>(nljson::parse(req_cfg->body().data()));
                    PJSERVER_DEBUG() << "req_cfg body: \n" << req_cfg_root->dump(4) << std::endl;

                    /************核心校验过程 DFS递归校验***********/
                    PJSERVER_F_DEBUG("[%d][%s][%d] start match!\n", 
                        http_item->getId(),
                        http_item->getName().c_str(),
                        http_item->getProjectId());
                        
                    if(!JsonDataVerifyHelper(*req_root, *req_cfg_root))
                    {
                        throw std::logic_error("protocol body is not match!");
                    }
    
                }
                else  
                {
                    // TODO 其他类型转换适配
                }
            }
            else
            {
                PJSERVER_F_ERROR("protocol body type is not match! real[%s] - expect[%s] \n", req->body().contentType().toString(), req_cfg->body().contentType().toString());

                resp->body().appendData(R"({"code": -200, "message":"body type is not match!"})");
                
                //TODO： websocket埋点
                return;
            }
        }
        else
        {
            // 请求Body没配情况下 默认怎么处理?
            PJSERVER_F_WARN("protocol body is empty! [%d][%s][%d], url[%s], cfg_body_type[%s], cfg_body:%s \n",
                http_item->getId(),
                http_item->getName().c_str(),
                http_item->getProjectId(),
                req->path().c_str(), 
                req_cfg->body().contentType().toString(),
                req_cfg->body().toString().c_str());
        }
    } catch (const std::exception& e) {

        PJSERVER_F_ERROR("protocol body parse exception! %s \n", e.what());

        resp->body().appendData(R"({"code": -200, "message":"body parse error!"})");
        //TODO： websocket埋点

        return;
    }

    PJSERVER_F_INFO("[%d][%s][%d] match success!\n", 
        http_item->getId(),
        http_item->getName().c_str(),
        http_item->getProjectId());

    // 响应直接拷贝
    // TODO 这里是否可以不拷贝?? 
    *resp = *resp_cfg;
    if(resp->body().data().empty())
    {
        resp->body().appendData(R"({"code": 0, "message": "success"})");
    }

    PJSERVER_DEBUG() << std::endl << resp->toString() << std::endl;
    return;
}


}