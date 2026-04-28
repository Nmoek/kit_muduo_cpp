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
#include "net/tcp_server.h"
#include "web/web_log.h"
#include "domain/custom_tcp_context.h"
#include "domain/custom_tcp_message.h"
#include "net/http/http_util.h"

#include "nlohmann/json.hpp"
#include <assert.h>

using nljson = nlohmann::json;

namespace kit_domain {


CustomTcpProjectServer::CustomTcpProjectServer(
    int64_t project_id, 
    kit_muduo::TcpServerPtr tcp_server, 
    const CustomTcpPatternType pattern_type,
    const std::vector<char> &info)
    :ProjectServer(project_id)
    ,_tcp_server(tcp_server)
{
    _pattern_info = CustomTcpPatternFactory::Create(pattern_type, info);

    assert(_pattern_info);

    _tcp_server->setConnectionCallback(std::bind(&CustomTcpProjectServer::onConnect, this, std::placeholders::_1));

    _tcp_server->setMessageCallback(std::bind(&CustomTcpProjectServer::onMessage, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));

}

kit_muduo::EventLoop* CustomTcpProjectServer::getLoop() const
{
    if(_tcp_server)
        assert("TcpServer pointer is null! \n");
    return _tcp_server->getLoop();
}

void CustomTcpProjectServer::AddProtocolItem(std::shared_ptr<ProtocolItem> item)
{
    auto tcp_item = std::dynamic_pointer_cast<CustomTcpProtocolItem>(item);
    if(!tcp_item)
    {
        PJ_F_ERROR("custom protocol item is nullptr! \n");
        return;
    }

    // 1. 配置的协议校验内容缓存
    // 注意这里的结构主要是配合数据库对账和快速索引的
    // 2. 功能码映射 区分到底是哪一条协议
    {
        std::lock_guard<std::mutex> lock(_mtx);
        _protocol_items.emplace(item->getId(), item);
        _func_codes[tcp_item->getReq()->functionCodeFieldValue()] = tcp_item;

    }

    PJSERVER_DEBUG() << "CustomTcpProjectServer::AddProtocolItem ok, " << tcp_item->getReq()->functionCodeFieldValue() << std::endl;
}

void CustomTcpProjectServer::DelProtocolItem(int64_t protocol_id)
{
    std::lock_guard<std::mutex> lock(_mtx);
    auto it = _protocol_items.find(protocol_id);

    if(it == _protocol_items.end()) 
    {
        PJ_F_ERROR("CustomTcpProjectServer: Protocol item not found, project_id[%d],protocol_id[%d] \n",  _project_id, protocol_id);
        return;
    } 
    auto tcp_item = std::dynamic_pointer_cast<CustomTcpProtocolItem>(it->second);

    assert(tcp_item);

    const std::string &func_code_str = tcp_item->getReq()->functionCodeFieldValue();

    auto it2 = _func_codes.find(func_code_str);
    if(it2 == _func_codes.end()) 
    {
        PJ_F_ERROR("CustomTcpProjectServer: func code not found, project_id[%d], protocol_id[%d], func_code[%s]\n",  _project_id, protocol_id, func_code_str.c_str());
        return;
    } 
    PJ_F_DEBUG("CustomTcpProjectServer: Deleted protocol item, pproject_id[%d], protocol_id[%d] \n",  _project_id, protocol_id);

    // 一并删除
    _protocol_items.erase(it);
    _func_codes.erase(it2);
}

std::shared_ptr<ProtocolItem> CustomTcpProjectServer::GetProtocolItem(int64_t protocol_id)
{
    std::lock_guard<std::mutex> lock(_mtx);
    auto it = _protocol_items.find(protocol_id);
    return it == _protocol_items.end() ? nullptr : it->second;
}

void CustomTcpProjectServer::UpdateReqCfgProtocolItem(int64_t protocol_id, const nljson& req_cfg_json)
{
    std::lock_guard<std::mutex> lock(_mtx);
    
    auto it = _protocol_items.find(protocol_id);
    if(it == _protocol_items.end())
        return;

    auto item = it->second;
    auto p = item->getOriProtocol();

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

    auto tcp_item  = std::dynamic_pointer_cast<CustomTcpProtocolItem>(item);

    assert(tcp_item);

    auto it2 = _func_codes.find(tcp_item->getReq()->functionCodeFieldValue());
    if(it2 == _func_codes.end())
        return;
    _func_codes.erase(it2);

    // 更新一下即可不需要删除
    try {
        // 由于是服务器服务 需要对功能码也进行更新
        p->m_reqCfg.get_to<CustomTcpMessage>(*tcp_item->getReq());
        _func_codes[tcp_item->getReq()->functionCodeFieldValue()] = tcp_item;

    } catch (const std::exception &e) {
        PJSERVER_F_ERROR("UpdateReqCfgProtocolItem fail, %s.  protocol_id[%d]  \n", e.what(), protocol_id);
        return;
    }


}

void CustomTcpProjectServer::UpdateRespCfgProtocolItem(int64_t protocol_id, const nljson& resp_cfg_json)
{
    std::lock_guard<std::mutex> lock(_mtx);
    
    auto it = _protocol_items.find(protocol_id);
    if(it == _protocol_items.end())
        return;
    // 获取原始协议对象后删除对象
    auto item = it->second;
    auto p = item->getOriProtocol();

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

    auto tcp_item  = std::dynamic_pointer_cast<CustomTcpProtocolItem>(item);

    assert(tcp_item);

    // 更新一下即可不需要删除
    try {

        // 注意这里不需要进行功能码更新
        p->m_respCfg.get_to<CustomTcpMessage>(*tcp_item->getResp());

    } catch (const std::exception &e) {
        PJSERVER_F_ERROR("UpdateReqCfgProtocolItem fail, %s.  protocol_id[%d]  \n", e.what(), protocol_id);
    }
}

void CustomTcpProjectServer::UpdateReqBodyProtocolItem(int64_t protocol_id, const ProtocolBodyType body_type, const std::vector<char>& req_body_data)
{
    std::lock_guard<std::mutex> lock(_mtx);
    auto it = _protocol_items.find(protocol_id);
    if(it == _protocol_items.end())
    {
        PJSERVER_F_ERROR("protocol_id[%d] not found! \n", protocol_id);
        return;
    }

    auto tcp_item  = std::dynamic_pointer_cast<CustomTcpProtocolItem>(it->second);

    assert(tcp_item);
    kit_muduo::http::Body body(kit_muduo::http::ContentType(static_cast<int32_t>(body_type)), req_body_data);

    tcp_item->getReq()->setBody(body);


}

void CustomTcpProjectServer::UpdateRespBodyProtocolItem(int64_t protocol_id, const ProtocolBodyType body_type, const std::vector<char>& resp_body_data)
{
    std::lock_guard<std::mutex> lock(_mtx);
    auto it = _protocol_items.find(protocol_id);
    if(it == _protocol_items.end())
    {
        PJSERVER_F_ERROR("protocol_id[%d] not found! \n", protocol_id);
        return;
    }

    auto tcp_item  = std::dynamic_pointer_cast<CustomTcpProtocolItem>(it->second);

    assert(tcp_item);
    kit_muduo::http::Body body(kit_muduo::http::ContentType(static_cast<int32_t>(body_type)), resp_body_data);

    tcp_item->getResp()->setBody(body);
}

void CustomTcpProjectServer::setPatternInfo(const std::shared_ptr<CustomTcpPattern> pattern)
{
    std::lock_guard<std::mutex> lock(_pattern_info_mtx);
    _pattern_info = pattern;
}

std::shared_ptr<CustomTcpPattern> CustomTcpProjectServer::getPatternInfo()
{
    std::lock_guard<std::mutex> lock(_pattern_info_mtx);
    return _pattern_info;
}



std::shared_ptr<CustomTcpProtocolItem> CustomTcpProjectServer::findByFuncCode(const std::string &func_code)
{
    std::lock_guard<std::mutex> lock(_mtx);
    auto it = _func_codes.find(func_code);
    return  it == _func_codes.end() ? nullptr : it->second;
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

    if(!context->parseRequest(*buf, receiveTime))
    {
        PJSERVER_ERROR() << "custom tcp request parse error! " << std::endl;

        // 出错一般直接关闭 不需要进行回发
        conn->shutdown();
    }

    if(context->gotAll())
    {
        auto req = context->request();
        // 重置conn中的上下文 清理实际的message对象
        context->reset(); 

        handleRequest(conn, req);
    }

}

void CustomTcpProjectServer::handleRequest(kit_muduo::TcpConnectionPtr conn, std::shared_ptr<CustomTcpMessage> req)
{
    // 把收到的二进制头部进行打印
    auto pattern = getPatternInfo();
    
    auto cfg_func_code_field = pattern->functionCodeField();
    
    auto func_code_field = req->getField(cfg_func_code_field->idx());
  
    if(!func_code_field)
    {
        CUSTOM_F_ERROR("getField not found! %d \n", cfg_func_code_field->idx());

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
