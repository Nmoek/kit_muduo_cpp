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
#include "domain/http_protocol_item.h"
#include "nlohmann/json.hpp"
#include "domain/type.h"
#include "service/svc_protocol.h"

#include <cstdint>
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
    const auto& req_cfg = http_item->getReqCfg();

    auto route_result = http_server_->addRoute(req_cfg.method, req_cfg.path,  std::bind(&HttpProjectServer::HttpProjectProcess, this, item->getId(), std::placeholders::_1, std::placeholders::_2));

    if(!route_result.ok())
    {
        PJSERVER_F_ERROR("http protocol item add route error! %d:%s \n", route_result.status, route_result.message.c_str());

        result.error.set(RuntimeError::kRouteConflict);
        return result;
    }


    // 2. 校验内容缓存
    {
        std::lock_guard<std::mutex> lock(mtx_);
        http_items_.emplace(item->getId(), HttpRuntimeItem{http_item, route_result.route_id});
    }


    PJSERVER_DEBUG() << "HttpProjectServer::AddProtocolItem ok" << std::endl;

    return result;
}


RuntimeResult<void> HttpProjectServer::DelProtocolItem(int64_t protocol_id) 
{
    RuntimeResult<void> result;
  
    std::lock_guard<std::mutex> lock(mtx_);

    auto it = http_items_.find(protocol_id);
    if(it == http_items_.end())
    {
        PJSERVER_F_ERROR("HttpProjectServer: Protocol item not found, pjId[%d], pcId[%d] \n",  project_id_, protocol_id);

        result.error.set(RuntimeError::kProtocolItemNotFound);
        return result;
    }
    uint64_t route_id = it->second.route_id;

    if(!it->second.item)
    {
        PJSERVER_F_ERROR("http protocol item is nullptr! protocol_id[%d] \n", protocol_id);
        
        result.error.set(RuntimeError::kNullProtocolItem);
        return result;
    }

    // 1. 删路由
    if(!http_server_->removeRoute(route_id))
    {
        result.error.set(RuntimeError::kRouteNotFound);
        return result;
    }

    // 2. 删缓存
    http_items_.erase(it);

    return result;
}

RuntimeResult<std::shared_ptr<ProtocolItem>> HttpProjectServer::GetProtocolItem(int64_t protocol_id)
{
    RuntimeResult<std::shared_ptr<ProtocolItem>> result;

    std::lock_guard<std::mutex> lock(mtx_);
    auto it = http_items_.find(protocol_id);
    if(it == http_items_.end())
    {
        result.error.set(RuntimeError::kProtocolItemNotFound);
    }
    else
    {
        result.val = it->second.item;
    }

    return result;
}

RuntimeResult<void> HttpProjectServer::UpdateReqCfgProtocolItem(int64_t protocol_id, const nljson& req_cfg_json)
{
    RuntimeResult<void> result;
    HttpItemReqHeaderCfg new_req_cfg;

    if(!new_req_cfg.fromJson(req_cfg_json))
    {
        PJSERVER_F_ERROR("http req cfg json parse error! %s \n", req_cfg_json.dump().c_str());

        result.error.set(RuntimeError::kInvalidProtocolConfig);
        return result;
    }
    /* 注意: 只有涉及协议头修改才需要重新配置路由
     *
     * 更新原则：
     *   1. 先用临时对象验证新配置，不污染现有运行态；
     *   2. 只有验证通过后，才删除旧 route；
     *   3. 新 route 注册成功后，再提交到当前 HttpProtocolItem；
     *   4. 任一步失败，旧 route / 旧配置都保留。
     */
    std::unique_lock<std::mutex> lock(mtx_);
    
    auto it = http_items_.find(protocol_id);
    if(it == http_items_.end())
    {
        PJSERVER_F_ERROR("protocol_id[%d] not found! \n", protocol_id);
        result.error.set(RuntimeError::kProtocolItemNotFound);
        return result;
    }

    if(!it->second.item)
    {
        PJSERVER_F_ERROR("http protocol item is nullptr! protocol_id[%d] \n", protocol_id);
        
        result.error.set(RuntimeError::kNullProtocolItem);
        return result;
    }

    return ReplaceReqCfgProtocolItem(it->second, new_req_cfg);
}


RuntimeResult<void> HttpProjectServer::UpdateRespCfgProtocolItem(int64_t protocol_id, const nljson& resp_cfg_json)
{
    RuntimeResult<void> result;
    HttpItemRespHeaderCfg new_resp_cfg;

    if(!new_resp_cfg.fromJson(resp_cfg_json))
    {
        CUSTOM_F_ERROR("resp json parse error!\n");
        result.error.set(RuntimeError::kInvalidProtocolConfig);
        return result;
    }

    std::lock_guard<std::mutex> lock(mtx_);
    
    auto it = http_items_.find(protocol_id);
    if(it == http_items_.end())
    {
        PJSERVER_F_ERROR("protocol_id[%d] not found! \n", protocol_id);
        result.error.set(RuntimeError::kProtocolItemNotFound);
        return result;
    }
    if(!it->second.item)
    {
        result.error.set(RuntimeError::kNullProtocolItem);
        return result;
    }

    // 更新校验缓存
    it->second.item->setRespCfg(new_resp_cfg);

    return result;
}

RuntimeResult<void> HttpProjectServer::UpdateReqBodyProtocolItem(int64_t protocol_id, const ProtocolBodyType body_type, const std::vector<char> &body_data)
{
    RuntimeResult<void> result;

    std::lock_guard<std::mutex> lock(mtx_);
    auto it = http_items_.find(protocol_id);
    if(it == http_items_.end())
    {
        PJSERVER_F_ERROR("protocol_id[%d] not found! \n", protocol_id);

        result.error.set(RuntimeError::kProtocolItemNotFound);
        return result;
    }
    if(!it->second.item)
    {
        result.error.set(RuntimeError::kNullProtocolItem);
        return result;
    }

    it->second.item->setReqBody(body_type, body_data);

    return result;
}

RuntimeResult<void> HttpProjectServer::UpdateRespBodyProtocolItem(int64_t protocol_id, const ProtocolBodyType body_type,const std::vector<char> &body_data)
{
    RuntimeResult<void> result;

    std::lock_guard<std::mutex> lock(mtx_);
    auto it = http_items_.find(protocol_id);
    if(it == http_items_.end())
    {
        PJSERVER_F_ERROR("protocol_id[%d] not found! \n", protocol_id);

        result.error.set(RuntimeError::kProtocolItemNotFound);
        return result;
    }
    if(!it->second.item)
    {
        result.error.set(RuntimeError::kNullProtocolItem);
        return result;
    }

    it->second.item->setRespBody(body_type, body_data);

    return result;
}

RuntimeResult<void> HttpProjectServer::ReplaceReqCfgProtocolItem(const HttpRuntimeItem& http_run_item, const HttpItemReqHeaderCfg &new_req_cfg)
{
    RuntimeResult<void> result;
    auto http_item = http_run_item.item;
    const auto& old_req_cfg = http_item->getReqCfg();
    uint64_t old_route_id = http_run_item.route_id;
    
    // 只改 headers / body 之类不影响路由的字段，直接提交新配置即可
    if(isSameRoute(old_req_cfg, new_req_cfg))
    {
        http_item->setReqCfg(new_req_cfg);
        return result;
    }

    // 路由Key{method, path}发生变化则需要更新路由

    // 先添加新路由
    auto route_result = http_server_->addRoute(new_req_cfg.method, new_req_cfg.path, std::bind(&HttpProjectServer::HttpProjectProcess, this, http_item->getId(), std::placeholders::_1, std::placeholders::_2));
    if(!route_result.ok())
    {
        PJSERVER_F_ERROR("add route failed! pjId[%d], pcId[%d], path[%s], method[%s] \n", project_id_,http_item->getId(), new_req_cfg.path.c_str(), new_req_cfg.method.toStr());

        result.error.set(RuntimeError::kRouteConflict);
        return result;
    }

    // 先删旧路由
    if(!http_server_->removeRoute(old_route_id))
    {

        // 回滚新增加的路由
        if(!http_server_->removeRoute(route_result.route_id))
        {
            PJSERVER_F_ERROR("remove new route error! route_id[%lu], path[%s] \n", route_result.route_id, new_req_cfg.path.c_str());
        }

        PJSERVER_F_ERROR("remove old route error! route_id[%lu], path[%s] \n", old_route_id, old_req_cfg.path.c_str());

        result.error.set(RuntimeError::kRouteNotFound);
        return result;
    }

    // 路由已切换成功，最后提交运行态对象
    http_item->setReqCfg(new_req_cfg);
    http_items_[http_item->getId()] = {http_item, route_result.route_id};

    return result;
}


bool HttpProjectServer::isSameRoute(const HttpItemReqHeaderCfg &old_cfg, const HttpItemReqHeaderCfg &new_cfg)
{
    return old_cfg.method.toInt() == new_cfg.method.toInt() 
    && old_cfg.path == new_cfg.path;
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

    // 可以无锁 因为所有协议项增删改查都是成队列形式
    auto it = http_items_.find(protocol_id);
    
    if(it == http_items_.end() || !it->second.item)
    {
        PJSERVER_F_ERROR("url[%s] protocol item is nullptr! \n", req->path().c_str());
        
        resp->setStateCode(StateCode::k404NotFound);
        return;
    }
    auto http_item = it->second.item;

  
    auto req_cfg = http_item->getReqCfg();
    auto resp_cfg = http_item->getRespCfg();
    const auto& req_body_view = http_item->getReqBodyView();
    const auto& resp_body_view = http_item->getRespBodyView();
    

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
        if(!req_body_view.body_data->empty())
        {
            if(!req->body().data().empty()
                && req->body().contentType() == req_body_view.body_type)
            {
                // TODO 这里需要大量的模版方法模式
                if(ContentType::kJsonType == req->body().contentType()())
                {
                    PJSERVER_F_DEBUG("protocol body type is json! \n");
                    auto req_root = std::make_unique<nljson>(nljson::parse(req->body().data()));
                    PJSERVER_DEBUG() << "req body: \n" << req_root->dump(4) << std::endl;
                    auto req_cfg_root = std::make_unique<nljson>(nljson::parse(*req_body_view.body_data));
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
                PJSERVER_F_ERROR("protocol body type is not match! real[%s] - expect[%s] \n", req->body().contentType().toStr(), req_body_view.body_type.toStr());

                resp->body().appendData(R"({"code": -200, "message":"body type is not match!"})");
                
                //TODO： websocket埋点
                return;
            }
        }
        else
        {
            // 请求Body没配情况下 默认怎么处理?
            PJSERVER_F_WARN("protocol body is empty! [%d][%s][%d], url[%s], cfg_body_type[%s] \n",
                http_item->getId(),
                http_item->getName().c_str(),
                http_item->getProjectId(),
                req->path().c_str(), 
                req_body_view.body_type.toStr());
        }
    } catch (const std::exception& e) {

        PJSERVER_F_ERROR("protocol body parse exception! %s \n", e.what());

        resp->body().appendData(R"({"code": -200, "message":"body parse error!"})");
        //TODO： websocket埋点

        return;
    }

    PJSERVER_F_INFO("pjId[%d] pcId[%d] name[%s] match success!\n", 
        http_item->getProjectId(),
        http_item->getId(),
        http_item->getName().c_str()
    );

    // 响应数据拷贝
    resp->setStateCode(resp_cfg.state_code);
    resp->setHeaders(resp_cfg.headers);
    resp->setBody(Body{resp_body_view.body_type, *resp_body_view.body_data});

    PJSERVER_DEBUG() << std::endl << resp->toString() << std::endl;
    return;
}


}
