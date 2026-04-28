/**
 * @file project_vo.h
 * @brief  测试服务 视图对象
 * @author ljk5
 * @version 1.0
 * @date 2025-07-24 19:54:24
 * @copyright Copyright (c) 2025 HIKRayin
 */
#ifndef __KIT_PROJECT_VO_H__
#define __KIT_PROJECT_VO_H__

#include "base/time_stamp.h"
#include "nlohmann/json.hpp"
#include "domain/project.h"

#include <string>
#include <vector>

namespace kit_domain {


struct ProjectVo {
    int64_t                  id;               // 主键id
    std::string              name;             // 测试名称
    int32_t                  mode;             // 测试模式
    int32_t                  protocol_type;    // 协议种类
    uint16_t                 listen_port;      // 监听端口号
    std::string              target_ip;        // 目标ip + 端口 x.x.x.x:8888
    int64_t                  user_id;          // 所属用户id
    int32_t                  status;           // 当前服务状态 1开启 0关闭
    int32_t                  pattern_type;     // 格式类型

    std::string              ctime;           // 创建时间

    NLOHMANN_DEFINE_TYPE_INTRUSIVE(ProjectVo, id, name, mode, protocol_type, listen_port, target_ip, user_id, status, pattern_type, ctime)
};

ProjectVo CovertProjectVo(const Project &p);

std::vector<ProjectVo> CovertProjectVos(const std::vector<Project> &projects);

} // namespace kit_domain
#endif // __KIT_VO_PROJECT_H__