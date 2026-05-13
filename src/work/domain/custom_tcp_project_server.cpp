/**
 * @file tcp_project_server.cpp
 * @brief TcpProjectServer类实现
 * @author ljk5
 * @version 1.0
 * @date 2025-10-21
 * @copyright Copyright (c) 2025 HIKRayin
 */
#include "domain/project_server.h"
#include "domain/protocol_item.h"
#include "domain/protocol.h"
#include "domain/runtime_result.h"
#include "net/tcp_server.h"
#include "web/web_log.h"
#include "domain/custom_tcp_context.h"
#include "domain/custom_tcp_message.h"
#include "net/http/http_util.h"
#include "domain/custom_tcp_protocol_item.h"

#include "nlohmann/json.hpp"
#include <assert.h>
#include <mutex>

using nljson = nlohmann::json;
using namespace kit_muduo;


namespace {

inline static EventLoop* CheckLoop(EventLoop *loop)
{
    assert(loop);
    return loop;
}
    
}


namespace kit_domain {


CustomTcpProjectServer::CustomTcpProjectServer(
    int64_t project_id, 
    const CustomTcpPatternType pattern_type,
    const std::vector<char> &info)
    :ProjectServer(project_id)
    ,tcp_server_(std::make_shared<TcpServer>(
        CheckLoop(loop_thread_.startLoop()), 
        InetAddress(0, "0.0.0.0"), 
        "pj" + std::to_string(project_id_) + "tcp", 
        kit_muduo::TcpServer::KReusePort
    ))
{
    pattern_info_ = CustomTcpPatternFactory::Create(pattern_type, info);

    assert(pattern_info_);

    assert(tcp_server_);
    tcp_server_->setConnectionCallback(std::bind(&CustomTcpProjectServer::onConnect, this, std::placeholders::_1));

    tcp_server_->setMessageCallback(std::bind(&CustomTcpProjectServer::onMessage, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));


}


void CustomTcpProjectServer::start() 
{
    tcp_server_->setThreadNum(0); // 使用单线程模式
    tcp_server_->start();
}

const kit_muduo::InetAddress& CustomTcpProjectServer::getBindAddr() const
{
    return tcp_server_->getBindAddr();
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
    const std::string &func_code_val = tcp_item->getReqCfg().function_code_filed_value;

    // 1. 配置的协议校验内容缓存
    // 注意这里的结构主要是配合数据库对账和快速索引的
    // 2. 功能码映射 区分到底是哪一条协议
    {
        std::lock_guard<std::mutex> lock(mtx_);
        auto p = tcp_items_.find(item->getId());
        if(p != tcp_items_.end())
        {
            PJSERVER_F_ERROR("custom protocol duplicate! pjId[%d] pcId[%d] funccode[%s]\n ", project_id_, item->getId(), func_code_val.c_str());

            result.error.set(RuntimeError::kFuncCodeConflict);
            return result;
        }

        auto p2 = func_codes2ids_.find(func_code_val);
        if(p2 != func_codes2ids_.end())
        {
            PJSERVER_F_ERROR("custom protocol duplicate! pjId[%d] pcId[%d] funccode[%s]\n ", project_id_, item->getId(), func_code_val.c_str());

            result.error.set(RuntimeError::kFuncCodeConflict);
            return result;
        }

        tcp_items_[item->getId()] = 
        {tcp_item, func_code_val};
        func_codes2ids_[func_code_val] = item->getId();

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
    const std::string& func_code_str = it->second.function_code_value;

    if(!it->second.item)
    {
        PJSERVER_F_ERROR("tcp protocol item is nullptr! protocol_id[%d] \n", protocol_id);
        
        result.error.set(RuntimeError::kNullProtocolItem);
        return result;
    }

    auto it2 = func_codes2ids_.find(func_code_str);
    if(it2 == func_codes2ids_.end()) 
    {
        PJSERVER_F_ERROR("CustomTcpProjectServer: func code not found, project_id[%d], protocol_id[%d], func_code[%s]\n",  project_id_, protocol_id, func_code_str.c_str());

        result.error.set(RuntimeError::kFuncCodeNotFound);
        return result;
    } 
    CUSTOM_F_DEBUG("CustomTcpProjectServer: Deleted protocol item, pjId[%d], pcId[%d] \n",  project_id_, protocol_id);

    // 一并删除
    tcp_items_.erase(it);
    func_codes2ids_.erase(it2);

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
    if(!new_req_cfg.fromJson(req_cfg_json, pattern_info_))
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

    if(!new_resp_cfg.fromJson(resp_cfg_json, pattern_info_))
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

std::shared_ptr<CustomTcpPattern> CustomTcpProjectServer::getPatternInfo()
{
    std::lock_guard<std::mutex> lock(pattern_info_mtx_);
    return pattern_info_;
}



std::shared_ptr<CustomTcpProtocolItem> CustomTcpProjectServer::findByFuncCode(const std::string &func_code)
{
    std::lock_guard<std::mutex> lock(mtx_);
    auto it = func_codes2ids_.find(func_code);

    return it == func_codes2ids_.end() ? nullptr : tcp_items_.at(it->second).item;
}


void CustomTcpProjectServer::onConnect(kit_muduo::TcpConnectionPtr conn)
{

    if(conn->connected())
    {
        PJ_F_INFO("==> new connection fd[%d][%s] \n", conn->fd(), conn->peerAddr().toIpPort().c_str());

        conn->setContext(std::make_shared<CustomTcpContext>(this));
    }
    else
    {
        PJ_F_INFO("==> disconnected connection  fd[%d][%s] \n", conn->fd(), conn->peerAddr().toIpPort().c_str());
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

    while(buf->readableBytes() > 0)
    {
        if(!context->parseRequest(*buf, receiveTime))
        {
            PJSERVER_ERROR() << "custom tcp request parse error! " << std::endl;

            // 出错一般直接关闭 不需要进行回发
            conn->shutdown();
        }

        if(!context->gotAll())
        {
            CUSTOM_F_INFO("custom tcp data is not complete! %ld\n", buf->readableBytes());
            break;
        }
        auto req = context->request();

        handleRequest(conn, req);
        // 重置conn中的上下文 清理实际的message对象
        context = std::make_shared<CustomTcpContext>(this);
        conn->setContext(context);
        
    }
}

RuntimeResult<void> CustomTcpProjectServer::ReplaceReqCfgProtocolItem(const CustomTcpRuntimeItem& tcp_run_item,  const CustomTcpItemCfg &new_req_cfg)
{
    RuntimeResult<void> result;
    auto tcp_item = tcp_run_item.item;
    const std::string& old_func_code_str = tcp_run_item.function_code_value;

    if(old_func_code_str == new_req_cfg.function_code_filed_value)
    {
        tcp_item->setReqCfg(new_req_cfg);
        return result;
    }

    // 先增加新的功能码
    auto p = func_codes2ids_.emplace(new_req_cfg.function_code_filed_value, tcp_item->getId());
    if(!p.second)
    {
        PJSERVER_F_ERROR("tcp protocol item func code already exist! exist: func code[%s], pcId[%d] <-----> cur: unc code[%s], pcId[%d]\n", p.first->first.c_str(), p.second, old_func_code_str.c_str(), tcp_item->getId());

        result.error.set(RuntimeError::kFuncCodeConflict);
        return result;
    }

    // 删除旧功能码
    auto n = func_codes2ids_.erase(old_func_code_str);
    if(n != 1)
    {
        n = func_codes2ids_.erase(new_req_cfg.function_code_filed_value);
        if(n != 1)
        {
            PJSERVER_F_ERROR("tcp protocol item del new funcode error! func code[%s], pcId[%d]  \n", new_req_cfg.function_code_filed_value.c_str(), tcp_item->getId());
        }
        PJSERVER_F_ERROR("tcp protocol item del old funcode error! func code[%s], pcId[%d]  \n", old_func_code_str.c_str(), tcp_item->getId());

        result.error.set(RuntimeError::kFuncCodeNotFound);
        return result;
    }

    tcp_item->setReqCfg(new_req_cfg);
    tcp_items_[tcp_item->getId()] = {tcp_item, new_req_cfg.function_code_filed_value};
    
    return result;
}


void CustomTcpProjectServer::handleRequest(kit_muduo::TcpConnectionPtr conn, std::shared_ptr<CustomTcpMessage> req)
{
    // 把收到的二进制头部进行打印
    auto pattern = getPatternInfo();
    
    auto cfg_func_code_field = pattern->functionCodeField();
    
    auto func_code_field = req->getField(cfg_func_code_field->byte_pos());
  
    if(!func_code_field)
    {
        CUSTOM_F_ERROR("getField not found! %d \n", cfg_func_code_field->byte_pos());

        conn->shutdown();
        return;
    }

    // 加载响应的格式
    const std::string& func_code_str = func_code_field->toHexString(!KIT_IS_LOCAL_BIG_ENDIAN());
    
    std::unique_lock<std::mutex> lock(mtx_);
    
    auto it = func_codes2ids_.find(func_code_str);
    if(it == func_codes2ids_.end())
    {
        CUSTOM_F_ERROR("FuncCode not found! %s \n", func_code_str.c_str());

        conn->shutdown();
        return;
    }
    auto tcp_item = tcp_items_.at(it->second).item;

    auto req_cfg = tcp_item->getReqCfg().clone();
    auto resp_cfg = tcp_item->getRespCfg().clone();
    const auto& req_body_view = tcp_item->getReqBodyView();
    const auto& resp_body_view = tcp_item->getRespBodyView();
    
    lock.unlock();

    // TODO 校验动作使用lua脚本执行？
    // 1. 头部检验(可配置)


    // 2. Body校验(可配置)


    // 3. 获取配置的响应 进行回发，可以采取两种形式
    // 1. 依赖人工配置
    // 2. 脚本生成:
        // 获取当前协议的脚本
        // 请求数据 => 脚本 => 响应数据
    
    try {
        const std::vector<char>& resp_data = pattern->serialize(resp_cfg, *resp_body_view.body_data,!KIT_IS_LOCAL_BIG_ENDIAN());

        if(!resp_data.empty())
        {
            conn->send(resp_data);
        }
    }catch(const std::exception& e) {

        CUSTOM_F_ERROR("serialize fail! %s\n", e.what());

    }

    return;
}



} // namespace kit_domain
