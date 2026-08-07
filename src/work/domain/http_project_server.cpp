/**
 * @file http_project_server.cpp
 * @brief HTTP协议测试服务器
 * @author ljk5
 * @version 1.0
 * @date 2025-10-20 17:49:01
 * @copyright Copyright (c) 2025 HIKRayin
 */

#include "base/time_stamp.h"
#include "domain/protocol_interaction_observation.h"
#include "domain/runtime_result.h"
#include "net/call_backs.h"
#include "net/event_loop.h"
#include "net/http/http_servlet.h"
#include "net/tcp_server.h"
#include "net/http/http_content.h"
#include "net/http/http_util.h"
#include "net/inet_address.h"
#include "net/socket.h"
#include "domain/domain_log.h"
#include "domain/http_project_server.h"
#include "net/http/http_context.h"
#include "net/http/http_request.h"
#include "net/http/http_response.h"
#include "domain/protocol_item.h"
#include "domain/http_protocol_item.h"
#include "net/tcp_server.h"
#include "nlohmann/json.hpp"
#include "domain/type.h"
#include "domain/runtime_loop_pool.h"
#include "domain/protocol_interaction.h"
#include "domain/protocol_interaction_hub.h"

#include <algorithm>
#include <cstdint>
#include <functional>
#include <iostream>
#include <iterator>
#include <memory>
#include <mutex>
#include <string_view>
#include <stdexcept>
#include <utility>
#include <vector>

using namespace kit_muduo;
using namespace kit_muduo::http;
using nljson = nlohmann::json;


namespace kit_domain {

namespace {



bool IsContentTypeMatch(const ContentMeta& actual_meta, const ContentMeta& expected_meta)
{
    if(expected_meta.media_type.empty())
    {
        return false;
    }

    return actual_meta.media_type == expected_meta.media_type;
}

void AttachHttpRequestCaptureFromContext(
    ProtocolInteractionObservation &obs,
    const HttpRequestPtr &req,
    ProtocolBodyType expect_body_type)
{
    obs.request.meta = {
        {"method", req->method().toString()},
        {"url", req->url()},
        {"path", req->path()},
        {"version", req->version().toString()}
    };

    obs.request.head_text = req->toHeaderString();
    obs.request.body_bytes = req->bodyData();
    obs.request.expect_body_type = expect_body_type;
    obs.request.content_meta = req->contentMeta();
    obs.request.prefer_hex_text_for_binary = ProtocolBodyType::kBinary == expect_body_type;

}

void AttachHttpResponseCaptureFromContext(
    ProtocolInteractionObservation &obs,
    const HttpResponsePtr &resp,
    ProtocolBodyType expect_body_type)
{
    obs.response.meta = {
        {"version", resp->version().toString()},
        {"status_code", resp->stateCode().toInt()},
    };

    obs.response.head_text = resp->toHeaderString();
    obs.response.body_bytes = resp->bodyData();
    obs.response.expect_body_type = expect_body_type;
    obs.response.content_meta = resp->contentMeta();
    obs.response.prefer_hex_text_for_binary = ProtocolBodyType::kBinary == expect_body_type;
}

}


HttpProjectServer::HttpProjectServer(int64_t project_id, std::shared_ptr<RuntimeLease> lease_loop, const kit_muduo::InetAddress &addr)
    :ProjectServer(
        project_id,
        lease_loop,
        addr,
        "pj" + std::to_string(project_id) + "http")
    ,dispatch_(std::make_shared<HttpServletDispatch>())
{
    tcp_server_.setConnectionCallback(std::bind(&HttpProjectServer::onConnect, this, std::placeholders::_1));

    tcp_server_.setMessageCallback(std::bind(&HttpProjectServer::onMessage, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
}

HttpProjectServer::~HttpProjectServer()
{
    stop();
}

const kit_muduo::InetAddress& HttpProjectServer::getBindAddr() const 
{
    return tcp_server_.getBindAddr(); 
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

    std::weak_ptr<HttpProtocolItem> weak_http_item = http_item;

    auto route_result = dispatch_->addRoute(ToMethodMask(req_cfg.method), req_cfg.path,  [this, 
        weak_http_item,
        pcId = http_item->getId()](kit_muduo::TcpConnectionPtr conn, kit_muduo::HttpContextPtr ctx){

        auto http_item = weak_http_item.lock();
        if(!http_item)
        {
            PJSERVER_F_INFO("http protocol item null! pcId[%ld]\n", pcId);
            return;
        }
    
        HttpProjectProcess(http_item, conn, ctx);
    }); 

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
    if(!dispatch_->removeRoute(route_id))
    {
        result.error.set(RuntimeError::kRouteNotFound);
        return result;
    }

    // 2. 删缓存
    it->second.item->cache()->close();
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

RuntimeResult<void> HttpProjectServer::UpdateBodyProtocolItem(int64_t protocol_id, ProtocolSide side, const ProtocolBodyType body_type, const std::vector<char> &body_data)
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

    if(ProtocolSide::kRequest == side)
    {
        it->second.item->setReqBody(body_type, body_data);
    }
    else
    {
        it->second.item->setRespBody(body_type, body_data);
    }

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

std::shared_ptr<CustomTcpPattern> HttpProjectServer::GetPatternInfo() 
{ 
    return nullptr; 
}

RuntimeResult<void> HttpProjectServer::ReplaceReqCfgProtocolItem(const HttpRuntimeItem& http_run_item, const HttpItemReqHeaderCfg &new_req_cfg)
{
    RuntimeResult<void> result;
    auto http_item = http_run_item.item;
    std::weak_ptr<HttpProtocolItem> weak_http_item{http_item};

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
    auto route_result = dispatch_->addRoute(ToMethodMask(new_req_cfg.method), new_req_cfg.path, [this, 
        weak_http_item,
        pcId = http_item->getId()](kit_muduo::TcpConnectionPtr conn, kit_muduo::HttpContextPtr ctx){
            
        auto http_item = weak_http_item.lock();
        if(!http_item)
        {
            PJSERVER_F_INFO("http protocol item null! pcId[%ld]\n", pcId);
            return;
        }
    
        HttpProjectProcess(http_item, conn, ctx);
    });
    if(!route_result.ok())
    {
        PJSERVER_F_ERROR("add route failed! pjId[%d], pcId[%d], path[%s], method[%s] \n", project_id_,http_item->getId(), new_req_cfg.path.c_str(), new_req_cfg.method.toStr());

        result.error.set(RuntimeError::kRouteConflict);
        return result;
    }

    // 先删旧路由
    if(!dispatch_->removeRoute(old_route_id))
    {

        // 回滚新增加的路由
        if(!dispatch_->removeRoute(route_result.route_id))
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
    && kit_muduo::http::NormalizeHttpPath(old_cfg.path) == kit_muduo::http::NormalizeHttpPath(new_cfg.path);
}

std::shared_ptr<HttpProtocolItem> HttpProjectServer::findRuntimeItem(int64_t protocol_id) const
{
    auto it = http_items_.find(protocol_id);

    return it == http_items_.end() ? nullptr : it->second.item;
}

void HttpProjectServer::onConnect(kit_muduo::TcpConnectionPtr conn)
{
    if(conn->connected())
    {
        PJSERVER_F_INFO("==> new connection fd[%d][%s] \n", conn->fd(), conn->peerAddr().toIpPort().c_str());

        conn->setContext(std::make_shared<HttpContext>());
    }
    else
    {
        PJSERVER_F_INFO("==> disconnected connection  fd[%d][%s] \n", conn->fd(), conn->peerAddr().toIpPort().c_str());
    }
}

void HttpProjectServer::onMessage(kit_muduo::TcpConnectionPtr conn, kit_muduo::Buffer *buf, kit_muduo::TimeStamp receiveTime)
{
    bool is_exception = false;
    std::shared_ptr<HttpContext> context = std::static_pointer_cast<HttpContext>(conn->getContext());
    if(nullptr == context)
    {
        PJSERVER_ERROR() << "http context is null!" << std::endl;
        return;
    }

    while(buf->readableBytes() > 0)
    {
        size_t before_len = buf->readableBytes();

        if(!context->parseRequest(*buf, receiveTime))
        {
            PJSERVER_ERROR() << "http request parse error! " << std::endl;
            BadRequest400Servlet::Handle(conn, context);
            sendAndObserve(conn,
                context,
                nullptr,
                InteractionResult::kParseError,
                "http request parse error",
                true);
            return;
        }

        // 未解析完 等待更多数据
        if(!context->gotAll())
        {
            PJSERVER_F_DEBUG("http data not complete! %lu --> %lu \n", before_len, buf->readableBytes());
            break;
        }

        try {

            handleRequest(conn, context);
  
        } catch(const std::exception &e) {

            PJSERVER_F_ERROR("http callback exception: %s \n", e.what());

            is_exception = true;
        } catch(...) {

            PJSERVER_F_ERROR("http callback unknown exception\n");

            is_exception = true;
        }

        if(is_exception)
        {
            ServerErr500Servlet::Handle(conn, context);
            context->response()->setConnectionClosed(true);
            context->response()->resetBodyData();

            sendAndObserve(conn,
                context,
                nullptr,
                InteractionResult::kInternalError,
                "http callback exception",
                true);
            return;
        }
        // 重置conn中的上下文
        context = std::make_shared<HttpContext>();
        conn->setContext(context);

    }


}

void HttpProjectServer::handleRequest(kit_muduo::TcpConnectionPtr conn, kit_muduo::HttpContextPtr ctx)
{
    auto req = ctx->request();
    auto resp = ctx->response();

    PJSERVER_F_INFO("woker thread [%d][%s] ===> %s \n", conn->fd(), conn->name().c_str(), req->path().c_str());

    const std::string &connection = req->getHeader("Connection");

    bool closed = HeaderContainsToken(connection, "close")
            || (Version::kHttp10 == req->version().toInt() && !HeaderContainsToken(connection, "keep-alive"));
    resp->setConnectionClosed(closed);

    // 注意 这里只需要匹配结果 而并不需要直接进行对应的业务执行
    auto outcome = dispatch_->handleWithOutcome(conn, ctx);

    // 不匹配需要走 project notice
    if(MatchStatus::kFound != outcome.status)
    {
        sendAndObserve(conn, 
            ctx,
            nullptr, 
            ProtocolInteractionObservation::ToInterResult(outcome.status), 
            "", 
            resp->connectionClosed());
    }
}

void HttpProjectServer::sendAndObserve(TcpConnectionPtr conn,
    HttpContextPtr ctx,
    std::shared_ptr<HttpProtocolItem> http_item,
    InteractionResult result,
    const std::string &message,
    bool close_after_send)
{
    auto resp = ctx->response();
    auto response_bytes =
        std::make_shared<std::vector<uint8_t>>(resp->toBytes());

    ProtocolInteractionObservation obs = buildHttpObservation(ctx,
        conn->peerAddr().toIpPort(),
        result,
        http_item,
        message);

    emitObserve(std::move(obs));

    conn->send(*response_bytes);

    if(close_after_send)
    {
        conn->shutdown();
    }

}

ProtocolInteractionObservation HttpProjectServer::buildHttpObservation(HttpContextPtr ctx,
    const std::string &peer_addr,
    InteractionResult result,
    std::shared_ptr<HttpProtocolItem> http_item,
    const std::string &message)
{
    auto req = ctx->request();
    auto resp = ctx->response();
    auto cache = http_item ? http_item->cache() : notice_cache_;;

    ProtocolInteractionObservation obs;
    obs.project_id = project_id_;
    obs.protocol_id = http_item ? http_item->getId() : 0;
    obs.cache_instance_id = cache->cacheInstanceId();
    obs.scope = http_item ? InteractionScope::kProtocol : InteractionScope::kProject;
    obs.protocol_type = ProtocolType::kHttp;
    obs.time_ms = req->receiveTime().millSeconds();
    obs.peer_addr = std::move(peer_addr);
    obs.result = result;
    obs.error_message = std::move(message);
    obs.weak_record_cache = cache;

    if(InteractionScope::kProtocol == obs.scope)
    {
        const auto &req_body_view = http_item->getReqBodyView();
        const auto &resp_body_view = http_item->getRespBodyView();

        AttachHttpRequestCaptureFromContext(
            obs,
            req,
            req_body_view.body_type);

        AttachHttpResponseCaptureFromContext(
            obs,
            resp,
            resp_body_view.body_type);
    }
    else
    {
        // 解析上下文状态 >=kExpectBody 说明请求头已经解析完 但方法不匹配无法知道期望类型
        if(ctx->state() >= HttpContext::kExpectBody)
        {
            AttachHttpRequestCaptureFromContext(
                obs, 
                req, 
                ProtocolBodyType::kNone);
        }
        // 解析上下文状态 <kExpectBody  说明请求头就是出错的
        else if(ctx->state() < HttpContext::kExpectBody)
        {
            obs.request.raw_bytes = ctx->rawCapture();
        }

        AttachHttpResponseCaptureFromContext(
            obs, 
            resp, 
            ProtocolBodyType::kNone);
    }
    
    return obs;

}




/**
 * @brief (校验功能暂时弃用)json数据比对辅助(只比对key值是否正确)
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


void HttpProjectServer::HttpProjectProcess(std::shared_ptr<HttpProtocolItem> http_item, TcpConnectionPtr conn, HttpContextPtr ctx)
{
    auto req = ctx->request();
    auto resp = ctx->response();
    resp->setVersion(Version::kHttp11);

#if 0
    // 可以无锁 因为所有协议项增删改查都是成队列形式
    auto http_item = findRuntimeItem(protocol_id);
    if(!http_item)
    {
        PJSERVER_F_ERROR("url[%s] protocol item is nullptr! \n", req->path().c_str());
        
        ctx->setAttribute(kAttrInteractionResult, "protocol_not_found");
        ctx->setAttribute(kAttrInteractionErrorMessage, "protocol item not found");
        resp->setStateCode(StateCode::k404NotFound);
        return;
    }
#endif
    
    auto req_cfg = http_item->getReqCfg();
    auto resp_cfg = http_item->getRespCfg();
    const auto& req_cfg_body_view = http_item->getReqBodyView();
    const auto& resp_cfg_body_view = http_item->getRespBodyView();
    

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
    // TODO 请求校验模块尚未成型
{
#if 0

    try {
        if(!req_cfg_body_view.body_data->empty())
        {
            const ContentMeta& actual_meta = req->contentMeta();
            const ContentMeta& expected_meta = req_cfg_body_view.meta;
            if(req->bodyData().empty())
            {
                PJSERVER_F_ERROR("protocol body is empty! expect_media_type[%s]\n",
                    expected_meta.media_type.c_str());

                resp->setStateCode(StateCode::k200Ok);
                resp->setJson({{"code", -200}, {"message", "body parse error!"}});
                return;
            }

            if(!IsContentTypeMatch(actual_meta, expected_meta))
            {
                PJSERVER_F_ERROR("protocol content type mismatch! real[%s] - expect[%s] \n",
                    actual_meta.media_type.c_str(),
                    expected_meta.media_type.c_str());

                resp->setStateCode(StateCode::k200Ok);
                resp->setJson({{"code", -200}, {"message", "media type mismatch"}});

                return;
            }

            // TODO 这里需要大量的模版方法模式
            if(ProtocolBodyType::kJson == req_cfg_body_view.body_type)
            {
                PJSERVER_F_DEBUG("protocol body type is json! \n");
                auto req_root = std::make_unique<nljson>(nljson::parse(req->bodyData()));
                PJSERVER_DEBUG() << "req body: \n" << req_root->dump(4) << std::endl;
                auto req_cfg_root = std::make_unique<nljson>(nljson::parse(*req_cfg_body_view.body_data));
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
            // 请求Body没配情况下 默认怎么处理?
            PJSERVER_F_WARN("protocol body is empty! [%d][%s][%d], url[%s], cfg_body_type[%s] \n",
                http_item->getId(),
                http_item->getName().c_str(),
                http_item->getProjectId(),
                req->path().c_str(), 
                ProtocolBodyTypeToString(req_cfg_body_view.body_type).c_str());
        }
    } catch (const std::exception& e) {

        PJSERVER_F_ERROR("protocol body parse exception! %s \n", e.what());

        resp->setStateCode(StateCode::k200Ok);
        resp->setJson({{"code", -200}, {"message", "body parse error!"}});

        return;
    }

#endif
}

    PJSERVER_F_INFO("pjId[%d] pcId[%d] name[%s] match success!\n", 
        http_item->getProjectId(),
        http_item->getId(),
        http_item->getName().c_str()
    );

    // 响应直接使用协议项配置阶段生成的运行态 body/meta 快照。
    resp->setStateCode(resp_cfg.state_code);
    resp->setHeaders(resp_cfg.headers);

    resp->setContentMeta(resp_cfg_body_view.meta);
    resp->setBodyData(*resp_cfg_body_view.body_data);

    PJSERVER_DEBUG() << std::endl << resp->toString() << std::endl;

    sendAndObserve(conn, 
        ctx, 
        http_item,
        InteractionResult::kMatched, 
        "service handle ok", 
        resp->connectionClosed());
    return;
}

void HttpProjectServer::closeAllProtocolInteractionCaches()
{
    std::lock_guard<std::mutex> lock(mtx_);
    for (auto& [protocol_id, runtime_item] : http_items_)
    {
        if (runtime_item.item)
        {
            runtime_item.item->cache()->close();
        }
    }
}

}
