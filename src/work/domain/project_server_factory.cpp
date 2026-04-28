/**
 * @file project_server_factory.cpp
 * @brief ProjectServer工厂模式实现 - 工厂方法模式
 * @author ljk5
 * @version 2.0
 * @date 2025-10-28
 * @copyright Copyright (c) 2025 HIKRayin
 */
#include "web/web_log.h"
#include "domain/project_server_factory.h"
#include "domain/project_server.h"
#include "net/http/http_server.h"
#include "base/event_loop_thread.h"
#include "domain/custom_tcp_pattern.h"

#include <vector>
#include <algorithm>
#include <mutex>

using namespace kit_muduo;

namespace kit_domain {

// HttpProjectServerCreator实现
std::shared_ptr<ProjectServer> HttpProjectServerCreator::create(const kit_domain::Project &p) 
{
    // 本地模式下 需要单开线程开启一个新server
    // TODO 分布式模式下使用RPC通知目标服务器开启服务
    EventLoopThread t(nullptr, std::to_string(p.m_id) + "http_loop");
    EventLoop * loop = t.startLoop();

    // 默认绑定到eth0 IP上
    const std::string& local_ip = InetAddress::GetInterfaceIpv4("eth0").toIp();

    const InetAddress& address = InetAddress(p.m_listenPort, local_ip);

    PJ_F_INFO("Creating HttpProjectServer, project_id[%d] address[%s]\n", p.m_id, address.toIpPort().c_str());
    
    try {
        std::string server_name = "pj";
        server_name += std::to_string(p.m_id);
        server_name += "_http_server";

        auto http_server = std::make_shared<http::HttpServer>(
            loop, address, server_name, false, kit_muduo::TcpServer::KReusePort
        );
        http_server->setThreadNum(0); // 使用单线程模式
        http_server->start();

        return std::make_shared<HttpProjectServer>(p.m_id, http_server);
    } catch (const std::exception& e) {
        PJ_ERROR() << "Failed to create HttpProjectServer: " << e.what() << std::endl;
        return nullptr;
    }
}

// HttpsProjectServerCreator实现
std::shared_ptr<ProjectServer> HttpsProjectServerCreator::create(const kit_domain::Project &p) {
    
    PJ_INFO() << "Creating HttpsProjectServer, project_id=" << p.m_id  << std::endl;
    
    // TODO: 实现HTTPS服务器创建逻辑
    PJ_WARN() << "HttpsProjectServer not implemented yet, using HttpProjectServer" << std::endl;
    return nullptr;
}

// TcpProjectServerCreator实现
std::shared_ptr<ProjectServer> TcpProjectServerCreator::create(const kit_domain::Project &p) 
{
    
    // 本地模式下 需要单开线程开启一个新server
    // TODO 分布式模式下使用RPC通知目标服务器开启服务
    EventLoopThread t(nullptr, std::to_string(p.m_id) + "tcp_loop");
    EventLoop * loop = t.startLoop();

    // 默认绑定到eth0 IP上
    const std::string& local_ip = InetAddress::GetInterfaceIpv4("eth0").toIp();

    const InetAddress& address = InetAddress(p.m_listenPort, local_ip);

    PJ_F_INFO("Creating TcpProjectServer, project_id[%d] address[%s]\n", p.m_id, address.toIpPort().c_str());
    
    try {
        std::string server_name = "pj";
        server_name += std::to_string(p.m_id);
        server_name += "_tcp_server";

        auto tcp_server = std::make_shared<TcpServer>(
            loop, address, server_name, TcpServer::KReusePort
        );
        tcp_server->setThreadNum(0); // 使用单线程模式
        tcp_server->start();

        PJ_ERROR() << "m_patternType: " << (int32_t)p.m_patternType << ", " << nlohmann::json::parse(p.m_patternInfo).dump(4) << std::endl;
        
        // 创建自定义TCP服务器 必须带解析格式  否则无法解析
        return std::make_shared<CustomTcpProjectServer>(p.m_id, tcp_server, p.m_patternType, p.m_patternInfo);
        
    } catch (const std::exception& e) {
        PJ_ERROR() << "Failed to create CustomTcpProjectServer: " << std::endl << e.what() << std::endl;
        return nullptr;
    }
}

// ProjectServerFactoryManager实现
ProjectServerFactoryManager& ProjectServerFactoryManager::getInstance() {
    static ProjectServerFactoryManager instance;
    return instance;
}

ProjectServerFactoryManager::ProjectServerFactoryManager() {
    initializeDefaultCreators();
}

void ProjectServerFactoryManager::initializeDefaultCreators() {
    PJ_DEBUG() << "Initializing default ProjectServer creators" << std::endl;
    
    // 注册默认的创建器
    registerCreator<HttpProjectServerCreator>();    // HTTP测试服务器
    registerCreator<HttpsProjectServerCreator>();   // HTTPS测试服务器
    registerCreator<TcpProjectServerCreator>();     // 自定义TCP测试服务器
    
    PJ_INFO() << "Default ProjectServer creators initialized successfully" << std::endl;
}

void ProjectServerFactoryManager::registerCreator(std::unique_ptr<ProjectServerCreator> creator) {

    if (!creator) {
        PJ_ERROR() << "Attempted to register null creator" << std::endl;
        return;
    }
    
    std::lock_guard<std::mutex> lock(mutex_);
    
    ProtocolType protocol_type = creator->getProtocolType();
    auto result = creators_.emplace(protocol_type, std::move(creator));
    
    if (result.second) {
        PJ_DEBUG() << "Registered creator for protocol type: " << static_cast<int32_t>(protocol_type) << std::endl;
    } else {
        PJ_WARN() << "Creator for protocol type " << static_cast<int32_t>(protocol_type) 
                  << " already exists, replacing with new creator" << std::endl;
        creators_[protocol_type] = std::move(creator);
    }
}

std::shared_ptr<ProjectServer> ProjectServerFactoryManager::createServer(const kit_domain::Project &p) 
{

    // 这个锁是否有存在必要?
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = creators_.find(p.m_protocolType);
    if (it == creators_.end()) {
        PJ_ERROR() << "Unsupported protocol type: " << static_cast<int32_t>(p.m_protocolType) << std::endl;
        return nullptr;
    }
    
    PJ_DEBUG() << "Creating ProjectServer with protocol type: " << static_cast<int32_t>(p.m_protocolType)
               << ", project_id: " << p.m_id << std::endl;
    
    try {

        return it->second->create(p);

    } catch (const std::exception& e) {
        PJ_ERROR() << "Failed to create ProjectServer for protocol type " 
                   << static_cast<int32_t>(p.m_protocolType) << ": " << e.what() << std::endl;
        return nullptr;
    }
}

bool ProjectServerFactoryManager::isProtocolSupported(ProtocolType protocol_type) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return creators_.find(protocol_type) != creators_.end();
}

std::vector<ProtocolType> ProjectServerFactoryManager::getSupportedProtocols() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<ProtocolType> protocols;
    
    for (const auto& pair : creators_) {
        protocols.push_back(pair.first);
    }
    
    return protocols;
}



} // namespace kit_domain
