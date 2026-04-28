/**
 * @file project.h
 * @brief  测试服务 Entity领域对象
 * @author ljk5
 * @version 1.0
 * @date 2025-07-17 10:36:18
 * @copyright Copyright (c) 2025 HIKRayin
 */
#ifndef __KIT_DOMAIN_PROJECT_H__
#define __KIT_DOMAIN_PROJECT_H__

#include <cstdint>
#include <string>
#include <vector>

#include "base/time_stamp.h"
#include "domain/type.h"

namespace kit_domain
{


/**
 * @brief 测试服务
 */
struct Project
{
    int64_t                  m_id;
    std::string              m_name;            // 测试名称
    ProjectMode              m_mode;            // 测试模式
    ProtocolType             m_protocolType;    // 协议种类
    uint16_t                 m_listenPort;      // 监听端口号
    std::string              m_targetIp;        // 目标ip + 端口 x.x.x.x:8888
    int64_t                  m_userId;          // 所属用户id
    ProjectStatus            m_status;          // 当前服务状态
    CustomTcpPatternType     m_patternType;     // 协议格式类型
    std::vector<char>        m_patternInfo;     //解析格式信息

    kit_muduo::TimeStamp     m_ctime;           // 创建时间

    bool operator==(const Project& other) const
    {
        return m_id == other.m_id &&
               m_name == other.m_name &&
               m_mode == other.m_mode &&
               m_listenPort == other.m_listenPort &&
               m_targetIp == other.m_targetIp;
    }
};

} //  kit_domain
#endif //__KIT_DOMAIN_PROJECT_H__