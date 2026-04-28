/**
 * @file project_server_factory.h
 * @brief ProjectServer工厂模式实现 - 工厂方法模式
 * @author ljk5
 * @version 2.0
 * @date 2025-10-28
 * @copyright Copyright (c) 2025 HIKRayin
 */
#ifndef __KIT_DOMAIN_PROJECT_SERVER_FACTORY_H__
#define __KIT_DOMAIN_PROJECT_SERVER_FACTORY_H__

#include "domain/project_server.h"
#include "domain/type.h"
#include "domain/project.h"

#include <memory>
#include <unordered_map>
#include <mutex>

namespace kit_muduo {
class EventLoop;
class InetAddress;
}

namespace kit_domain {

/**
 * @brief ProjectServer创建器抽象基类
 * 采用工厂方法模式，每个具体的创建器负责创建特定类型的ProjectServer
 */
class ProjectServerCreator {
public:
    virtual ~ProjectServerCreator() = default;
    
    /**
     * @brief  创建ProjectServer实例
     * @param p Project实体
     * @return std::shared_ptr<ProjectServer> 
     */
    virtual std::shared_ptr<ProjectServer> create(const kit_domain::Project &p) = 0;
    
    /**
     * @brief 获取创建器支持的协议类型
     * @return 协议类型
     */
    virtual ProtocolType getProtocolType() const = 0;
};

/**
 * @brief HTTP ProjectServer创建器
 */
class HttpProjectServerCreator : public ProjectServerCreator {
public:
    std::shared_ptr<ProjectServer> create(const kit_domain::Project &p) override;
    
    ProtocolType getProtocolType() const override { return ProtocolType::HTTP_PROTOCOL; }
};

/**
 * @brief HTTPS ProjectServer创建器
 */
class HttpsProjectServerCreator : public ProjectServerCreator {
public:
    std::shared_ptr<ProjectServer> create(const kit_domain::Project &pe) override;
    
    ProtocolType getProtocolType() const override { return ProtocolType::HTTPS_PROTOCOL; }
};

/**
 * @brief TCP ProjectServer创建器
 */
class TcpProjectServerCreator : public ProjectServerCreator {
public:
    std::shared_ptr<ProjectServer> create(const kit_domain::Project &p) override;
    
    ProtocolType getProtocolType() const override { return ProtocolType::CUSTOM_TCP_PROTOCOL; }
};

/**
 * @brief ProjectServer工厂管理器
 * 采用工厂方法模式，管理所有ProjectServer创建器
 */
class ProjectServerFactoryManager 
{
public:
    /**
     * @brief ProjectServer工厂管理器单例
     * @return ProjectServerFactoryManager& 
     */
    static ProjectServerFactoryManager& getInstance();
    
    /**
     * @brief 注册创建器
     * @param creator 创建器指针
     */
    void registerCreator(std::unique_ptr<ProjectServerCreator> creator);
    
    /**
     * @brief 根据Project实体创建ProjectServer
     * @param p Project实体
     * @return std::shared_ptr<ProjectServer> 
     */
    std::shared_ptr<ProjectServer> createServer(const kit_domain::Project &p);
        
    /**
     * @brief 注册自定义创建器
     * @tparam CreatorType 创建器类型
     */
    template<typename CreatorType>
    void registerCreator() {
        registerCreator(std::make_unique<CreatorType>());
    }
    
    /**
     * @brief 检查是否支持指定的协议类型
     * @param protocol_type 协议类型
     * @return 是否支持
     */
    bool isProtocolSupported(ProtocolType protocol_type) const;
    
    /**
     * @brief 获取所有支持的协议类型
     * @return 协议类型列表
     */
    std::vector<ProtocolType> getSupportedProtocols() const;

private:
    ProjectServerFactoryManager();
    void initializeDefaultCreators();

private:
    std::unordered_map<ProtocolType, std::unique_ptr<ProjectServerCreator>> creators_;
    mutable std::mutex mutex_;
};

/**
 * @brief 简化的工厂接口
 */
class ProjectServerFactory {
public:
    /**
     * @brief 根据协议类型创建对应的ProjectServer实例
     * @param protocol_type 协议类型
     * @param project_id 项目ID
     * @param loop 事件循环
     * @param address 监听地址
     * @param server_name 服务器名称
     * @return 返回ProjectServer的shared_ptr
     */
    static std::shared_ptr<ProjectServer> Create(const kit_domain::Project &p) 
    {
        return ProjectServerFactoryManager::getInstance().createServer(p);
    }
};

} // namespace kit_domain

#endif // __KIT_DOMAIN_PROJECT_SERVER_FACTORY_H__
