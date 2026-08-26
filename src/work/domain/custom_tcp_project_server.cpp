/**
 * @file tcp_project_server.cpp
 * @brief TcpProjectServer类实现
 * @author ljk5
 * @version 1.0
 * @date 2025-10-21
 * @copyright Copyright (c) 2025 HIKRayin
 */
#include "domain/custom_tcp_project_server.h"
#include "domain/protocol_interaction.h"
#include "domain/protocol_item.h"
#include "domain/runtime_result.h"
#include "domain/type.h"
#include "net/call_backs.h"
#include "net/tcp_server.h"
#include "domain/domain_log.h"
#include "domain/custom_tcp_context.h"
#include "domain/custom_tcp_message.h"
#include "domain/custom_tcp_protocol_item.h"
#include "domain/custom_tcp_pattern.h"
#include "domain/runtime_loop_pool.h"
#include "domain/protocol_interaction_observation.h"

#include "nlohmann/json.hpp"
#include <assert.h>
#include <exception>
#include <memory>
#include <mutex>
#include <optional>

using nljson = nlohmann::json;
using namespace kit_muduo;
using namespace kit_domain;


namespace {

void AttachTcpRequestCaptureFromContext(
    ProtocolInteractionObservation &obs,
    const CustomTcpMessagePtr &req,
    ProtocolBodyType expect_body_type)
{

    obs.request.meta = {
        {"function_code", req->functionCodeHex()},
        {"header_size", req->getHeaderLen()},
        {"body_size", req->bodyData().size()},
    };

    obs.request.head_text = req->toHeaderString();
    obs.request.body_bytes = req->bodyData();
    obs.request.expect_body_type = expect_body_type;
    obs.request.prefer_hex_text_for_binary = expect_body_type == ProtocolBodyType::kBinary;

}

void AttachTcpResponseCaptureFromContext(
    ProtocolInteractionObservation &obs,
    const CustomTcpMessagePtr &resp,
    ProtocolBodyType expect_body_type)
{
    obs.response.meta = {
        {"function_code", resp->functionCodeHex()},
        {"header_size", resp->getHeaderLen()},
        {"body_size", resp->bodyData().size()},
    };

    obs.response.head_text = resp->toHeaderString();
    obs.response.body_bytes = resp->bodyData();
    obs.response.expect_body_type = expect_body_type;
    obs.response.prefer_hex_text_for_binary = expect_body_type == ProtocolBodyType::kBinary;
}

}


namespace kit_domain {


CustomTcpProjectServer::CustomTcpProjectServer(int64_t project_id, const std::vector<char> &info, std::shared_ptr<RuntimeLease> lease_loop, const kit_muduo::InetAddress &addr)
    :ProjectServer(
        project_id, 
        lease_loop,
        addr,
        "pj" + std::to_string(project_id) + "tcp")
{
    try
    {
        pattern_info_ = CustomTcpPatternFactory::Create(nljson::parse(info));
    }
    catch(const std::exception& e)
    {
        PJSERVER_F_ERROR("custom tcp pattern parse exception: %s\n", e.what());
    }

    assert(pattern_info_);

    tcp_server_.setConnectionCallback(std::bind(&CustomTcpProjectServer::onConnect, this, std::placeholders::_1));

    tcp_server_.setMessageCallback(std::bind(&CustomTcpProjectServer::onMessage, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));


}

CustomTcpProjectServer::~CustomTcpProjectServer()
{
    stop();
}


const kit_muduo::InetAddress& CustomTcpProjectServer::getBindAddr() const
{
    return tcp_server_.getBindAddr();
}


RuntimeResult<void> CustomTcpProjectServer::AddProtocolItem(std::shared_ptr<ProtocolItem> item)
{
    RuntimeResult<void> result;
    auto tcp_item = std::dynamic_pointer_cast<CustomTcpProtocolItem>(item);
    if(!tcp_item)
    {
        PJSERVER_F_ERROR("custom protocol item is nullptr! \n");

        result.error.set(RuntimeError::kNullProtocolItem);
        return result;
    }
    const std::string &func_code_val = tcp_item->getReqCfg().function_code;

    int64_t protocol_id = tcp_item->getId();
    std::weak_ptr<CustomTcpProtocolItem> weak_tcp_item{tcp_item};

    // 1. 配置的协议校验内容缓存
    // 注意这里的结构主要是配合数据库对账和快速索引的
    // 2. 功能码映射 区分到底是哪一条协议
    {
        std::lock_guard<std::mutex> lock(mtx_);
        auto it = tcp_items_.find(protocol_id);
        auto cb = findCBByFuncCodeUnLock(tcp_item->getReqCfg().function_code);
        if(it != tcp_items_.end() || nullptr != cb)
        {
            PJSERVER_F_ERROR("custom protocol duplicate! pjId[%d] pcId[%d] funccode[%s]\n ", project_id_, item->getId(), func_code_val.c_str());

            result.error.set(RuntimeError::kFuncCodeConflict);
            return result;
        }
        tcp_items_[protocol_id] = CustomTcpRuntimeItem{
            .protocol_id = protocol_id,
            .function_code_value = func_code_val,
            .item = tcp_item,
            .cb = [
                this, 
                weak_tcp_item,
                protocol_id](TcpConnectionPtr conn, CustomTcpContextPtr ctx){
                auto tcp_item = weak_tcp_item.lock();
                if(!tcp_item)
                {
                    PJSERVER_F_INFO("custom tcp protocol item null! pcId[%ld]\n", protocol_id);
                    return;
                }
                CustomTcpProcess(tcp_item, conn, ctx);
            }
        };
    }

    PJSERVER_DEBUG() << "CustomTcpProjectServer::AddProtocolItem ok" << std::endl;

    return result;
}

RuntimeResult<void> CustomTcpProjectServer::DelProtocolItem(int64_t protocol_id)
{
    RuntimeResult<void> result;

    std::lock_guard<std::mutex> lock(mtx_);
    auto it = tcp_items_.find(protocol_id);

    if(it == tcp_items_.end()) 
    {
        PJSERVER_F_ERROR("CustomTcpProjectServer: Protocol item not found, pjId[%d], pcId[%d] \n",  project_id_, protocol_id);

        result.error.set(RuntimeError::kProtocolItemNotFound);
        return result;
    } 

    if(!it->second.item)
    {
        PJSERVER_F_ERROR("tcp protocol item is nullptr! protocol_id[%d] \n", protocol_id);
        
        result.error.set(RuntimeError::kNullProtocolItem);
        return result;
    }

    CUSTOM_F_DEBUG("CustomTcpProjectServer: Deleted protocol item, pjId[%d], pcId[%d] \n",  project_id_, protocol_id);

    // 缓存删除
    it->second.item->cache()->close();
    tcp_items_.erase(it);

    return result;
}

RuntimeResult<std::shared_ptr<ProtocolItem>> CustomTcpProjectServer::GetProtocolItem(int64_t protocol_id)
{
    RuntimeResult<std::shared_ptr<ProtocolItem>> result;

    std::lock_guard<std::mutex> lock(mtx_);
    auto it = tcp_items_.find(protocol_id);
    if(it == tcp_items_.end())
    {
        result.error.set(RuntimeError::kProtocolItemNotFound);
    }
    else
    {
        result.val = it->second.item;
    }
    return result;
}

RuntimeResult<void> CustomTcpProjectServer::UpdateReqCfgProtocolItem(int64_t protocol_id, const nljson& req_cfg_json)
{
    RuntimeResult<void> result;
    CustomTcpItemCfg new_req_cfg;

    std::unique_lock<std::mutex> lock(pattern_info_mtx_);
    if(!new_req_cfg.fromJson(req_cfg_json, pattern_info_->spec()))
    {
        PJSERVER_F_ERROR("tcp req cfg jons parse error!\n");
        result.error.set(RuntimeError::kInvalidProtocolConfig);
        return result;
    }
    lock.unlock();

    std::lock_guard<std::mutex> lock2(mtx_);
    
    auto it = tcp_items_.find(protocol_id);
    if(it == tcp_items_.end()) 
    {
        PJSERVER_F_ERROR("CustomTcpProjectServer: Protocol item not found, pjId[%d], pcId[%d] \n",  project_id_, protocol_id);

        result.error.set(RuntimeError::kProtocolItemNotFound);
        return result;
    } 

    if(!it->second.item)
    {
        PJSERVER_F_ERROR("tcp protocol item is nullptr! protocol_id[%d] \n", protocol_id);
        
        result.error.set(RuntimeError::kNullProtocolItem);
        return result;
    }

    return ReplaceReqCfgProtocolItem(it->second, new_req_cfg);
}

RuntimeResult<void> CustomTcpProjectServer::UpdateRespCfgProtocolItem(int64_t protocol_id, const nljson& resp_cfg_json)
{
    RuntimeResult<void> result;
    CustomTcpItemCfg new_resp_cfg;

    if(!new_resp_cfg.fromJson(resp_cfg_json, pattern_info_->spec()))
    {
        CUSTOM_F_ERROR("resp json parse error!\n");
        result.error.set(RuntimeError::kInvalidProtocolConfig);
        return result;
    }

    std::lock_guard<std::mutex> lock(mtx_);
    
    auto it = tcp_items_.find(protocol_id);
    if(it == tcp_items_.end()) 
    {
        PJSERVER_F_ERROR("CustomTcpProjectServer: Protocol item not found, pjId[%d], pcId[%d] \n",  project_id_, protocol_id);

        result.error.set(RuntimeError::kProtocolItemNotFound);
        return result;
    } 

    if(!it->second.item)
    {
        PJSERVER_F_ERROR("tcp protocol item is nullptr! protocol_id[%d] \n", protocol_id);
        
        result.error.set(RuntimeError::kNullProtocolItem);
        return result;
    }

    it->second.item->setRespCfg(new_resp_cfg);


    return result;
}



RuntimeResult<void> CustomTcpProjectServer::UpdateBodyProtocolItem(int64_t protocol_id, ProtocolSide side, const ProtocolBodyType body_type, const std::vector<char> &body_data)
{
    RuntimeResult<void> result;

    std::lock_guard<std::mutex> lock(mtx_);
    auto it = tcp_items_.find(protocol_id);
    if(it == tcp_items_.end())
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

RuntimeResult<void> CustomTcpProjectServer::UpdateReqBodyProtocolItem(int64_t protocol_id, const ProtocolBodyType body_type, const std::vector<char> &body_data)
{
    RuntimeResult<void> result;

    std::lock_guard<std::mutex> lock(mtx_);
    auto it = tcp_items_.find(protocol_id);
    if(it == tcp_items_.end())
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

RuntimeResult<void> CustomTcpProjectServer::UpdateRespBodyProtocolItem(int64_t protocol_id, const ProtocolBodyType body_type,const std::vector<char> &body_data)
{
    RuntimeResult<void> result;

    std::lock_guard<std::mutex> lock(mtx_);
    auto it = tcp_items_.find(protocol_id);
    if(it == tcp_items_.end())
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

RuntimeResult<void> CustomTcpProjectServer::setPatternInfo(const std::shared_ptr<CustomTcpPattern> pattern)
{
    std::lock_guard<std::mutex> lock(pattern_info_mtx_);
    pattern_info_ = pattern;
    return RuntimeResult<void>();
}

std::shared_ptr<CustomTcpPattern> CustomTcpProjectServer::GetPatternInfo()
{
    std::lock_guard<std::mutex> lock(pattern_info_mtx_);
    return pattern_info_;
}



ProcessCallback CustomTcpProjectServer::findCBByFuncCode(const std::string &func_code)
{
    std::lock_guard<std::mutex> lock(mtx_);
    return findCBByFuncCodeUnLock(func_code);
}

ProcessCallback CustomTcpProjectServer::findCBByFuncCodeUnLock(const std::string&func_code)
{
    for(auto &it : tcp_items_)
    {
        if(func_code == it.second.function_code_value)
        {
            return it.second.cb;
        }
    }
    return nullptr;
}

void CustomTcpProjectServer::sendAndObserve(kit_muduo::TcpConnectionPtr conn,
    CustomTcpContextPtr ctx,
    std::shared_ptr<CustomTcpProtocolItem> tcp_item,
    InteractionResult result,
    std::string message,
    bool is_shutdown)
{
    std::optional<std::vector<uint8_t>> data = std::nullopt;
    if(!is_shutdown && nullptr != tcp_item)
    {
        data = ctx->response()->toBytes();
        if(!data.has_value())
        {
            CUSTOM_F_ERROR("custom tcp message serialize error!\n");
            is_shutdown = true;
            result = InteractionResult::kSerializeError;
            message = "serialize error";
        }
    }

    ProtocolInteractionObservation obs = buildCustomTcpObservation(ctx,
        conn->peerAddr().toIpPort(),
        result,
        tcp_item,
        std::move(message));

    emitObserve(std::move(obs));

    // 注意这里和http不一样 出错就立马关闭 不向客户端发送任何东西
    if(is_shutdown)
    {
        conn->shutdown();
    }
    else
    {
        conn->send(data.value());
    }

}

ProtocolInteractionObservation CustomTcpProjectServer::buildCustomTcpObservation(CustomTcpContextPtr ctx,
    const std::string &peer_addr,
    InteractionResult result,
    std::shared_ptr<CustomTcpProtocolItem> tcp_item,
    const std::string &message)
{
    auto req = ctx->request();
    auto resp = ctx->response();
    auto cache = tcp_item ? tcp_item->cache() : notice_cache_;

    ProtocolInteractionObservation obs;
    obs.project_id = project_id_;
    obs.protocol_id = tcp_item ? tcp_item->getId() : 0;
    obs.cache_instance_id = cache->cacheInstanceId();
    obs.scope = tcp_item ? InteractionScope::kProtocol : InteractionScope::kProject;
    obs.protocol_type = ProtocolType::kCustomTcp;
    obs.time_ms = req->recordTime().millSeconds();
    obs.peer_addr = std::move(peer_addr);
    obs.result = result;
    obs.error_message = std::move(message);
    obs.weak_record_cache = cache;

    if(InteractionScope::kProtocol == obs.scope)
    {
        const auto &req_body_view = tcp_item->getReqBodyView();
        const auto &resp_body_view = tcp_item->getRespBodyView();

        AttachTcpRequestCaptureFromContext(
            obs,
            req,
            req_body_view.body_type);

        AttachTcpResponseCaptureFromContext(
            obs,
            resp,
            resp_body_view.body_type);
    }
    else
    {
        // 解析上下文状态 >=kExpectBody 说明请求头已经解析完
        if(ctx->state() >= CustomTcpContext::kExpectBody)
        {
            AttachTcpRequestCaptureFromContext(
                obs, 
                req, 
                ProtocolBodyType::kNone);
        }
        // 解析上下文状态 <kExpectBody  说明请求头就是出错的
        else if(ctx->state() < CustomTcpContext::kExpectBody)
        {
            obs.request.raw_bytes = ctx->rawCapture();
        }

        AttachTcpResponseCaptureFromContext(
            obs, 
            resp, 
            ProtocolBodyType::kNone);
    }
    
    return obs;

}



void CustomTcpProjectServer::onConnect(kit_muduo::TcpConnectionPtr conn)
{

    if(conn->connected())
    {
        PJSERVER_F_INFO("==> new connection fd[%d][%s] \n", conn->fd(), conn->peerAddr().toIpPort().c_str());

        conn->setContext(std::make_shared<CustomTcpContext>(this));
    }
    else
    {
        PJSERVER_F_INFO("==> disconnected connection  fd[%d][%s] \n", conn->fd(), conn->peerAddr().toIpPort().c_str());
    }
}

void CustomTcpProjectServer::onMessage(kit_muduo::TcpConnectionPtr conn, kit_muduo::Buffer *buf, kit_muduo::TimeStamp receiveTime)
{
    auto context = std::static_pointer_cast<CustomTcpContext>(conn->getContext());
    if(nullptr == context)
    {
        PJSERVER_ERROR() << "custom tcp context is null!" << std::endl;
        return;
    }

    bool is_exception = false;

    while(buf->readableBytes() > 0)
    {
        auto result = context->parseRequest(*buf, receiveTime);
        if(!result.ok())
        {
            PJSERVER_ERROR() << "custom tcp request parse error! " << std::endl;

            context->rawCapture().assign(buf->peek(), buf->peek() + std::min(buf->readableBytes(), context->limits().max_error_capture_bytes));

            // 出错一般直接关闭
            sendAndObserve(conn,
                context,
                nullptr,
                result.toInterResult(),
                result.message,
                true);
            return;
        }

        if(!context->gotAll())
        {
            CUSTOM_F_INFO("custom tcp data is not complete! %ld\n", buf->readableBytes());
            break;
        }

        try {
            if(result.cb)
            {
                result.cb(conn, context);
            }
            else
            {
                PJSERVER_F_ERROR("custom tcp callback null!!\n");
                sendAndObserve(conn,
                    context,
                    nullptr,
                    InteractionResult::kInternalError,
                    "custom tcp callback missing",
                    true);
                return;
            }
        } catch(const std::exception &e) {
            PJSERVER_F_ERROR("custom tcp callback exception: %s \n", e.what());

            is_exception = true;
        } catch(...) {

            PJSERVER_F_ERROR("custom tcp callback unknown exception\n");
            is_exception = true;
        }

        if(is_exception)
        {
            sendAndObserve(conn,
                context,
                nullptr,
                InteractionResult::kInternalError,
                "custom tcp callback exception",
                true);
            return;
        }

        // 重置conn中的上下文 清理实际的message对象
        context = std::make_shared<CustomTcpContext>(this);
        conn->setContext(context);
        
    }
}

RuntimeResult<void> CustomTcpProjectServer::ReplaceReqCfgProtocolItem(CustomTcpRuntimeItem& tcp_run_item,  const CustomTcpItemCfg &new_req_cfg)
{
    RuntimeResult<void> result;
    auto tcp_item = tcp_run_item.item;
    const std::string& old_func_code_str = tcp_run_item.function_code_value;

    if(old_func_code_str == new_req_cfg.function_code)
    {
        tcp_item->setReqCfg(new_req_cfg);
        return result;
    }

    // 检查是否和其他功能码冲突
    if(nullptr != findCBByFuncCodeUnLock(new_req_cfg.function_code))
    {
        PJSERVER_F_ERROR("tcp protocol item func code already exist! exist: func code[%s], pcId[%d] \n", old_func_code_str.c_str(), tcp_item->getId());

        result.error.set(RuntimeError::kFuncCodeConflict);
        return result;
    }

    tcp_run_item.function_code_value = new_req_cfg.function_code;
    tcp_item->setReqCfg(new_req_cfg);
    
    return result;
}


void CustomTcpProjectServer::CustomTcpProcess(std::shared_ptr<CustomTcpProtocolItem> tcp_item, kit_muduo::TcpConnectionPtr conn, CustomTcpContextPtr ctx)
{
    InteractionResult inter_result{InteractionResult::kMatched};

    auto pattern = GetPatternInfo();

    auto resp_cfg = tcp_item->getRespCfg();
    const auto& resp_cfg_body_view = tcp_item->getRespBodyView();
    std::string message = "service handle ok";

    // TODO 校验动作使用lua脚本执行？
    // 1. 头部检验(可配置)


    // 2. Body校验(可配置)


    // 3. 获取配置的响应 进行回发，可以采取两种形式
    // 1. 依赖人工配置
    // 2. 脚本生成:
        // 获取当前协议的脚本
        // 请求数据 => 脚本 => 响应数据
    ctx->response()->setBodyData(*resp_cfg_body_view.body_data);

    if(!pattern->assembleMessageFromCfg(ctx->response(), resp_cfg, resp_cfg_body_view.body_data->size()))
    {
        PJSERVER_F_ERROR("custom tcp message assemble error! pcId[%ld]\n", tcp_item->getId());
        message = "assemble message error";
        inter_result = InteractionResult::kSerializeError;
    }

    sendAndObserve(conn, 
        ctx,
        tcp_item,
        inter_result,
        std::move(message),
        inter_result != InteractionResult::kMatched);
    return;
}

void CustomTcpProjectServer::closeAllProtocolInteractionCaches()
{
    std::lock_guard<std::mutex> lock(mtx_);
    for (auto& [protocol_id, runtime_item] : tcp_items_)
    {
        if (runtime_item.item)
        {
            runtime_item.item->cache()->close();
        }
    }
}

} // namespace kit_domain
