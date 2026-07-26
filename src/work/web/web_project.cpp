/**
 * @file web_project.cpp
 * @brief 测试服务 web层接口
 * @author ljk5
 * @version 1.0
 * @date 2025-07-17 16:53:47
 * @copyright Copyright (c) 2025 HIKRayin
 */
#include "web/web_project.h"
#include "domain/type.h"
#include "net/http/http_util.h"
#include "web/write_response.h"
#include "work/service/svc_project.h"
#include "domain/project.h"
#include "net/http/http_server.h"
#include "net/http/http_response.h"
#include "net/http/http_request.h"
#include "net/http/http_context.h"
#include "web/web_common.h"
#include "web/web_log.h"
#include "nlohmann/json.hpp"
#include "web/project_vo.h"
#include "base/event_loop_thread.h"
#include "application.h"
#include "domain/project_server.h"
#include "domain/project_server_factory.h"
#include "domain/custom_tcp_pattern.h"
#include "domain/user.h"
#include "domain/protocol.h"
#include "service/svc_protocol.h"
#include "runtime/runtime_controller.h"

#include <memory>
#include <cstring>
#include <stdexcept>

using namespace kit_muduo;
using namespace kit_muduo::http;
using namespace kit_domain;
using nljson = nlohmann::json;

namespace kit_domain {

/***************Body解析临时变量定义 其他模块不允许引用**************** */

struct CustomPatternFieldReq {
    std::string name;
    int32_t len;
    std::string attr;

    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(CustomPatternFieldReq, name, len, attr)
};

struct CustomPatternMagicNumReq {
    int32_t      pos;
    std::string  value;

    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(CustomPatternMagicNumReq, pos, value)
};


struct AddProjectReq {
    std::string              name;             // 测试名称
    ProjectMode              mode;             // 测试模式
    ProtocolType                  protocol_type;    // 协议种类 1 2 3
    // uint16_t                 listen_port;      // 监听端口号(弃用 不再由用户指定)
    std::string              target_ip;        // 目标ip + 端口 x.x.x.x:8888
    nljson                   pattern_info;  // 解析格式信息

    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(AddProjectReq, name, mode, protocol_type, target_ip, pattern_info)
};


struct StartAndStopProjectReq
{
    int64_t id;
    int32_t operation; // 1 start 0 stop

    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(StartAndStopProjectReq, operation)
};

/**
 * @brief List 用于Body解析
 */
struct ProjectListReq {
    int32_t                  offset;          // 页码
    int32_t                  limit;           // 页大小

    NLOHMANN_DEFINE_TYPE_INTRUSIVE(ProjectListReq, offset, limit)
};

struct ProjectDetailNameReq {

    std::string name;
    NLOHMANN_DEFINE_TYPE_INTRUSIVE(ProjectDetailNameReq, name)
};

struct ProjectEditPatternInfoReq {
    int64_t     id;            // project id
    nljson      pattern_info;  // 格式内容
    NLOHMANN_DEFINE_TYPE_INTRUSIVE(ProjectEditPatternInfoReq, id, pattern_info)
};


/***************Body解析临时变量定义 其他模块不允许引用**************** */

ProjectHandler::ProjectHandler(std::shared_ptr<ProjectSvcInterface> svc,
    std::shared_ptr<ProtocolSvcInterface> pc_svc,
    std::shared_ptr<RuntimeControllerInterface> project_runtime_manager)
    :svc_(std::move(svc))
    ,pc_svc_(std::move(pc_svc))
    ,project_runtime_manager_(std::move(project_runtime_manager))
{

}


void ProjectHandler::RegisterRoutes(std::shared_ptr<kit_muduo::http::HttpServer> server)
{
#define XX(WORK_FUNC) \
    std::bind(&ProjectHandler::WORK_FUNC, this, std::placeholders::_1, std::placeholders::_2)

    // 新增测试服务
    server->Post("/projects/add", XX(AddProject));

    // 开启/停止测试服务监听端口
    server->Post("/projects/:project_id/runtime_state", XX(StartAndStopProject));

    // 获取所有未软删除的测试服务
    server->Get("/projects/valid", XX(GetAllValid));

    server->Get("/projects/:project_id", XX(SingleProject));
    server->Delete("/projects/:project_id", XX(DelProject));
    server->Post("/projects/:project_id/restore", XX(RestoreProject));

    // 获取/修改某个服务的title名称
    server->Post("/projects/:project_id/name", XX(DetailName));

    // 获取整个测试服务列表
    server->Post("/projects/list", XX(List));

    server->Get("/projects/:project_id/pattern_info", XX(QueryPatternInfo));
    server->Post("/projects/pattern_info", XX(EditPatternInfo));

#undef XX
}

static bool CheckProjectInfo(const AddProjectReq &request)
{

    if(ProjectMode::ClientMode == request.mode)
    {
        const std::string & target_ip = request.target_ip;
        size_t pos = target_ip.find(":");
        if(pos != std::string::npos)
        {
            uint16_t port = ::atoi(target_ip.substr(pos + 1).c_str());
            if(port <= 0 || port > 65535)
            {
                PJ_F_ERROR("client mode target ip invalid\n");
                return false;
            }
        }
        else
        {
            PJ_F_ERROR("client mode target ip invalid\n");
            return false;
        }
    }

    if(request.protocol_type <= ProtocolType::kUnknown ||
       request.protocol_type >= ProtocolType::kMax)
    {
        PJ_F_ERROR("protocol type invalid\n");
        return false;
    }

    if(static_cast<ProtocolType>(request.protocol_type) == ProtocolType::kCustomTcp
        && !CustomTcpPatternSpec::FromJson(request.pattern_info).has_value())
    {
        PJ_F_ERROR("custom tcp pattern_info invalid\n");
        return false;
    }

    return true;
}

void ProjectHandler::AddProject(kit_muduo::TcpConnectionPtr conn, kit_muduo::HttpContextPtr ctx) noexcept
{
    auto req = ctx->request();
    auto resp = ctx->response();
    resp->setVersion(Version::kHttp11);
    resp->setStateCode(StateCode::k200Ok);
    resp->setContentMeta(MakeContentMeta(KnownMediaType::kApplicationJson));

    WriteOpResult write_result;
    AddProjectReq request;

    auto bind_result = ctx->bindJson(request);
    if(!bind_result.ok)
    {
        PJ_F_ERROR("body bind error: %s\n", bind_result.message.c_str());

        WriteOpResponseHelper(ctx, write_result.failed(-200, "body parse error"));
        return;
    }

    if(!CheckProjectInfo(request))
    {
        WriteOpResponseHelper(ctx, write_result.allErr().failed(-200, "project info invalid"));
        return;
    }

    // 用户权限校验
    auto current_user = CurrentUserFromContext(ctx);
    if(current_user.user_id <= 0)
    {
        WriteForbidden(ctx);
        return;
    }

    // DTO转换 避免对外暴露领域模型Entity
    kit_domain::Project p;
    p.m_id = -1;
    p.m_name = std::move(request.name);
    p.m_mode = request.mode;
    p.m_protocolType = request.protocol_type;
    p.m_listenPort = 0;
    p.m_targetIp =  std::move(request.target_ip);
    p.m_userId = current_user.user_id;
    p.m_status = ProjectStatus::kValid;
    p.m_runtimeState = ProjectRuntimeState::kStopped;
    p.m_patternInfo = std::move(request.pattern_info);

    int project_id = -1;
    try {
        project_id = svc_->Add(ctx, p);
        if(project_id <= 0)
        {
            throw std::runtime_error("project add error");
        }
    } catch(const std::exception& e) {

        PJ_F_ERROR("service add exception: %s \n", e.what());

        WriteOpResponseHelper(ctx, write_result.failed(-300, "service failed"));
        return;

    }

    // 2. 需要和开启的服务进行通信（通信方式如何选择?)，需要进行增删改协议项
    // 2.1 线程通信  复用loop队列
    // 2.2 RPC通信
    // 2.3 注册Web API

    WriteOpResponseHelper(ctx, write_result.persistedOk().success(), [project_id](auto &root){
        root["data"]["project_id"] = project_id;
    });
}

void ProjectHandler::StartAndStopProject(kit_muduo::TcpConnectionPtr conn, kit_muduo::HttpContextPtr ctx) noexcept
{
    auto req = ctx->request();
    auto resp = ctx->response();
    resp->setVersion(Version::kHttp11);
    resp->setStateCode(StateCode::k200Ok);
    resp->setContentMeta(MakeContentMeta(KnownMediaType::kApplicationJson));

    WriteOpResult write_result;

    int64_t project_id = 0;
    int32_t tmp_state;


    if(!ParseRouteArithmetic(ctx, "project_id", project_id)
        || !ParseQueryArithmetic(ctx, "operation", tmp_state))
    {
        PJ_F_ERROR("route dynamic param error\n");

        WriteOpResponseHelper(ctx, write_result.allErr().failed(-200, "route dynamic param error"));
        return;
    }
    const ProjectRuntimeState will_state = static_cast<ProjectRuntimeState>(tmp_state);

    if(project_id <= 0
        || (ProjectRuntimeState::kRunning != will_state && ProjectRuntimeState::kStopped != will_state))
    {
        WriteOpResponseHelper(ctx, write_result.allErr().failed(-200, "request param invalid"));
        return;
    }

    if(!CheckProjectAccess(ctx, svc_.get(), project_id, false, false))
    {
        WriteForbidden(ctx);
        return;
    }

    ProjectRuntimeResult pj_runtime_result;
    try {

        if(ProjectRuntimeState::kRunning == will_state)
        {
            pj_runtime_result = project_runtime_manager_->startProject(ctx, project_id);

        }
        else if(ProjectRuntimeState::kStopped == will_state)
        {
            pj_runtime_result = project_runtime_manager_->stopProject(ctx, project_id);
        }

    } catch(const std::exception& e) {

        PJ_F_ERROR("project server runtime exception: %s \n", e.what());

        WriteOpResponseHelper(ctx, write_result.failed(-300, "service failed"));
        return;
    }


    // 2. 需要和开启的服务进行通信（通信方式如何选择?)，需要进行增删改协议项
    // 2.1 线程通信  复用loop队列
    // 2.2 RPC通信
    // 2.3 注册Web API

    WriteOpResponseHelper(ctx, WriteOpResult::FromPjRuntimeResult(pj_runtime_result), [&pj_runtime_result](auto &root){
        root["data"]["listen_port"] = pj_runtime_result.snapshot.listen_port;
        root["data"]["runtime_state"] = pj_runtime_result.snapshot.runtime_state;
    });
}

void ProjectHandler::DelProject(kit_muduo::TcpConnectionPtr conn, kit_muduo::HttpContextPtr ctx) noexcept
{
    auto req = ctx->request();
    auto resp = ctx->response();
    resp->setVersion(Version::kHttp11);
    resp->setStateCode(StateCode::k200Ok);

    WriteOpResult write_result;

    // PJ_DEBUG() << "ProjectHandler::DelProject " << std::endl << req->bodyString() << std::endl;

    // 获取测试服务主键id
    int64_t project_id = 0;
    if(!ParseRouteArithmetic(ctx, "project_id", project_id))
    {
        PJ_F_ERROR("route dynamic param error! \n");

        WriteOpResponseHelper(ctx, write_result.allErr().failed(-200, "route dynamic param error"));
        return;
    }

    if(!CheckProjectAccess(ctx, svc_.get(), project_id, false, false))
    {
        WriteForbidden(ctx);
        return;
    }

    // 查测试服务 信息
    ProjectRuntimeResult pj_runtime_result;
    try
    {

        pj_runtime_result = project_runtime_manager_->delProject(ctx, project_id);

    }
    catch(const std::exception& e)
    {
        PJ_F_ERROR("service GetById exception: %s \n", e.what());

        WriteOpResponseHelper(ctx, write_result.failed(-300, "service failed"));
        return;
    }

    WriteOpResponseHelper(ctx, WriteOpResult::FromPjRuntimeResult(pj_runtime_result));
}

void ProjectHandler::SingleProject(kit_muduo::TcpConnectionPtr conn, kit_muduo::HttpContextPtr ctx) noexcept
{
    auto req = ctx->request();
    auto resp = ctx->response();
    resp->setVersion(Version::kHttp11);
    resp->setStateCode(StateCode::k200Ok);

    int64_t project_id = 0;
    if(!ParseRouteArithmetic(ctx, "project_id", project_id))
    {
        PJ_F_ERROR("route dynamic param error! \n");

        WriteJsonError(ctx, -200, "route dynamic param error");
        return;
    }

    Project project;
    // 查测试服务 信息
    try
    {
        project = svc_->GetById(ctx, project_id);
        if(project.m_id <= 0)
        {
            WriteJsonResponse(ctx, {
                {"code", 0}, 
                {"message", "project is not exists!"}, {"data", nljson::array()}
            });

            return;
        }
        auto current_user = CurrentUserFromContext(ctx);
        if(project.m_id > 0 && !current_user.IsAdmin() && project.m_userId != current_user.user_id)
        {
            WriteForbidden(ctx);
            return;
        }
    }
    catch(const std::exception& e)
    {
        PJ_F_ERROR("service GetById exception: %s \n", e.what());

        WriteJsonError(ctx, -300, "service failed");

        return;
    }

    nljson root;
    root["code"] = 0;
    root["message"] = "success";
    root["data"].push_back(CovertProjectVo(project));

    WriteJsonResponse(ctx, root);
    PJ_DEBUG() << std::endl << root.dump(4) << std::endl;
}

void ProjectHandler::List(kit_muduo::TcpConnectionPtr conn, kit_muduo::HttpContextPtr ctx) noexcept
{
    auto req = ctx->request();
    auto resp = ctx->response();
    resp->setVersion(Version::kHttp11);
    resp->setStateCode(StateCode::k200Ok);
    ProjectListReq request;

    PJ_DEBUG() << std::endl << req->bodyString() << std::endl;

    auto bind_result = ctx->bindJson(request);
    if(!bind_result.ok)
    {
        PJ_F_ERROR("body bind error: %s\n", bind_result.message.c_str());
        WriteJsonError(ctx, -200, "body parse error");
        return;
    }


    std::vector<Project> projects;
    // 查测试服务 信息
    try
    {
        auto current_user = CurrentUserFromContext(ctx);
        if(current_user.IsAdmin())
        {
            projects = svc_->GetAll(ctx, request.offset, request.limit);
        }
        else
        {
            projects = svc_->GetByUser(ctx, current_user.user_id, ProjectStatus::kValid, request.offset, request.limit);
        }
    }
    catch(const std::exception& e)
    {
        PJ_F_ERROR("service GetByUser exception: %s \n", e.what());
        WriteJsonError(ctx, -300, "service failed");

        return;
    }


    nljson root;
    root["code"] = 0;
    root["message"] = "success";
    root["data"] = nljson::array();
    for(const auto& p : projects)
    {
        //VO转换
        nljson node = CovertProjectVo(p);
        root["data"].push_back(node);
    }
    WriteJsonResponse(ctx, root);

    PJ_DEBUG() << std::endl << root.dump(4) << std::endl;
}

void ProjectHandler::GetAllValid(kit_muduo::TcpConnectionPtr conn, kit_muduo::HttpContextPtr ctx) noexcept
{
    auto req = ctx->request();
    auto resp = ctx->response();
    resp->setVersion(Version::kHttp11);
    resp->setStateCode(StateCode::k200Ok);
    resp->setContentMeta(MakeContentMeta(KnownMediaType::kApplicationJson));

    PJ_DEBUG() << std::endl << req->bodyString() << std::endl;

    std::vector<Project> projects;
    try
    {
        auto current_user = CurrentUserFromContext(ctx);
        if(current_user.IsAdmin())
        {
            projects = svc_->GetAllValid(ctx);
        }
        else
        {
            projects = svc_->GetByUser(ctx, current_user.user_id, ProjectStatus::kValid, 0, 1000);
        }
    }
    catch(const std::exception& e)
    {
        PJ_F_ERROR("service GetAllValid exception: %s \n", e.what());
        WriteJsonError(ctx, -300, "service failed");
        return;
    }

    nljson root;
    root["code"] = 0;
    root["message"] = "success";
    root["data"] = nljson::array();
    for(const auto& p : projects)
    {
        root["data"].push_back(CovertProjectVo(p));
    }

    WriteJsonResponse(ctx, root);

    PJ_DEBUG() << std::endl << root.dump(4) << std::endl;
}

void ProjectHandler::DetailName(kit_muduo::TcpConnectionPtr conn, kit_muduo::HttpContextPtr ctx) noexcept
{
    auto req = ctx->request();
    auto resp = ctx->response();
    resp->setVersion(Version::kHttp11);
    resp->setStateCode(StateCode::k200Ok);
    resp->setContentMeta(MakeContentMeta(KnownMediaType::kApplicationJson));

    ProjectDetailNameReq request;
    WriteOpResult write_result;

    PC_DEBUG() << std::endl << req->bodyString() << std::endl;

    int64_t project_id = 0;
    if(!ParseRouteArithmetic(ctx, "project_id", project_id))
    {
        PJ_F_ERROR("route dynamic param error! \n");

        WriteOpResponseHelper(ctx, write_result.failed(-200, "query param  fail"));
        return;
    }

    if(!CheckProjectAccess(ctx, svc_.get(), project_id, false, false))
    {
        WriteForbidden(ctx);
        return;
    }

    auto bind_result = ctx->bindJson(request);
    if(!bind_result.ok)
    {
        PC_F_ERROR("body bind error: %s\n", bind_result.message.c_str());

        WriteOpResponseHelper(ctx, write_result.failed(-200, "body parse error"));
        return;
    }

    try {

        bool ok = svc_->UpdateName(ctx, project_id, request.name);
        if(!ok)
        {
            throw std::runtime_error("UpdateName error");
        }
    } catch(const std::exception& e) {

        PC_F_ERROR("service UpdateName exception: %s \n", e.what());

        WriteOpResponseHelper(ctx, write_result.failed(-300, "service failed"));
        return;
    }

    WriteOpResponseHelper(ctx, write_result.persistedOk().success());
}



void ProjectHandler::QueryPatternInfo(kit_muduo::TcpConnectionPtr conn, kit_muduo::HttpContextPtr ctx) noexcept
{
    auto req = ctx->request();
    auto resp = ctx->response();
    resp->setVersion(Version::kHttp11);
    resp->setStateCode(StateCode::k200Ok);
    resp->setContentMeta(MakeContentMeta(KnownMediaType::kApplicationJson));


    int64_t project_id = 0;
    if(!ParseRouteArithmetic(ctx, "project_id", project_id))
    {
        PJ_F_ERROR("route dynamic param error! \n");

        WriteJsonError(ctx, -200, "route dynamic param error");
        return;
    }


    if(!CheckProjectAccess(ctx, svc_.get(), project_id, false, false))
    {
        WriteForbidden(ctx);
        return;
    }

    nljson pattern_info;
    try {

        pattern_info = svc_->GetPatternInfoById(ctx, project_id);


    } catch(const std::exception& e) {
        PJ_F_ERROR("service GetPatternInfoById exception: %s \n", e.what());

        WriteJsonError(ctx, -300, "service failed");

        return;
    }

    nljson root;
    root["code"] = 0;
    root["message"] = "success";
    root["data"] = pattern_info;
    WriteJsonResponse(ctx, root);

    // PJ_DEBUG() << std::endl << root.dump(4) << std::endl;

    return;
}


void ProjectHandler::EditPatternInfo(kit_muduo::TcpConnectionPtr conn, kit_muduo::HttpContextPtr ctx) noexcept
{
    auto req = ctx->request();
    auto resp = ctx->response();
    resp->setVersion(Version::kHttp11);
    resp->setStateCode(StateCode::k200Ok);
    resp->setContentMeta(MakeContentMeta(KnownMediaType::kApplicationJson));

    WriteOpResult write_result;
    ProjectEditPatternInfoReq request;

    auto bind_result = ctx->bindJson(request);
    if(!bind_result.ok)
    {
        PC_F_ERROR("body bind error: %s\n", bind_result.message.c_str());

        WriteOpResponseHelper(ctx, write_result.failed(-200, "body parse error"));
        return;
    }

    auto current_user = CurrentUserFromContext(ctx);

    auto auth_project = svc_->GetById(ctx, request.id);

    if((!current_user.IsAdmin() && auth_project.m_userId != current_user.user_id))
    {
        WriteForbidden(ctx);
        return;
    }

    ProjectRuntimeResult pj_runtime_result;
    try {

        pj_runtime_result = project_runtime_manager_->editPatternInfo(ctx, request.id, request.pattern_info);

    } catch(const std::exception& e) {
        PJ_F_ERROR("service UpdatePatternInfo exception: %s \n", e.what());

        WriteOpResponseHelper(ctx, write_result.failed(-300, "service failed"));
        return;
    }

    WriteOpResponseHelper(ctx, WriteOpResult::FromPjRuntimeResult(pj_runtime_result));
}

void ProjectHandler::RestoreProject(kit_muduo::TcpConnectionPtr conn, kit_muduo::HttpContextPtr ctx) noexcept
{
    auto resp = ctx->response();
    resp->setVersion(Version::kHttp11);
    resp->setStateCode(StateCode::k200Ok);
    resp->setContentMeta(MakeContentMeta(KnownMediaType::kApplicationJson));

    WriteOpResult write_result;

    auto current_user = CurrentUserFromContext(ctx);
    if(!current_user.IsAdmin())
    {
        WriteForbidden(ctx);
        return;
    }

    int64_t project_id = 0;
    if(!ParseRouteArithmetic(ctx, "project_id", project_id))
    {
        PJ_F_ERROR("route dynamic param error! \n");

        WriteOpResponseHelper(ctx, write_result.allErr().failed(-200, "route dynamic param error"));
        return;
    }

    bool ok = svc_->UpdateStatus(ctx, project_id, ProjectStatus::kValid)
        && svc_->UpdateRuntimeState(ctx, project_id, ProjectRuntimeState::kStopped, 0);
    if(!ok)
    {
        WriteOpResponseHelper(ctx, write_result.allErr().failed(-300, "service failed"));
        return;
    }

    WriteOpResponseHelper(ctx, write_result.persistedOk().success());
}


}   // kit_domain
