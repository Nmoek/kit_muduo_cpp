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

#include "nlohmann/json.hpp"
#include <assert.h>

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
        PJ_F_ERROR("custom protocol item is nullptr! \n");

        result.error.set(RuntimeError::kNullProtocolItem);
        return result;
    }

    // 1. 配置的协议校验内容缓存
    // 注意这里的结构主要是配合数据库对账和快速索引的
    // 2. 功能码映射 区分到底是哪一条协议
    {
        std::lock_guard<std::mutex> lock(mtx_);
        protocol_items_.emplace(item->getId(), item);
        func_codes_[tcp_item->getReq()->functionCodeFieldValue()] = tcp_item;

    }

    PJSERVER_DEBUG() << "CustomTcpProjectServer::AddProtocolItem ok, " << tcp_item->getReq()->functionCodeFieldValue() << std::endl;

    return result;
}

RuntimeResult<void> CustomTcpProjectServer::DelProtocolItem(int64_t protocol_id)
{
    RuntimeResult<void> result;

    std::lock_guard<std::mutex> lock(mtx_);
    auto it = protocol_items_.find(protocol_id);

    if(it == protocol_items_.end()) 
    {
        PJ_F_ERROR("CustomTcpProjectServer: Protocol item not found, project_id[%d],protocol_id[%d] \n",  project_id_, protocol_id);

        result.error.set(RuntimeError::kProtocolItemNotFound);
        return result;
    } 
    auto tcp_item = std::dynamic_pointer_cast<CustomTcpProtocolItem>(it->second);

    assert(tcp_item);

    const std::string &func_code_str = tcp_item->getReq()->functionCodeFieldValue();

    auto it2 = func_codes_.find(func_code_str);
    if(it2 == func_codes_.end()) 
    {
        PJ_F_ERROR("CustomTcpProjectServer: func code not found, project_id[%d], protocol_id[%d], func_code[%s]\n",  project_id_, protocol_id, func_code_str.c_str());

        result.error.set(RuntimeError::kRouteOperationError);
        return result;
    } 
    CUSTOM_F_DEBUG("CustomTcpProjectServer: Deleted protocol item, pproject_id[%d], protocol_id[%d] \n",  project_id_, protocol_id);

    // 一并删除
    protocol_items_.erase(it);
    func_codes_.erase(it2);

    return result;
}

RuntimeResult<std::shared_ptr<ProtocolItem>> CustomTcpProjectServer::GetProtocolItem(int64_t protocol_id)
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

RuntimeResult<void> CustomTcpProjectServer::UpdateReqCfgProtocolItem(int64_t protocol_id, const nljson& req_cfg_json)
{
    RuntimeResult<void> result;
    std::lock_guard<std::mutex> lock(mtx_);
    
    auto it = protocol_items_.find(protocol_id);
    if(it == protocol_items_.end())
    {
        result.error.set(RuntimeError::kProtocolItemNotFound);
        return result;
    }


    auto tcp_item  = std::dynamic_pointer_cast<CustomTcpProtocolItem>(it->second);
    if(!tcp_item)
    {
        result.error.set(RuntimeError::kNullProtocolItem);
        return result;
    }

    auto it2 = func_codes_.find(tcp_item->getReq()->functionCodeFieldValue());
    if(it2 == func_codes_.end())
    {
        result.error.set(RuntimeError::kRouteOperationError);
        return result;
    }
    func_codes_.erase(it2);

    if(!tcp_item->setReqCfg(req_cfg_json))
    {
        CUSTOM_F_ERROR("req json parse error!\n");
        result.error.set(RuntimeError::kInvalidProtocolConfig);
        return result;
    }

    func_codes_[tcp_item->getReq()->functionCodeFieldValue()] = tcp_item;

    return result;
}

RuntimeResult<void> CustomTcpProjectServer::UpdateRespCfgProtocolItem(int64_t protocol_id, const nljson& resp_cfg_json)
{
    RuntimeResult<void> result;
    std::lock_guard<std::mutex> lock(mtx_);
    
    auto it = protocol_items_.find(protocol_id);
    if(it == protocol_items_.end())
    {
        result.error.set(RuntimeError::kProtocolItemNotFound);
        return result;
    }

    auto tcp_item  = std::dynamic_pointer_cast<CustomTcpProtocolItem>(it->second);
    if(!tcp_item)
    {
        result.error.set(RuntimeError::kNullProtocolItem);
        return result;
    }

    if(!tcp_item->setRespCfg(resp_cfg_json))
    {
        CUSTOM_F_ERROR("resp json parse error!\n");
        result.error.set(RuntimeError::kInvalidProtocolConfig);
        return result;
    }

    return result;
}

RuntimeResult<void> CustomTcpProjectServer::UpdateReqBodyProtocolItem(int64_t protocol_id, const ProtocolBodyType body_type, const std::vector<char> &body_data)
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

RuntimeResult<void> CustomTcpProjectServer::UpdateRespBodyProtocolItem(int64_t protocol_id, const ProtocolBodyType body_type,const std::vector<char> &body_data)
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
    auto it = func_codes_.find(func_code);

    return it == func_codes_.end() ? nullptr : it->second;
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
    auto tcp_item = findByFuncCode(func_code_str);
    if(!tcp_item)
    {
        CUSTOM_F_ERROR("FuncCode not found! %s \n", func_code_str.c_str());

        conn->shutdown();
        return;
    }
    auto cfg_resp = tcp_item->getResp();
    if(!cfg_resp)
    {
        CUSTOM_F_ERROR("cfg response is null! \n");

        conn->shutdown();
        return;
    }

    // 校验动作使用lua脚本执行？
    // 1. 头部检验(可配置)


    // 2. Body校验(可配置)


    // 3. 获取配置的响应 进行回发，可以采取两种形式
    // 1. 依赖人工配置
    // 2. 脚本生成:
        // 获取当前协议的脚本
        // 请求数据 => 脚本 => 响应数据
    
    try {
        const std::vector<char>& resp_data = pattern->serialize(cfg_resp, !KIT_IS_LOCAL_BIG_ENDIAN());

        if(conn->connected() && resp_data.size())
        {
            conn->send(resp_data);
        }
    }catch(const std::exception& e) {

        CUSTOM_F_ERROR("serialize fail! %s\n", e.what());

    }

    return;
}



} // namespace kit_domain
