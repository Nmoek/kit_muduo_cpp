/**
 * @file protocol.h
 * @brief Dao层 数据结构
 * @author ljk5
 * @version 1.0
 * @date 2025-07-29 16:29:37
 * @copyright Copyright (c) 2025 HIKRayin
 */
#ifndef __KIT_DAOS_PROTOCOL_H__
#define __KIT_DAOS_PROTOCOL_H__

#include <string>
#include <vector>

namespace kit_dao
{

struct Protocol
{
    int64_t                 m_id;           // 主键id
    std::string             m_name;         // 测试协议名称
    int32_t                 m_type;         // 测试协议类型
    int64_t                 m_projectId;    // 所属测试服务Id
    std::string             m_runtimeKey;   // 运行态唯一键值, 用于判断当前加入的协议是否有重复
    int32_t                 m_status;       // 测试协议状态 0失效 1有效
    int32_t                 m_runtimeEnabled; // 协议项是否上线状态  0未上线 1已上线
    int32_t                 m_reqBodyType;  // 请求协议数据类型 
    int32_t                 m_respBodyType; // 响应协议数据类型
    int32_t                 m_reqBodyDataStatus; // body是否存在 目的不去对真正的Body数据进行查询
    int32_t                 m_respBodyDataStatus; // body是否存在
    std::string             m_reqCfg;       // 请求协议配置数据
    std::string             m_respCfg;      // 响应协议配置数据
    std::vector<char>       m_reqBodyData;  // 请求协议配置Body
    std::vector<char>       m_respBodyData; // 响应协议配置Body
    int32_t                 m_isEndian;        //是否大小端转换 1转换 0不转换
    
    int64_t                  m_ctime;            // 创建时间
    int64_t                  m_utime;            // 修改时间
};

struct ProtocolAccessInfo
{
    int64_t                 protocol_id;              // 协议项Id
    int64_t                 project_id;               // 测试服务Id
    std::string             runtime_key;              // 协议项运行唯一键
    int32_t                 protocol_type;            // 测试协议类型
    int32_t                 protocol_status;          // 协议项是否有效
    int32_t                 protocol_runtime_enabled; // 协议项是否上线状态
    int64_t                 project_user_id;                  // 所属用户Id
    int32_t                 project_runtime_state;
    int32_t                 project_status;           // 测试服务是否有效
};


} // namespace kit_dao
#endif  //__KIT_DAOS_PROTOCOL_H__