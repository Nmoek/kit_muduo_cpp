/**
 * @file project_vo.cpp
 * @brief  测试服务 视图对象
 * @author ljk5
 * @version 1.0
 * @date 2025-07-24 20:06:50
 * @copyright Copyright (c) 2025 HIKRayin
 */
#include "web/project_vo.h"


namespace kit_domain {

static std::string CovertLengthPolicy(const Project &p)
{
    if(p.m_protocolType != ProtocolType::kCustomTcp || !p.m_patternInfo.is_object())
    {
        return "";
    }

    const auto iter = p.m_patternInfo.find("length_policy");
    if(iter == p.m_patternInfo.end() || !iter->is_string())
    {
        return "";
    }

    return iter->get<std::string>();
}

ProjectVo CovertProjectVo(const Project &p)
{
    return ProjectVo{
        p.m_id,
        p.m_name,
        p.m_mode,
        static_cast<int32_t>(p.m_protocolType),
        CovertLengthPolicy(p),
        p.m_listenPort,
        p.m_targetIp,
        p.m_userId,
        static_cast<int32_t>(p.m_status),
        static_cast<int32_t>(p.m_runtimeState),
        p.m_ctime.toUtcRfc3339(),
    };
}

std::vector<ProjectVo> CovertProjectVos(const std::vector<Project> &projects)
{
    std::vector<ProjectVo> res;
    for(const auto &p : projects)
        res.emplace_back(CovertProjectVo(p));
    return res;
}

}   // namespace kit_domain
