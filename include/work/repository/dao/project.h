/**
 * @file project.h
 * @brief Dao层 数据结构
 * @author ljk5
 * @version 1.0
 * @date 2025-07-23 10:54:14
 * @copyright Copyright (c) 2025 HIKRayin
 */
#ifndef __KIT_DAOS_PROJECT_H__
#define __KIT_DAOS_PROJECT_H__

#include <string>
#include <vector>

namespace kit_dao
{

struct Project
{
    int64_t                  m_id;
    std::string              m_name;            // 测试名称
    int32_t                  m_mode;            // 测试模式
    int32_t                  m_protocolType;    // 协议种类
    uint16_t                 m_listenPort;      // 监听端口号
    std::string              m_targetIp;        // 目标ip + 端口 x.x.x.x:8888
    int64_t                  m_userId;          // 所属用户id
    int32_t                  m_status;          // 测试服务当前状态
    int32_t                  m_patternType;     // 协议格式类型
    std::vector<char>        m_patternInfo;     // 格式解析信息

    int64_t                  m_ctime;            // 创建时间
    int64_t                  m_utime;            // 修改时间
};
    


} // namespace kit_dao
#endif  //__KIT_DAOS_PROJECT_H__