/**
 * @file project_server.cpp
 * @brief  测试服务实际服务器
 * @author ljk5
 * @version 1.0
 * @date 2025-08-26 15:43:53
 * @copyright Copyright (c) 2025 HIKRayin
 */
#include "domain/project_server.h"




namespace kit_domain {

ProjectServer::ProjectServer(int64_t project_id)
    :project_id_(project_id)
    ,loop_thread_(nullptr, std::string("pj" + std::to_string(project_id) + "loop"))
{ 

}

ProjectServer::~ProjectServer()
{
    stop();
}



}