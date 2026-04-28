/**
 * @file web_project.cpp
 * @brief 测试服务 web层接口
 * @author ljk5
 * @version 1.0
 * @date 2025-07-17 16:53:47
 * @copyright Copyright (c) 2025 HIKRayin
 */
#include "web/web_project.h"
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

#include <memory>
#include <cstring>

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

#if 0
struct CustomPatternInfoReq {
    int32_t                     pattern_type;
    nljson                      pattern_info;
    nljson                      pattern_fields;

    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(CustomPatternReq, pattern_type, pattern_info, pattern_fields)

    static bool from_multi_form(const MultiFormConvert::PartMap &parts, CustomPatternReq &req)
    {
        PJ_WARN() << "CustomPatternReq dont support from_multi_form" << std::endl;
        return false;
    }
};
#endif

/**
 * @brief AddProject 用于Body解析
 */ 
struct AddProjectReq {
    std::string              name;             // 测试名称
    int32_t                  mode;             // 测试模式
    int32_t                  protocol_type;    // 协议种类 1 2 3
    uint16_t                 listen_port;      // 监听端口号
    std::string              target_ip;        // 目标ip + 端口 x.x.x.x:8888
    int32_t                  pattern_type;
    nljson                   pattern_info;  // 解析格式信息

    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(AddProjectReq, name, mode, protocol_type, listen_port, target_ip, pattern_type, pattern_info)

    static bool from_multi_form(const MultiFormConvert::PartMap &parts, AddProjectReq &req)
    {
        PJ_WARN() << "AddProjectReq dont support from_multi_form" << std::endl;
        return false;
    }
};

/**
 * @brief List 用于Body解析
 */
struct ProjectListReq {
    // int64_t                  user_id;          // 所属用户id不能放在协议里 
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
    int32_t     pattern_type;  // 自定义TCP格式
    nljson      pattern_info;  // 格式内容
    NLOHMANN_DEFINE_TYPE_INTRUSIVE(ProjectEditPatternInfoReq, id, pattern_type, pattern_info)

    static bool from_multi_form(const MultiFormConvert::PartMap &parts, ProjectEditPatternInfoReq &req)
    {
        PC_WARN() << "ProjectEditPatternInfoReq dont support from_multi_form" << std::endl;
        return false;
    }
};


/***************Body解析临时变量定义 其他模块不允许引用**************** */

ProjectHandler::ProjectHandler(std::shared_ptr<kit_domain::ProjectSvcInterface> svc)
    :_svc(svc)
{  }


ProjectHandler::~ProjectHandler() { }

ProjectHandler* ProjectHandler::Instance(std::shared_ptr<ProjectSvcInterface> svc)
{
    static ProjectHandler h(svc);
    return &h;
}


void ProjectHandler::RegisterRoutes(std::shared_ptr<kit_muduo::http::HttpServer> server)
{
#define XX(WORK_FUNC) \
    std::bind(&ProjectHandler::WORK_FUNC, this, std::placeholders::_1, std::placeholders::_2)

    // 新增测试服务
    server->Post("/projects/add", XX(AddProject));

    // 开启测试服务监听端口
    // server->Post("/projects/start", XX(AddProject));

    // TODO 使用正则表达式
    // TODO 查询单个服务 动态路由
    // TODO 接口需要重新考虑 有点丑陋
    server->Get("/projects/:project_id", XX(SingleProject));
    server->Delete("/projects/:project_id", XX(DelProject));
 
    // 获取/修改某个服务的title名称
    // TODO 动态路由 
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

    if(ProjectMode::ServerMode == request.mode
        && request.listen_port <= 0)
    {
        PJ_F_ERROR("server mode listen port invalid\n");
        return false;
    }
    else if(ProjectMode::ClientMode == request.mode)
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

    if(request.protocol_type <= static_cast<int32_t>(ProtocolType::UNKNOWN_PROTOCOL) &&
       request.protocol_type >= static_cast<int32_t>(ProtocolType::MAX))
    {
        PJ_F_ERROR("protocol type invalid\n");
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

    AddProjectReq request;

    // 自动根据req中的 content-type类型去解析对象
    bool ok = ctx->Bind(&request);
    if(!ok)
    {
        PJ_F_ERROR("body bind error! \n");
        resp->body().appendData(R"({"code": -200, "message":"body parse error"})");
        return;
    }

    if(!CheckProjectInfo(request))
    {
        resp->body().appendData(R"({"code": -200, "message":"test service info invalid"})");
        return;
    }


    // DTO转换 避免对外暴露领域模型Entity
    kit_domain::Project p;
    p.m_id = -1;
    p.m_name = std::move(request.name);
    p.m_mode = static_cast<ProjectMode>(request.mode);
    p.m_protocolType = static_cast<ProtocolType>(request.protocol_type);
    p.m_listenPort = request.listen_port;
    p.m_targetIp =  std::move(request.target_ip);
    p.m_userId = 1/*request.user_id 暂时写死*/;
    p.m_status = ProjectStatus::ON_STATUS; // 新增一定是有效的 TODO后期根据实际保活探测决定
    p.m_patternType = static_cast<CustomTcpPatternType>(request.pattern_type);

    std::string tmp_str = std::move(request.pattern_info.dump());
    const std::vector<char> pattern_info(tmp_str.begin(), tmp_str.end());
    p.m_patternInfo = std::move(pattern_info);


    int project_id = -1;
    try 
    {
        //TODO: 增加服务时需要判断端口是否重复
        project_id = _svc->Add(ctx, p);
    }
    catch(const std::exception& e)
    {
        PJ_F_ERROR("service add exception: %s \n", e.what());
    }

    if(project_id <= 0)
    {
        PJ_F_ERROR("service add failed \n");

        // TODO: 自定义业务错误码统一处理
        resp->body().appendData(R"({"code": -300, "message":"service failed"})");

        return;
    }
    // 主键id更新一下
    p.m_id = project_id;
    
    // 工厂模式创建ProjectServer
    auto project_server = ProjectServerFactory::Create(p);
    if (!project_server) 
    {
        PJ_F_ERROR("create ProjectServer faild! protocol_type[%d] project_id[%d] \n", static_cast<int32_t>(p.m_protocolType), project_id);

        resp->body().appendData(R"({"code": -300, "message":"failed to create server"})");
        return;
    }

    _app->AddServer(project_id, project_server);

    // 2. 需要和开启的服务进行通信（通信方式如何选择?)，需要进行增删改协议项
    // 2.1 线程通信  复用loop队列
    // 2.2 RPC通信
    // 2.3 注册Web API

    nljson root;
    root["code"] = 0;
    root["message"] = "success";
    root["data"]["project_id"] = project_id;
    resp->body().appendData(root.dump());
}

void ProjectHandler::DelProject(kit_muduo::TcpConnectionPtr conn, kit_muduo::HttpContextPtr ctx) noexcept
{
    auto req = ctx->request();
    auto resp = ctx->response();
    resp->setVersion(Version::kHttp11);
    resp->setStateCode(StateCode::k200Ok);

    PJ_DEBUG() << "ProjectHandler::DelProject " << std::endl << req->body().toString() << std::endl;

    // user_id怎么获取??
    // 使用的是query param模式不需要进行body解析
    // 获取测试服务主键id
    std::string val1 = ctx->Param("project_id");
    // std::string val2 = ctx->Param("user_id"); // ??
    if(val1.empty())
    {
        PJ_F_ERROR("query param parse error! \n");
        resp->body().appendData(R"({"code": -200, "message":"query param parse error"})");
        return;
    }

    int64_t project_id = 0;
    // int64_t user_id = 0;
    try {

        project_id = std::stol(val1);

    } catch(const std::exception& e) {

        PJ_F_ERROR("query param transform fail! %s , %s\n", val1.c_str(), e.what());
        
        resp->body().appendData(R"({"code": -200, "message":"query param transform fail"})");
        return;
    }


    bool ok = false;
    // 查测试服务 信息
    try 
    {
        ok = _svc->UpdateStatus(ctx, project_id, ProjectStatus::OFF_STATUS);
    }
    catch(const std::exception& e)
    {
        PJ_F_ERROR("service GetById exception: %s \n", e.what());

        resp->body().appendData(R"({"code": -300, "message":"service failed"})");

        return;
    }

    if(!ok)
    {
        resp->body().appendData(R"({"code": 0, "message":"delete project fail!", "data":[]})");
        return;
    }

    nljson root;
    root["code"] = 0; // TODO domain错误码统一化
    root["message"] = "success";
    root["data"] = nljson::array();

    resp->body().appendData(root.dump());
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
        project_id = std::stol(ctx->Param("project_id"));
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
        // DEBUG: 给一个默认admin用户 写死为1 所有访问都共用一个账户
        projects = _svc->GetByUser(ctx, 1/*request.user_id*/, request.offset, request.limit);
    }
    catch(const std::exception& e)
    {
        PJ_F_ERROR("service GetByUser exception: %s \n", e.what());
        resp->body().appendData(R"({"code": -300, "message":"service failed"})");

        return;
    }

    // TODO: 查协议数

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

void ProjectHandler::DetailName(kit_muduo::TcpConnectionPtr conn, kit_muduo::HttpContextPtr ctx) noexcept
{
    auto req = ctx->request();
    auto resp = ctx->response();
    resp->setVersion(Version::kHttp11);
    resp->setStateCode(StateCode::k200Ok);
    resp->body().setContentType(ContentType::kJsonType);

    ProjectDetailNameReq request; //json

    PC_DEBUG() << std::endl << req->body().toString() << std::endl;

    int64_t project_id = 0;
    try {
        // TODO boost万能转换
        project_id = std::stol(ctx->Param("project_id"));
        if(project_id <= 0)
            throw;

    } catch(const std::exception& e) {

        PJ_F_ERROR("query param transform fail! project_id=%d , %s\n", project_id, e.what());
        
        resp->body().appendData(R"({"code": -200, "message":"query param transform fail"})");
        return;
    }

    // 自动根据req中的 content-type类型去解析对象
    bool ok = ctx->Bind(&request);
    if(!ok)
    {
        PC_F_ERROR("body bind error! \n");

        resp->body().appendData(R"({"code": -200, "message":"body parse error"})");
        return;
    }

    nljson root;
    root["code"] = 0;
    root["message"] = "success";
    root["data"] = nljson::object();
    try 
    {
        if(HttpRequest::Method::kPost == req->method()())
        {
            bool ok = _svc->UpdateName(ctx, project_id, request.name);
            if(!ok)
            {
                PJ_F_ERROR("service UpdateName failed \n");
        
                root["code"] = -300;
                root["message"] = "service failed";
            }
        }
        else if(HttpRequest::Method::kGet == req->method()())
        {
            auto pj = _svc->GetById(ctx, project_id);
            root["data"]["id"] = pj.m_id;
            root["data"]["name"] = pj.m_name;
        }

    }
    catch(const std::exception& e)
    {
        PC_F_ERROR("service UpdateName exception: %s \n", e.what());
        root["code"] = -300;
        root["message"] = "service failed";
    }

    resp->body().appendData(root.dump());
    return;
}



void ProjectHandler::QueryPatternInfo(kit_muduo::TcpConnectionPtr conn, kit_muduo::HttpContextPtr ctx) noexcept
{
    auto req = ctx->request();
    auto resp = ctx->response();
    resp->setVersion(Version::kHttp11);
    resp->setStateCode(StateCode::k200Ok);
    resp->body().setContentType(ContentType::kJsonType);

    int64_t project_id = stoi(ctx->Param("project_id"));

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

    ProjectEditPatternInfoReq request;

    bool ok = ctx->Bind(&request);
    if(!ok)
    {
        PC_F_ERROR("body bind error! \n");

        resp->body().appendData(R"({"code": -200, "message":"body parse error"})");
        return;
    }

    const std::string& json_str = request.pattern_info.dump();
    const std::vector<char> pattern_info(json_str.begin(), json_str.end());
    ok = false;
    try {

        ok = _svc->UpdatePatternInfo(ctx, request.id, request.pattern_type, pattern_info);
        
        if(!ok) throw;

    } catch(const std::exception& e) {
        PJ_F_ERROR("service UpdatePatternInfo exception: %s \n", e.what());
        
        resp->body().appendData(R"({"code": -300, "message":"service failed"})");

        return;
    }

    // TODO： 改完格式需要清除所有协议配置

    nljson root;
    root["code"] = 0;
    root["message"] = "success";
    root["data"] = nljson::object();
    resp->body().appendData(root.dump());

    return;

}


}   // kit_domain
