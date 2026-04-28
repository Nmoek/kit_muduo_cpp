/**
 * @file protocol.h
 * @brief 对应web 测试协议项
 * @author ljk5
 * @version 1.0
 * @date 2025-07-17 11:34:44
 * @copyright Copyright (c) 2025 HIKRayin
 */
#ifndef __KIT_DOMAIN_PROTOCOL_H__
#define __KIT_DOMAIN_PROTOCOL_H__

#include <cstdint>
#include <string>
#include <vector>

#include "base/time_stamp.h"
#include "domain/type.h"
#include "net/http/http_request.h"
#include "net/http/http_response.h"


namespace kit_domain
{


/**
 * @brief 测试协议项
 */
struct Protocol
{
    int64_t                 m_id;
    std::string             m_name;         // 测试协议名称
    ProtocolType            m_type;         // 测试协议类型
    int64_t                 m_projectId;    // 所属测试服务Id
    ProtocolStatus          m_status;       // 协议项是否有效
    ProtocolBodyType        m_reqBodyType;  // 请求协议数据类型
    ProtocolBodyType        m_respBodyType; // 响应协议数据类型
    int32_t                 m_reqBodyDataStatus; // body是否存在
    int32_t                 m_respBodyDataStatus; // body是否存在
    nlohmann::json          m_reqCfg;       // 请求协议数据
    nlohmann::json          m_respCfg;      // 响应协议数据
    std::vector<char>       m_reqBodyData;  // 请求协议数据
    std::vector<char>       m_respBodyData; // 响应协议数据
    bool                    m_isEndian;     // 是否大小端转换

    kit_muduo::TimeStamp    m_ctime;        // 创建时间
    kit_muduo::TimeStamp    m_utime;        // 更新时间
};




} // namespace kit_domain
#endif // __KIT_DOMAIN_PROTOCOL_H__