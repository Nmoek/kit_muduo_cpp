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

    // multiform转换
    static bool from_multi_form(const MultiFormConvert::PartMap &parts, CustomPatternFieldReq &req)
    {
        PJ_WARN() << "CustomPatternFieldReq dont support from_multi_form" << std::endl;
        return false;
    }
};

struct CustomPatternMagicNumReq {
    int32_t      pos;
    std::string  value;

    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(CustomPatternMagicNumReq, pos, value)

    static bool from_multi_form(const MultiFormConvert::PartMap &parts, CustomPatternMagicNumReq &req)
    {
        PJ_WARN() << "CustomPatternMagicNumReq dont support from_multi_form" << std::endl;
        return false;
    }
};


struct AddProjectReq {
    std::string              name;             // 测试名称
    int32_t                  mode;             // 测试模式
    ProtocolType                  protocol_type;    // 协议种类 1 2 3
    // uint16_t                 listen_port;      // 监听端口号(弃用 不再由用户指定)
    std::string              target_ip;        // 目标ip + 端口 x.x.x.x:8888
    nljson                   pattern_info;  // 解析格式信息

    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(AddProjectReq, name, mode, protocol_type, target_ip, pattern_info)

    static bool from_multi_form(const MultiFormConvert::PartMap &parts, AddProjectReq &req)
    {
        PJ_WARN() << "AddProjectReq dont support from_multi_form" << std::endl;
        return false;
    }
};


struct StartAndStopProjectReq 
{
    int64_t id;
    int32_t operation; // 1 start 0 stop

    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(StartAndStopProjectReq, operation)

    static bool from_multi_form(const MultiFormConvert::PartMap &parts, StartAndStopProjectReq &req)
    {
        PJ_WARN() << "StartAndStopProjectReq dont support from_multi_form" << std::endl;
        return false;
    }
};

/**
 * @brief List 用于Body解析
 */
struct ProjectListReq { 
    int32_t                  offset;          // 页码
    int32_t                  limit;           // 页大小
    
    NLOHMANN_DEFINE_TYPE_INTRUSIVE(ProjectListReq, offset, limit)

    static bool from_multi_form(const MultiFormConvert::PartMap &parts, ProjectListReq &req)
    {
        PJ_WARN() << "ProjectListReq dont support from_multi_form" << std::endl;
        return false;
    }
};

struct ProjectDetailNameReq {
    
    std::string name;
    NLOHMANN_DEFINE_TYPE_INTRUSIVE(ProjectDetailNameReq, name)

    static bool from_multi_form(const MultiFormConvert::PartMap &parts, ProjectDetailNameReq &req)
    {
        PC_WARN() << "ProjectDetailNameReq dont support from_multi_form" << std::endl;
        return false;
    }
};

struct ProjectEditPatternInfoReq {
    int64_t     id;            // project id
    nljson      pattern_info;  // 格式内容
    NLOHMANN_DEFINE_TYPE_INTRUSIVE(ProjectEditPatternInfoReq, id, pattern_info)

    static bool from_multi_form(const MultiFormConvert::PartMap &parts, ProjectEditPatternInfoReq &req)
    {
        PC_WARN() << "ProjectEditPatternInfoReq dont support from_multi_form" << std::endl;
        return false;
    }
};


/***************Body解析临时变量定义 其他模块不允许引用**************** */

ProjectHandler::ProjectHandler(std::shared_ptr<ProjectSvcInterface> svc, std::shared_ptr<ProtocolSvcInterface> pc_svc)
    :_svc(std::move(svc)),
     _pc_svc(std::move(pc_svc)),
     _app(nullptr)
{  }


ProjectHandler::~ProjectHandler() { }

void ProjectHandler::RegisterRoutes(std::shared_ptr<kit_muduo::http::HttpServer> server)
{
#define XX(WORK_FUNC) \
    std::bind(&ProjectHandler::WORK_FUNC, this, std::placeholders::_1, std::placeholders::_2)

    // 新增测试服务
    server->Post("/projects/add", XX(AddProject));

    // 开启/停止测试服务监听端口
    server->Post("/projects/:project_id/status", XX(StartAndStopProject));

    // 获取所有未软删除的测试服务
    server->Get("/projects/valid", XX(GetAllValid));

    // TODO 使用正则表达式
    // TODO 查询单个服务 动态路由
    // TODO 接口需要重新考虑 有点丑陋
    server->Get("/projects/:project_id", XX(SingleProject));
    server->Delete("/projects/:project_id", XX(DelProject));
    server->Post("/projects/:project_id/restore", XX(RestoreProject));

    // 获取/修改某个服务的title名称
    server->Post("/projects/:project_id/name", XX(DetailName));

    // TODO 删除测试服务 动态路由
        // 1. 关监听端口
        // 2. 删协议项
        // 2. 删服务项
    // server->Post("/projects/del/:id", XX(DelProject));
    
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
    resp->body().setContentType(ContentType::kJsonType);

    WriteOpResult write_result;
    AddProjectReq request;

    // 自动根据req中的 content-type类型去解析对象
    bool ok = ctx->Bind(&request);
    if(!ok)
    {
        PJ_F_ERROR("body bind error! \n");

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
        resp->setStateCode(StateCode::k403Forbidden);
        resp->body().appendData(R"({"code": -403, "message":"forbidden", "data":{}})");
        return;
    }

    // DTO转换 避免对外暴露领域模型Entity
    kit_domain::Project p;
    p.m_id = -1;
    p.m_name = std::move(request.name);
    p.m_mode = static_cast<ProjectMode>(request.mode);
    p.m_protocolType = request.protocol_type;
    p.m_listenPort = 0;
    p.m_targetIp =  std::move(request.target_ip);
    p.m_userId = current_user.user_id;
    p.m_status = ProjectStatus::kValid;
    p.m_runtimeState = ProjectRuntimeState::kStopped;
    p.m_patternInfo = std::move(request.pattern_info);

    int project_id = -1;
    try {
        project_id = _svc->Add(ctx, p);
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
    resp->body().setContentType(ContentType::kJsonType);

    WriteOpResult write_result;

    const std::string& project_id_str = ctx->routeParam("project_id");
    const std::string &operation_str = ctx->queryParam("operation");

    int64_t project_id = atoi(project_id_str.c_str());
    const ProjectRuntimeState will_state = static_cast<ProjectRuntimeState>(atoi(operation_str.c_str()));

    if(project_id_str.empty()
        || operation_str.empty()
        || project_id <= 0
        || (ProjectRuntimeState::kRunning != will_state && ProjectRuntimeState::kStopped != will_state))
    {
        WriteOpResponseHelper(ctx, write_result.allErr().failed(-200, "request param invalid"));
        return;
    }

    auto current_user = CurrentUserFromContext(ctx);

    auto auth_project = _svc->GetById(ctx, project_id);
    if(auth_project.m_id <= 0 || (!current_user.IsAdmin() && auth_project.m_userId != current_user.user_id))
    {
        resp->setStateCode(StateCode::k403Forbidden);
        resp->body().appendData(R"({"code": -403, "message":"forbidden", "data":{}})");
        return;
    }

    bool ok = false;
    uint16_t cur_listen_port = 0;
    try {

        if(ProjectRuntimeState::kRunning == will_state)
        {
            auto project_server = _app->findServer(project_id);
            if(!project_server)
            {
                // 获取loop租约
                auto lease_loop = _app->leaseLoop(project_id);
                if(!lease_loop)
                {
                    WriteOpResponseHelper(ctx, write_result.failed(-300, "loop lease faild"));
                    return;
                }

                auto p = _svc->GetById(ctx, project_id);
                if(p.m_id <= 0)
                {
                    throw std::runtime_error("GetById error");
                }

                // 工厂模式创建测试服务
                project_server = ProjectServerFactory::Create(p, lease_loop);
                if (!project_server) 
                {
                    PJ_F_ERROR("create ProjectServer faild! protocol_type[%d] project_id[%d] \n", static_cast<int32_t>(p.m_protocolType), project_id);

                    WriteOpResponseHelper(ctx, write_result.failed(-300, "failed to create server"));
                    return;
                }

                // 把所有未删且已上线的协议都加到服务器上
                std::vector<Protocol> pcs = _pc_svc->GetActiveByProject(nullptr, project_id);
                for(auto &pc : pcs)
                {
                    auto protocol_item = ProtocolItemFactory::Create(std::make_shared<Protocol>(pc), project_server);
                    if(!protocol_item)
                    {
                        PJ_F_ERROR("create ProtocolItem faild!  protocol_id[%d] protocol_type[%d] project_id[%d] \n",  pc.m_id, static_cast<int32_t>(pc.m_type), pc.m_projectId);

                        WriteOpResponseHelper(ctx, write_result.failed(-300, "failed to create server"));
                        return;
                    }

                    project_server->AddProtocolItem(protocol_item);
                    
                    PJ_F_DEBUG("pjId[%d], pcId[%d], name[%s] add success!\n", pc.m_projectId, pc.m_id, pc.m_name.c_str());
                }

                cur_listen_port = project_server->getBindAddr().toPort();
                // 主键id更新
                project_server->setProjectId(project_id);
                _app->addServer(project_id, project_server);

                project_server->start();
            }
            else
            {
                PJ_F_WARN("project server has stared! pjId[%ld] \n", project_id);
                cur_listen_port = project_server->getBindAddr().toPort();
            }
        }
        else if(ProjectRuntimeState::kStopped == will_state)
        {
            auto project_server = _app->findServer(project_id);
            if (project_server) 
            {
                project_server->stop(); // 注意: 这里会阻塞执行
                _app->delServer(project_id);
            }
            else
            {
                PJ_F_WARN("ProjectServer not found or stopped! project_id[%d] \n", project_id);
            }
        }

        // 更新数据库
        ok = _svc->UpdateRuntimeStatus(ctx, project_id, will_state, cur_listen_port);
        if(!ok)
        {
            PJ_F_ERROR("UpdateRuntimeStatus error! pjId[%ld] will_status[%d], cur_listen_port[%u]\n", project_id, static_cast<int32_t>(will_state), cur_listen_port);

            WriteOpResponseHelper(ctx, write_result.runOk().failed(-300, "service failed"));
            return;
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

    WriteOpResponseHelper(ctx, write_result.allOk().success(), [cur_listen_port, will_state](auto &root){
        root["data"]["listen_port"] = cur_listen_port;
        root["data"]["runtime_state"] = will_state;
    });
}

void ProjectHandler::DelProject(kit_muduo::TcpConnectionPtr conn, kit_muduo::HttpContextPtr ctx) noexcept
{
    auto req = ctx->request();
    auto resp = ctx->response();
    resp->setVersion(Version::kHttp11);
    resp->setStateCode(StateCode::k200Ok);

    WriteOpResult write_result;

    // PJ_DEBUG() << "ProjectHandler::DelProject " << std::endl << req->body().toString() << std::endl;

    // 获取测试服务主键id
    std::string val1 = ctx->routeParam("project_id");
    if(val1.empty())
    {
        PJ_F_ERROR("query param parse error! \n");

        WriteOpResponseHelper(ctx, write_result.failed(-200, "query param parse error"));
        return;
    }

    bool ok = false;
    // 查测试服务 信息
    try 
    {
        int64_t project_id = std::stol(val1);
        auto current_user = CurrentUserFromContext(ctx);
        auto auth_project = _svc->GetById(ctx, project_id);
        if(auth_project.m_id <= 0 || (!current_user.IsAdmin() && auth_project.m_userId != current_user.user_id))
        {
            resp->setStateCode(StateCode::k403Forbidden);
            resp->body().appendData(R"({"code": -403, "message":"forbidden", "data":{}})");
            return;
        }

        auto pj_server = _app->findServer(project_id);
        if(!pj_server)
        {
            PJ_F_WARN("runtime project server not exist! pjId[%ld] \n", project_id);
        }
        else
        {
            pj_server->stop();
            _app->delServer(project_id);
        }

        ok = _svc->UpdateStatus(ctx, project_id, ProjectStatus::kInvalid) && _svc->UpdateRuntimeStatus(ctx, project_id, ProjectRuntimeState::kStopped, 0);
        if(!ok)
        {
            throw std::runtime_error("UpdateStatus error!");
        }

    }
    catch(const std::exception& e)
    {
        PJ_F_ERROR("service GetById exception: %s \n", e.what());

        WriteOpResponseHelper(ctx, write_result.failed(-300, "service failed"));
        return;
    }

    WriteOpResponseHelper(ctx, write_result.allOk().success());
}

void ProjectHandler::SingleProject(kit_muduo::TcpConnectionPtr conn, kit_muduo::HttpContextPtr ctx) noexcept
{
    auto req = ctx->request();
    auto resp = ctx->response();
    resp->setVersion(Version::kHttp11);
    resp->setStateCode(StateCode::k200Ok);

    // user_id怎么获取??
    // 使用的是query param模式不需要进行body解析
    // 获取测试服务主键id

    int64_t project_id = 0;
    try {
        // TODO boost万能转换
        project_id = std::stol(ctx->routeParam("project_id"));
        if(project_id <= 0)
            throw;

    } catch(const std::exception& e) {

        PJ_F_ERROR("query param transform fail! project_id=%d , %s\n", project_id, e.what());
        
        resp->body().appendData(R"({"code": -200, "message":"query param transform fail"})");
        return;
    }


    Project project;
    // 查测试服务 信息
    try 
    {
        project = _svc->GetById(ctx, project_id);
        auto current_user = CurrentUserFromContext(ctx);
        if(project.m_id > 0 && !current_user.IsAdmin() && project.m_userId != current_user.user_id)
        {
            resp->setStateCode(StateCode::k403Forbidden);
            resp->body().appendData(R"({"code": -403, "message":"forbidden", "data":{}})");
            return;
        }
    }
    catch(const std::exception& e)
    {
        PJ_F_ERROR("service GetById exception: %s \n", e.what());

        resp->body().appendData(R"({"code": -300, "message":"service failed"})");

        return;
    }

    if(project.m_id <= 0)
    {
        resp->body().appendData(R"({"code": 0, "message":"project is not exists!", "data":[]})");

        return;
    }

    nljson root;
    root["code"] = 0; // TODO domain错误码统一化
    root["message"] = "success";
    root["data"].push_back(CovertProjectVo(project));

    resp->body().setContentType(ContentType::kJsonType);
    resp->body().appendData(root.dump());
    PJ_DEBUG() << std::endl << root.dump(4) << std::endl;
}

void ProjectHandler::List(kit_muduo::TcpConnectionPtr conn, kit_muduo::HttpContextPtr ctx) noexcept
{
    auto req = ctx->request();
    auto resp = ctx->response();
    resp->setVersion(Version::kHttp11);
    resp->setStateCode(StateCode::k200Ok);
    ProjectListReq request;

    PJ_DEBUG() << std::endl << req->body().toString() << std::endl;

    // 自动根据req中的 content-type类型去解析对象
    bool ok = ctx->Bind(&request);
    if(!ok)
    {
        PJ_F_ERROR("body bind error! \n");
        resp->body().appendData(R"({"code": -200, "message":"body parse error"})");
        return;
    }


    std::vector<Project> projects;
    // 查测试服务 信息
    try 
    {
        auto current_user = CurrentUserFromContext(ctx);
        if(current_user.IsAdmin())
        {
            projects = _svc->GetAll(ctx, request.offset, request.limit);
        }
        else
        {
            projects = _svc->GetByUser(ctx, current_user.user_id, ProjectStatus::kValid, request.offset, request.limit);
        }
    }
    catch(const std::exception& e)
    {
        PJ_F_ERROR("service GetByUser exception: %s \n", e.what());
        resp->body().appendData(R"({"code": -300, "message":"service failed"})");

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
    resp->body().appendData(root.dump());

    PJ_DEBUG() << std::endl << root.dump(4) << std::endl;
}

void ProjectHandler::GetAllValid(kit_muduo::TcpConnectionPtr conn, kit_muduo::HttpContextPtr ctx) noexcept
{
    auto req = ctx->request();
    auto resp = ctx->response();
    resp->setVersion(Version::kHttp11);
    resp->setStateCode(StateCode::k200Ok);
    resp->body().setContentType(ContentType::kJsonType);

    PJ_DEBUG() << std::endl << req->body().toString() << std::endl;

    std::vector<Project> projects;
    try 
    {
        auto current_user = CurrentUserFromContext(ctx);
        if(current_user.IsAdmin())
        {
            projects = _svc->GetAllValid(ctx);
        }
        else
        {
            projects = _svc->GetByUser(ctx, current_user.user_id, ProjectStatus::kValid, 0, 1000);
        }
    }
    catch(const std::exception& e)
    {
        PJ_F_ERROR("service GetAllValid exception: %s \n", e.what());
        resp->body().appendData(R"({"code": -300, "message":"service failed"})");
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

    resp->body().appendData(root.dump());

    PJ_DEBUG() << std::endl << root.dump(4) << std::endl;
}

void ProjectHandler::DetailName(kit_muduo::TcpConnectionPtr conn, kit_muduo::HttpContextPtr ctx) noexcept
{
    auto req = ctx->request();
    auto resp = ctx->response();
    resp->setVersion(Version::kHttp11);
    resp->setStateCode(StateCode::k200Ok);
    resp->body().setContentType(ContentType::kJsonType);

    ProjectDetailNameReq request;
    WriteOpResult write_result;

    PC_DEBUG() << std::endl << req->body().toString() << std::endl;

    int64_t project_id = 0;
    try {
        // TODO boost万能转换
        project_id = std::stol(ctx->routeParam("project_id"));
        if(project_id <= 0)
        {
            throw std::runtime_error("`project_id` parse error");
        }

    } catch(const std::exception& e) {

        PJ_F_ERROR("query param fail: %s! pjId[%d] \n", e.what(), project_id);
        

        WriteOpResponseHelper(ctx, write_result.failed(-200, "query param  fail"));
        return;
    }

    auto current_user = CurrentUserFromContext(ctx);
    auto auth_project = _svc->GetById(ctx, project_id);
    if(auth_project.m_id <= 0 || (!current_user.IsAdmin() && auth_project.m_userId != current_user.user_id))
    {
        resp->setStateCode(StateCode::k403Forbidden);
        resp->body().appendData(R"({"code": -403, "message":"forbidden", "data":{}})");
        return;
    }

    // 自动根据req中的 content-type类型去解析对象
    bool ok = ctx->Bind(&request);
    if(!ok)
    {
        PC_F_ERROR("body bind error! \n");

        WriteOpResponseHelper(ctx, write_result.failed(-200, "body parse error"));
        return;
    }

    try {

        bool ok = _svc->UpdateName(ctx, project_id, request.name);
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
    resp->body().setContentType(ContentType::kJsonType);

    int64_t project_id = stoi(ctx->routeParam("project_id"));
    auto current_user = CurrentUserFromContext(ctx);
    auto auth_project = _svc->GetById(ctx, project_id);
    if(auth_project.m_id <= 0 || (!current_user.IsAdmin() && auth_project.m_userId != current_user.user_id))
    {
        resp->setStateCode(StateCode::k403Forbidden);
        resp->body().appendData(R"({"code": -403, "message":"forbidden","data":{}})");
        return;
    }

    std::vector<char> pattern_info;
    try {

        pattern_info = _svc->GetPatternInfoById(ctx, project_id);


    } catch(const std::exception& e) {
        PJ_F_ERROR("service GetPatternInfoById exception: %s \n", e.what());
        
        resp->body().appendData(R"({"code": -300, "message":"service failed"})");

        return;
    }

    nljson root;
    root["code"] = 0;
    root["message"] = "success";
    root["data"] = nljson::parse(pattern_info);
    resp->body().appendData(root.dump());

    PJ_DEBUG() << std::endl << root.dump(4) << std::endl;

    return;
}


void ProjectHandler::EditPatternInfo(kit_muduo::TcpConnectionPtr conn, kit_muduo::HttpContextPtr ctx) noexcept
{
    auto req = ctx->request();
    auto resp = ctx->response();
    resp->setVersion(Version::kHttp11);
    resp->setStateCode(StateCode::k200Ok);
    resp->body().setContentType(ContentType::kJsonType);

    WriteOpResult write_result;
    ProjectEditPatternInfoReq request;

    bool ok = ctx->Bind(&request);
    if(!ok)
    {
        PC_F_ERROR("body bind error! \n");

        WriteOpResponseHelper(ctx, write_result.failed(-200, "body parse error"));
        return;
    }

    const std::string& json_str = request.pattern_info.dump();
    auto current_user = CurrentUserFromContext(ctx);
    auto auth_project = _svc->GetById(ctx, request.id);
    if(auth_project.m_id <= 0 || (!current_user.IsAdmin() && auth_project.m_userId != current_user.user_id))
    {
        resp->setStateCode(StateCode::k403Forbidden);
        resp->body().appendData(R"({"code": -403, "message":"forbidden", "data":{}})");
        return;
    }
    if(!CustomTcpPatternSpec::FromJson(request.pattern_info).has_value())
    {
        PC_F_ERROR("custom tcp pattern_info invalid\n");

        WriteOpResponseHelper(ctx, write_result.failed(-200, "pattern info invalid"));
        return;
    }

    if(ProjectRuntimeState::kRunning == auth_project.m_runtimeState || nullptr != _app->findServer(request.id))
    {
        WriteOpResponseHelper(ctx, write_result.failed(-200, "project is running"));
        return;
    }


    const std::vector<char> pattern_info(json_str.begin(), json_str.end());
    ok = false;
    try {

        ok = _svc->UpdatePatternInfo(ctx, request.id, pattern_info);
        if(!ok)
        {
            throw std::runtime_error("UpdatePatternInfo failed");
        }

    } catch(const std::exception& e) {
        PJ_F_ERROR("service UpdatePatternInfo exception: %s \n", e.what());

        WriteOpResponseHelper(ctx, write_result.failed(-300, "service failed"));
        return;
    }

    WriteOpResponseHelper(ctx, write_result.persistedOk().success());
}

void ProjectHandler::RestoreProject(kit_muduo::TcpConnectionPtr conn, kit_muduo::HttpContextPtr ctx) noexcept
{
    auto resp = ctx->response();
    resp->setVersion(Version::kHttp11);
    resp->setStateCode(StateCode::k200Ok);
    resp->body().setContentType(ContentType::kJsonType);

    WriteOpResult write_result;

    auto current_user = CurrentUserFromContext(ctx);
    if(!current_user.IsAdmin())
    {
        resp->setStateCode(StateCode::k403Forbidden);
        resp->body().appendData(R"({"code": -403, "message":"forbidden", "data":{}})");
        return;
    }

    int64_t project_id = 0;
    try {
        project_id = std::stol(ctx->routeParam("project_id"));
    } catch(const std::exception &) {

        WriteOpResponseHelper(ctx, write_result.allErr().failed(-200, "query param fail"));
        return;
    }
    
    bool ok = _svc->UpdateStatus(ctx, project_id, ProjectStatus::kValid)
        && _svc->UpdateRuntimeStatus(ctx, project_id, ProjectRuntimeState::kStopped, 0);
    if(!ok)
    {
        WriteOpResponseHelper(ctx, write_result.allErr().failed(-300, "service failed"));
        return;
    }

    WriteOpResponseHelper(ctx, write_result.persistedOk().success());
}


}   // kit_domain
