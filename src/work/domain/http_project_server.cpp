/**
 * @file http_project_server.cpp
 * @brief HTTP协议测试服务器
 * @author ljk5
 * @version 1.0
 * @date 2025-10-20 17:49:01
 * @copyright Copyright (c) 2025 HIKRayin
 */

#include "base/thread.h"
#include "domain/runtime_result.h"
#include "net/call_backs.h"
#include "net/event_loop.h"
#include "net/http/http_util.h"
#include "net/inet_address.h"
#include "net/socket.h"
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
#include "service/svc_protocol.h"

#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <utility>

using namespace kit_muduo;
using namespace kit_muduo::http;
using nljson = nlohmann::json;



namespace {

inline static EventLoop* CheckLoop(EventLoop *loop)
{
    assert(loop);
    return loop;
}
    
}


namespace kit_domain {

HttpProjectServer::HttpProjectServer(int64_t project_id)
    :ProjectServer(project_id)
    ,http_server_(std::make_shared<http::HttpServer>(
        CheckLoop(loop_thread_.startLoop()), 
        InetAddress(0, "0.0.0.0"), 
        "pj" + std::to_string(project_id_) + "http", 
        false, 
        kit_muduo::TcpServer::KReusePort
    ))
{

}

void HttpProjectServer::start()
{
    http_server_->setThreadNum(0); // 使用单线程模式
    http_server_->start();
}

const kit_muduo::InetAddress& HttpProjectServer::getBindAddr() const 
{
    return http_server_->getBindAddr(); 
}


RuntimeResult<void> HttpProjectServer::AddProtocolItem(std::shared_ptr<ProtocolItem> item)
{
    RuntimeResult<void> result;

    auto http_item = std::dynamic_pointer_cast<HttpProtocolItem>(item);
    if(!http_item)
    {
        PJSERVER_F_ERROR("http protocol item is nullptr!");

        result.error.set(RuntimeError::kNullProtocolItem);
        return result;
    }

    // 1. 路由注册 
    auto req = http_item->getReq();

    auto route_result = http_server_->addRoute(req->method(), req->path(),  std::bind(&HttpProjectServer::HttpProjectProcess, this, item->getId(), std::placeholders::_1, std::placeholders::_2));

    if(!route_result.ok())
    {
        PJSERVER_F_ERROR("http protocol item add route error! %d:%s \n", route_result.status, route_result.message.c_str());

        result.error.set(RuntimeError::kRouteOperationError);
        return result;
    }


    item->setRegRouteId(route_result.route_id);
    // 2. 校验内容缓存
    {
        std::lock_guard<std::mutex> lock(mtx_);
        protocol_items_.emplace(item->getId(), item);
    }


    PJSERVER_DEBUG() << "HttpProjectServer::AddProtocolItem ok" << std::endl;

    return result;
}


RuntimeResult<void> HttpProjectServer::DelProtocolItem(int64_t protocol_id) 
{
    RuntimeResult<void> result;
  
    std::lock_guard<std::mutex> lock(mtx_);

    auto it = protocol_items_.find(protocol_id);
    if(it == protocol_items_.end())
    {
        PJSERVER_F_ERROR("protocol_id[%d] not found! \n", protocol_id);

        result.error.set(RuntimeError::kProtocolItemNotFound);
        return result;
    }
    auto http_item = std::dynamic_pointer_cast<HttpProtocolItem>(it->second);
    if(!http_item)
    {
        PJSERVER_F_ERROR("http protocol item is nullptr! protocol_id[%d] \n", protocol_id);
        
        result.error.set(RuntimeError::kRouteOperationError);
        return result;
    }

    // 1. 删路由
    if(!http_server_->removeRoute(http_item->getRegRouteId()))
    {
        result.error.set(RuntimeError::kRouteOperationError);
        return result;
    }

    // 2. 删缓存
    protocol_items_.erase(it);

    return result;
}

RuntimeResult<std::shared_ptr<ProtocolItem>> HttpProjectServer::GetProtocolItem(int64_t protocol_id)
{
    RuntimeResult<std::shared_ptr<ProtocolItem>> result;

    std::lock_guard<std::mutex> lock(mtx_);
    auto it = protocol_items_.find(protocol_id);
    if(it == protocol_items_.end())
    {
        result.error.set(RuntimeError::kProtocolItemNotFound);
    }
    else
    {
        result.val = it->second;
    }

    return result;
}

RuntimeResult<void> HttpProjectServer::UpdateReqCfgProtocolItem(int64_t protocol_id, const nljson& req_cfg_json)
{
    RuntimeResult<void> result;

    /* 注意: 只有涉及协议头修改才需要重新配置路由
     *
     * 更新原则：
     *   1. 先用临时对象验证新配置，不污染现有运行态；
     *   2. 只有验证通过后，才删除旧 route；
     *   3. 新 route 注册成功后，再提交到当前 HttpProtocolItem；
     *   4. 任一步失败，旧 route / 旧配置都保留。
     */
    std::unique_lock<std::mutex> lock(mtx_);
    
    auto it = protocol_items_.find(protocol_id);
    if(it == protocol_items_.end())
    {
        PJSERVER_F_ERROR("protocol_id[%d] not found! \n", protocol_id);
        result.error.set(RuntimeError::kProtocolItemNotFound);
        return result;
    }
    auto http_item = std::dynamic_pointer_cast<HttpProtocolItem>(it->second);
    if(!http_item)
    {
        PJSERVER_F_ERROR("http protocol item is nullptr! protocol_id[%d] \n", protocol_id);
        
        result.error.set(RuntimeError::kRouteOperationError);
        return result;
    }

    auto old_req_cfg = std::make_shared<HttpRequest>(*http_item->getReq());
    auto old_resp_cfg = std::make_shared<HttpResponse>(*http_item->getResp());
    auto old_route_id = http_item->getRegRouteId();

    if(!old_req_cfg || !old_resp_cfg)
    {
        result.error.set(RuntimeError::kNullProtocolItem);
        return result;
    }

    // 先用临时协议项验证新配置，避免把当前运行态改半截
    auto new_req_cfg = std::make_shared<HttpRequest>(*old_req_cfg);
    auto new_resp_cfg = std::make_shared<HttpResponse>(*old_resp_cfg);
    HttpProtocolItem shadow_item(new_req_cfg, new_resp_cfg);
    if(!shadow_item.setReqCfg(req_cfg_json))
    {
        result.error.set(RuntimeError::kInvalidProtocolConfig);
        return result;
    }

    const bool route_changed =
        old_req_cfg->method().toInt() != new_req_cfg->method().toInt()
        || old_req_cfg->path() != new_req_cfg->path();

    // 只改 headers / body 之类不影响路由的字段，直接提交新配置即可
    if(!route_changed)
    {
        http_item->setReqCfg(new_req_cfg);
        http_item->setRegRouteId(old_route_id);
        return result;
    }

    // 先删旧路由
    if(!http_server_->removeRoute(old_route_id))
    {
        PJSERVER_F_ERROR("removeRoute error! route_id[%d] \n", static_cast<int32_t>(old_route_id));

        result.error.set(RuntimeError::kRouteOperationError);
        return result;
    }

    // 再注册新路由。这里不能直接调用 AddProtocolItem，
    // 因为它会重复插入 protocol_items_，只需要补回路由即可。
    auto route_result = http_server_->addRoute(
        new_req_cfg->method(),
        new_req_cfg->path(),
        std::bind(&HttpProjectServer::HttpProjectProcess, this, protocol_id, std::placeholders::_1, std::placeholders::_2));

    if(!route_result.ok())
    {
        PJSERVER_F_ERROR("re-add route error! protocol_id[%d], path[%s], method[%s], reason[%d:%s]\n",
            protocol_id,
            new_req_cfg->path().c_str(),
            new_req_cfg->method().toString(),
            static_cast<int32_t>(route_result.status),
            route_result.message.c_str());

        // 回滚旧路由，保证失败后仍可继续按旧配置工作
        auto rollback_result = http_server_->addRoute(
            old_req_cfg->method(),
            old_req_cfg->path(),
            std::bind(&HttpProjectServer::HttpProjectProcess, this, protocol_id, std::placeholders::_1, std::placeholders::_2));
        if(!rollback_result.ok())
        {
            PJSERVER_F_ERROR("rollback add route failed! protocol_id[%d], old_path[%s], method[%s], reason[%d:%s]\n",
                protocol_id,
                old_req_cfg->path().c_str(),
                old_req_cfg->method().toString(),
                static_cast<int32_t>(rollback_result.status),
                rollback_result.message.c_str());
        }

        result.error.set(RuntimeError::kRouteOperationError);
        return result;
    }

    // 路由已切换成功，最后提交运行态对象
    http_item->setReqCfg(new_req_cfg);
    http_item->setRegRouteId(route_result.route_id);

    return result;
}


RuntimeResult<void> HttpProjectServer::UpdateRespCfgProtocolItem(int64_t protocol_id, const nljson& resp_cfg_json)
{
    RuntimeResult<void> result;

    std::lock_guard<std::mutex> lock(mtx_);
    
    auto it = protocol_items_.find(protocol_id);
    if(it == protocol_items_.end())
    {
        PJSERVER_F_ERROR("protocol_id[%d] not found! \n", protocol_id);
        result.error.set(RuntimeError::kProtocolItemNotFound);
        return result;
    }
    if(!it->second)
    {
        result.error.set(RuntimeError::kNullProtocolItem);
        return result;
    }

    // 更新校验缓存
    if(!it->second->setRespCfg(resp_cfg_json))
    {
        result.error.set(RuntimeError::kInvalidProtocolConfig);
        return result;
    }

    return result;
}

RuntimeResult<void> HttpProjectServer::UpdateReqBodyProtocolItem(int64_t protocol_id, const ProtocolBodyType body_type, const std::vector<char> &body_data)
{
    RuntimeResult<void> result;

    std::lock_guard<std::mutex> lock(mtx_);
    auto it = protocol_items_.find(protocol_id);
    if(it == protocol_items_.end())
    {
        PJSERVER_F_ERROR("protocol_id[%d] not found! \n", protocol_id);

        result.error.set(RuntimeError::kProtocolItemNotFound);
        return result;
    }
    if(!it->second)
    {
        result.error.set(RuntimeError::kNullProtocolItem);
        return result;
    }

    it->second->setReqBody(body_type, body_data);

    return result;
}

RuntimeResult<void> HttpProjectServer::UpdateRespBodyProtocolItem(int64_t protocol_id, const ProtocolBodyType body_type,const std::vector<char> &body_data)
{
    RuntimeResult<void> result;

    std::lock_guard<std::mutex> lock(mtx_);
    auto it = protocol_items_.find(protocol_id);
    if(it == protocol_items_.end())
    {
        PJSERVER_F_ERROR("protocol_id[%d] not found! \n", protocol_id);

        result.error.set(RuntimeError::kProtocolItemNotFound);
        return result;
    }
    if(!it->second)
    {
        result.error.set(RuntimeError::kNullProtocolItem);
        return result;
    }

    it->second->setRespBody(body_type, body_data);

    return result;
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

void HttpProjectServer::HttpProjectProcess(int32_t protocol_id, TcpConnectionPtr conn, HttpContextPtr ctx)
{
    auto req = ctx->request();
    auto resp = ctx->response();
    resp->setVersion(Version::kHttp11);
    resp->setStateCode(StateCode::k200Ok);

    // 可以无锁 因为所有协议项增删改查都是成队列形式
    auto it = protocol_items_.find(protocol_id);
    if(it == protocol_items_.end())
    {
        PJSERVER_F_ERROR("url[%s] protocol item is nullptr! \n", req->path().c_str());
        
        resp->setStateCode(StateCode::k404NotFound);
        return;
    }
  
    auto http_item = std::dynamic_pointer_cast<HttpProtocolItem>(it->second);

    auto req_cfg = http_item->getReq();
    auto resp_cfg = http_item->getResp();


    // 1.TODO 建立websocket 进行协议收发实时推送
    // 注意这里 http 返回的实际响应报文 和 websocket 可能是不同的内容
    // 2.TODO 校验接口统一  怎么进行统一??
    // RuntimeResult<void> ok = 校验接口(接收到的实际请求数据, 配置的期望请求数据);
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
    *resp = *resp_cfg;

    PJSERVER_DEBUG() << std::endl << resp->toString() << std::endl;
    return;
}


}
