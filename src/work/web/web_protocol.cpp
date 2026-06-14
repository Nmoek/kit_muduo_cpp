/**
 * @file web_protocol.cpp
 * @brief 
 * @author ljk5
 * @version 1.0
 * @date 2025-07-25 18:06:36
 * @copyright Copyright (c) 2025 HIKRayin
 */
#include "web/web_protocol.h"
#include "domain/protocol.h"
#include "domain/project.h"
#include "domain/type.h"
#include "domain/user.h"
#include "service/svc_project.h"
#include "service/svc_protocol.h"
#include "web/protocol_vo.h"
#include "web/web_log.h"

#include "net/http/http_server.h"
#include "net/http/http_response.h"
#include "net/http/http_request.h"
#include "net/http/http_context.h"
#include "application.h"
#include "domain/project_server.h"
#include "domain/protocol_item.h"
#include "net/event_loop.h"
#include "web/write_response.h"
#include "work/domain/runtime_result.h"
#include "runtime/runtime_controller.h"

#include <functional>
#include <memory>
#include <stdexcept>

using namespace kit_muduo;
using namespace kit_muduo::http;
using nljson = nlohmann::json;

namespace kit_domain {

/***************Body解析临时变量定义 其他模块不允许引用**************** */

struct AddProtocolReqHeader {
    int64_t id{-1};                    // 原来有id需要赋值没有默认-1
    std::string name;                          // 测试协议名称
    ProtocolType type{ProtocolType::kUnknown};                           // 测试协议类型 HTTP/TCP
    int64_t project_id{-1};                        // 所属测试服务Id
    ProtocolBodyType req_body_type{ProtocolBodyType::kUnknown};                 // 请求协议体类型 json/xml/plain
    ProtocolBodyType resp_body_type{ProtocolBodyType::kUnknown};                 // 响应协议体类型 json/xml/plain
    ProtocolConfigState config_state{ProtocolConfigState::kOff};         // 协议项是否同步上线
    
    // TCP特有
    int32_t is_endian{0};                          // 是否进行大小端转换 1进行 0不进行 频繁查询更新字段
    
    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(AddProtocolReqHeader, id, name, type, project_id, req_body_type, resp_body_type, config_state, is_endian)
};

/**
 * @brief AddProtocol 用于Body解析
 * 对于 protocol_req_cfg protocol_resp_cfg
 * HTTP目前配置项格式:
    {
    "method": "POST",
    "path": "/api/test2"
    }
 *************************
 * 自定义TCP目前配置项格式:
    {
        "function_code_filed_value": "0x0100", // 功能码值
        "pattern_fields": [  //  普通字段配置
            {}
        ], 
    }
 */
struct AddProtocolReq {
    AddProtocolReqHeader header;
    nljson protocol_req_cfg;        // 请求协议配置数据json
    nljson protocol_resp_cfg;       // 响应协议配置数据json

    //TODO 这里是否使用json + base64?
    std::vector<char> protocol_req_body;       // 请求协议Body
    std::vector<char> protocol_resp_body;      // 响应协议Body

    NLOHMANN_DEFINE_TYPE_INTRUSIVE(AddProtocolReq, header, protocol_req_cfg, protocol_resp_cfg, protocol_req_body, protocol_resp_body) 

    static bool from_multi_form(const MultiFormConvert::PartMap &parts, AddProtocolReq &req)
    {
        
        // 必填 且 不能为空
        auto it = parts.find("protocol_cfg_header");
        if(it == parts.end() || it->second.data.empty()) 
        {
            PC_F_ERROR("multiform name 'protocol_cfg_header' invalid! \n");
            return false;
        }
        nljson::parse(it->second.data).get_to<AddProtocolReqHeader>(req.header);

        // 必填 允许为空
        it = parts.find("protocol_req_cfg");
        if(it == parts.end())
        {
            PC_F_ERROR("multiform name 'protocol_req_cfg' invalid! \n");
            return false;
        }
        req.protocol_req_cfg = nljson::parse(it->second.data);
  
        it = parts.find("protocol_resp_cfg");
        if(it == parts.end())
        {
            PC_F_ERROR("multiform name 'protocol_resp_cfg' invalid! \n");
            return false;
        }
        req.protocol_resp_cfg = nljson::parse(it->second.data);

        it = parts.find("protocol_req_body");
        if(it == parts.end())
        {
            PC_F_ERROR("multiform name 'protocol_req_body' invalid! \n");
            return false;
        }
        req.protocol_req_body = std::move(it->second.data);

        it = parts.find("protocol_resp_body");
        if(it == parts.end())
        {
            PC_F_ERROR("multiform name 'protocol_resp_body' invalid! \n");
            return false;
        }
        req.protocol_resp_body = std::move(it->second.data);

        return true;
    }

};

// 重配置请求结构 复用新增
using ReconfigProtocolReq = AddProtocolReq;


struct LaunchAndWithdrawsProtocolReq {
    ProtocolConfigState runtime_enabled;

    NLOHMANN_DEFINE_TYPE_INTRUSIVE(LaunchAndWithdrawsProtocolReq, runtime_enabled)

    static bool from_multi_form(const MultiFormConvert::PartMap &parts, LaunchAndWithdrawsProtocolReq &req)
    {
        PC_WARN() << "DelProtocolReq dont supoort!" << std::endl;
        return false;
    }
};


/**
 * @brief DelProtocol 用于Body解析
 */
struct DelProtocolReq {
    static bool from_multi_form(const MultiFormConvert::PartMap &parts, DelProtocolReq &req)
    {
        PC_WARN() << "DelProtocolReq dont supoort!" << std::endl;
        return false;
    }
};

/**
 * @brief List 用于Body解析
 */
struct ProtocolListReq {
    int64_t                  project_id;      // 所属测试服务id
    int32_t                  status{static_cast<int32_t>(ProtocolStatus::kValid)};  // 状态
    bool                     include_inactive{false}; // 管理员列表是否包含已删除协议项
    int32_t                  offset;          // 页码
    int32_t                  limit;           // 页大小
    
    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(ProtocolListReq, project_id, status, include_inactive, offset, limit)

    static bool from_multi_form(const MultiFormConvert::PartMap &parts, ProtocolListReq &req)
    {
        PJ_WARN() << "ProtocolListReq dont support from_multi_form" << std::endl;
        return false;
    }
};


struct ProtocolDetailNameReq {
    std::string name;

    NLOHMANN_DEFINE_TYPE_INTRUSIVE(ProtocolDetailNameReq, name)

    static bool from_multi_form(const MultiFormConvert::PartMap &parts, ProtocolDetailNameReq &req)
    {
        PC_WARN() << "ProtocolDetailNameReq dont support from_multi_form" << std::endl;
        return false;
    }
};

/*复用 */


struct DetailCfgReq {
    ProtocolSide     side;         // 校验到底是请求配置还是响应配置
    nljson           cfg_data;     // 协议配置数据 必须是json

    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(DetailCfgReq, side, cfg_data)

    static bool from_multi_form(const MultiFormConvert::PartMap &parts, DetailCfgReq &req)
    {
        PC_WARN() << "DetailCfgReq dont support from_multi_form" << std::endl;
        return false;
    }
};


struct DetailReqHeader {
    ProtocolSide side;    // 校验到底是请求配置还是响应配置
    ProtocolBodyType body_type;  // body数据格式类型

    // 带默认值 = 未解析到的字段也不会抛异常
    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(DetailReqHeader, side, body_type)

    static bool from_multi_form(const MultiFormConvert::PartMap &parts, DetailReqHeader &req)
    {
        PC_WARN() << "DetailReqHeader dont support from_multi_form" << std::endl;
        return false;
    }
};

struct DetailReq {
    DetailReqHeader header;
    std::vector<char> cfg_data;

    NLOHMANN_DEFINE_TYPE_INTRUSIVE(DetailReq, header, cfg_data)

    static bool from_multi_form(const MultiFormConvert::PartMap &parts, DetailReq &req)
    {
        auto it = parts.find("detail_header");
        if(it == parts.end()) 
        {
            PC_F_ERROR("multiform name: detail_req_header not found! \n");
            return false;
        }
        req.header = nljson::parse(it->second.data).get<DetailReqHeader>();

        it = parts.find("detail_cfg_data");
        if(it == parts.end())  // 可能会传入空的detail_cfg_data
        {
            PC_F_WARN("multiform name: detail_cfg_data not found! \n");
        }
        else
        {
            req.cfg_data = std::move(it->second.data);
        }
        return true;
    }
};


/***************Body解析临时变量定义 其他模块不允许引用**************** */
ProtocolHandler::ProtocolHandler(std::shared_ptr<ProtocolSvcInterface> svc, 
    std::shared_ptr<ProjectSvcInterface> pj_svc, 
    std::shared_ptr<RuntimeControllerInterface> project_runtime_manager)
    :svc_(std::move(svc))
    ,pj_svc_(std::move(pj_svc))
    ,project_runtime_manager_(std::move(project_runtime_manager))
{ 

}


void ProtocolHandler::RegisterRoutes(std::shared_ptr<kit_muduo::http::HttpServer> server)
{
#define XX(WORK_FUNC) \
    std::bind(&ProtocolHandler::WORK_FUNC, this, std::placeholders::_1, std::placeholders::_2)

    // 新增测试项协议
    server->Post("/protocols/add", XX(AddProtocol));

    // 上线/下线协议项
    server->Post("/protocols/:protocol_id/runtime_enabled", XX(LaunchAndWithdrawsProtocol));

    // 获取单个测试项协议
    server->Get("/protocols/:protocol_id", XX(SingleProtocol));
    // 恢复软删协议项目
    server->Post("/protocols/:protocol_id/restore", XX(RestoreProtocol));

    // 删除测试项协议
    server->Delete("/protocols/:protocol_id", XX(DelProtocol));

    // 重配置测试协议项
    server->Post("/protocols/:protocol_id/reconfig", XX(ReconfigProtocol));
    
    // 获取整个测试项协议列表 按细节拆分 不能全量返回
        // 按二进制 / 已确定 协议拆分为不同的VO数据结构
        // 不返回实际的Body数据
    server->Post("/protocols/list", XX(List));

    // 单独修改协议项某个细节
    server->Post("/protocols/:protocol_id/name", XX(DetailName));
    server->Post("/protocols/:protocol_id/details/cfg", XX(DetailCfg));
    server->Post("/protocols/:protocol_id/details/body", XX(DetailBody));

    // 单独获取协议项某个细节
    server->Get("/protocols/:protocol_id/details/cfg", XX(GetCfg));


    // DEBUG: 这个接口弃用
    // server->Get("/protocols/:protocol_id/details/tcp/common_fields", XX(QueryCommonFields)); //TCP专属
    
    // 单独获取协议项请求体配置
    // 将body格式和body数据合并查询
    server->Get("/protocols/:protocol_id/details/body_type", XX(GetProtocolBodyType));
    server->Get("/protocols/:protocol_id/details/body_data", XX(GetProtocolBodyData));
    
    // DEBUG: 这个接口弃用
    // server->Get("/protocols/:protocol_id/details/body_info", XX(GetProtocolBodyInfo));

    // server->Get("/protocols/:project_id/cnt", XX(ProtocolCnt));



#undef XX
}


static bool CheckProjectAccess(HttpContextPtr ctx, ProjectSvcInterface *project_svc, int64_t project_id, bool require_active, bool admin_only)
{
    auto current_user = CurrentUserFromContext(ctx);
    if(admin_only && !current_user.IsAdmin())
    {
        return false;
    }
    if(!project_svc || project_id <= 0)
    {
        return false;
    }
    auto project = project_svc->GetById(ctx, project_id);
    if(project.m_id <= 0)
    {
        return false;
    }
    if(require_active && project.m_status != ProjectStatus::kValid)
    {
        return false;
    }
    return current_user.IsAdmin() || project.m_userId == current_user.user_id;
}

static bool CheckProtocolAccess(HttpContextPtr ctx,
    ProtocolSvcInterface *protocol_svc,
    int64_t protocol_id,
    bool require_active,
    bool admin_only,
    ProtocolAccessInfo &access_info)
{
    if(!protocol_svc)
    {
        return false;
    }

    if(!protocol_svc->GetAccessInfo(ctx, protocol_id, access_info))
    {
        return false;
    }

    auto current_user = CurrentUserFromContext(ctx);

    // 需要管理员权限 但当前非管理员
    if(admin_only && !current_user.IsAdmin())
    {
        return false;
    }

    // 当前操作需要`未软删`，如果已处于软删则不允许操作
    if(require_active && (ProjectStatus::kValid != access_info.project_status || ProtocolStatus::kValid != access_info.protocol_status))
    {
        return false;
    }
    
    return current_user.IsAdmin() 
        || access_info.project_user_id == current_user.user_id;
}

static void WriteForbidden(HttpContextPtr ctx)
{
    auto resp = ctx->response();
    resp->setStateCode(StateCode::k403Forbidden);
    resp->body().setContentType(ContentType::kJsonType);
    resp->body().appendData(R"({"code": -403, "message": "forbidden", "data":{}})");
}

static bool ParseProtocolIdFromRoute(HttpContextPtr ctx, int64_t &protocol_id)
{
    try {
        protocol_id = std::stol(ctx->routeParam("protocol_id"));
        return protocol_id > 0;
    } catch(const std::exception &e) {
        PC_F_ERROR("route param transform fail! protocol_id=%ld, %s\n", protocol_id, e.what());
        return false;
    }
}

static bool TryParseProtocolSide(const std::string &side_str, ProtocolSide &side)
{
    if(side_str.empty())
    {
        return false;
    }
    try {
        const int32_t side_val = std::stoi(side_str);
        if(static_cast<int32_t>(ProtocolSide::kRequest) == side_val
            || static_cast<int32_t>(ProtocolSide::kResponse) == side_val)
        {
            side = static_cast<ProtocolSide>(side_val);
            return true;
        }
    } catch(const std::exception &e) {
        PC_F_ERROR("protocol side transform fail! side=%s, %s\n", side_str.c_str(), e.what());
    }
    return false;
}

static bool ParseProtocolSideFromQuery(HttpContextPtr ctx, ProtocolSide &side)
{
    if(TryParseProtocolSide(ctx->queryParam("side"), side))
    {
        return true;
    }
    return TryParseProtocolSide(ctx->queryParam("req_or_resp"), side);
}


void ProtocolHandler::AddProtocol(kit_muduo::TcpConnectionPtr conn, kit_muduo::HttpContextPtr ctx) noexcept
{
    auto req = ctx->request();
    auto resp = ctx->response();
    resp->setVersion(Version::kHttp11);
    resp->setStateCode(StateCode::k200Ok);
    resp->body().setContentType(ContentType::kJsonType);

    WriteOpResult write_result;
    AddProtocolReq request; // 表单

    // 自动根据req中的 content-type类型去解析对象
    bool ok = ctx->Bind(&request);
    if(!ok)
    {
        PJ_F_ERROR("body bind error! \n");

        WriteOpResponseHelper(ctx, write_result.allErr().failed(-200, "body parse error"));
        return;
    }

    // DTO转换 避免对外暴露领域模型Entity
    auto p = std::make_shared<kit_domain::Protocol>();
    p->m_id = -1;
    p->m_name = std::move(request.header.name);
    p->m_type = request.header.type;
    p->m_projectId = request.header.project_id;
    p->m_status = ProtocolStatus::kValid;
    p->m_configState = request.header.config_state;
    p->m_reqBodyType = request.header.req_body_type;
    p->m_respBodyType = request.header.resp_body_type;
    p->m_reqBodyDataStatus = request.protocol_req_body.empty() ? 0 : 1;
    p->m_respBodyDataStatus = request.protocol_resp_body.empty() ? 0 : 1;
    p->m_reqCfg = std::move(request.protocol_req_cfg);
    p->m_respCfg = std::move(request.protocol_resp_cfg);
    p->m_reqBodyData = std::move(request.protocol_req_body);
    p->m_respBodyData = std::move(request.protocol_resp_body);
    p->m_isEndian = request.header.is_endian;

    if(!CheckProjectAccess(ctx, pj_svc_.get(), p->m_projectId, true, false))
    {
        WriteForbidden(ctx);
        return;
    }

    ProtocolRuntimeResult pc_runtime_result;
    try 
    {
        pc_runtime_result = project_runtime_manager_->addProtocol(ctx, *p);
        if(!pc_runtime_result.ok())
        {
            PC_F_ERROR("protocol runtime operation error! pjId[%ld], code[%d]: %s\n", 
                p->m_projectId,
                static_cast<int32_t>(pc_runtime_result.status.code),
                pc_runtime_result.status.message.c_str());

            WriteOpResponseHelper(ctx, WriteOpResult::FromPcRuntimeResult(pc_runtime_result));
            return;
        }
    }
    catch(const std::exception& e)
    {
        PC_F_ERROR("protoctol add exception: %s \n", e.what());
        
        WriteOpResponseHelper(ctx, write_result.allErr().failed(-300, "service failed"));
        return;
    }

    
    WriteOpResponseHelper(ctx, WriteOpResult::FromPcRuntimeResult(pc_runtime_result), [&pc_runtime_result](auto& root){
        root["data"]["project_id"] = pc_runtime_result.snapshot.project_id;
        root["data"]["protocol_id"] = pc_runtime_result.snapshot.protocol_id;
        root["data"]["config_state"] = pc_runtime_result.snapshot.config_state;
    });
}

void ProtocolHandler::LaunchAndWithdrawsProtocol(kit_muduo::TcpConnectionPtr conn, kit_muduo::HttpContextPtr ctx) noexcept
{
    auto req = ctx->request();
    auto resp = ctx->response();
    resp->setVersion(Version::kHttp11);
    resp->setStateCode(StateCode::k200Ok);
    resp->body().setContentType(ContentType::kJsonType);

    WriteOpResult write_result;
    LaunchAndWithdrawsProtocolReq request;

    bool ok = ctx->Bind(&request);
    if(!ok)
    {
        PJ_F_ERROR("body bind error! \n");

        WriteOpResponseHelper(ctx, write_result.allErr().failed(-200, "body parse error"));
        return;
    }

    int64_t protocol_id = 0;
    if(!ParseProtocolIdFromRoute(ctx, protocol_id))
    {
        WriteOpResponseHelper(ctx, write_result.allErr().failed(-200, "query param fail"));
        return;
    }

    const ProtocolConfigState will_config_state = request.runtime_enabled;

    if(ProtocolConfigState::kOn != will_config_state && ProtocolConfigState::kOff != will_config_state)
    {
        WriteOpResponseHelper(ctx, write_result.allErr().failed(-200, "request param invalid"));
        return;
    }

    ProtocolAccessInfo access_info;
    if(!CheckProtocolAccess(ctx, svc_.get(), protocol_id, true, false, access_info))
    {
        WriteForbidden(ctx);
        return;
    }
    int64_t project_id = access_info.project_id;

    ProtocolRuntimeResult pc_runtime_result;
    try {


        if(ProtocolConfigState::kOn == will_config_state)
        {
            pc_runtime_result = project_runtime_manager_->enableProtocol(ctx, project_id, protocol_id);
        }
        else if(ProtocolConfigState::kOff == will_config_state)
        {
            pc_runtime_result = project_runtime_manager_->disableProtocol(ctx, project_id, protocol_id);
        }

        if(!pc_runtime_result.ok())
        {
            PC_F_ERROR("protocol runtime operation error! pjId[%ld], pcId[%ld], code[%d]: %s\n", 
                project_id, protocol_id,
                static_cast<int32_t>(pc_runtime_result.status.code),
                pc_runtime_result.status.message.c_str());
            WriteOpResponseHelper(ctx, WriteOpResult::FromPcRuntimeResult(pc_runtime_result));
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

    WriteOpResponseHelper(ctx, WriteOpResult::FromPcRuntimeResult(pc_runtime_result), [&pc_runtime_result](auto& root){
        root["data"]["project_id"] = pc_runtime_result.snapshot.project_id;
        root["data"]["protocol_id"] = pc_runtime_result.snapshot.protocol_id;
        root["data"]["config_state"] = pc_runtime_result.snapshot.config_state;
    });
}


void ProtocolHandler::DelProtocol(kit_muduo::TcpConnectionPtr conn, kit_muduo::HttpContextPtr ctx) noexcept
{
    auto req = ctx->request();
    auto resp = ctx->response();
    resp->setVersion(Version::kHttp11);
    resp->setStateCode(StateCode::k200Ok);
    resp->body().setContentType(ContentType::kJsonType);

    WriteOpResult write_result;

    int64_t protocol_id = 0;
    if(!ParseProtocolIdFromRoute(ctx, protocol_id))
    {
        WriteOpResponseHelper(ctx, write_result.failed(-200, "query param fail"));
        return;
    }

    ProtocolAccessInfo access_info;
    if(!CheckProtocolAccess(ctx, svc_.get(), protocol_id, true, false, access_info))
    {
        WriteForbidden(ctx);
        return;
    }
    int64_t project_id = access_info.project_id;

    ProtocolRuntimeResult pc_runtime_result;
    try 
    {
        pc_runtime_result = project_runtime_manager_->delProtocol(ctx, project_id, protocol_id);
        if(!pc_runtime_result.ok())
        {
            PC_F_ERROR("protocol runtime operation error! pjId[%ld], pcId[%ld], code[%d]: %s\n", 
                project_id, protocol_id,
                static_cast<int32_t>(pc_runtime_result.status.code),
                pc_runtime_result.status.message.c_str());
            WriteOpResponseHelper(ctx, WriteOpResult::FromPcRuntimeResult(pc_runtime_result));
            return;
        }
    }
    catch(const std::exception& e)
    {
        PJ_F_ERROR("service del exception: %s \n", e.what());

        WriteOpResponseHelper(ctx, write_result.failed(-300, "service failed"));
        return;
    }

    WriteOpResponseHelper(ctx, WriteOpResult::FromPcRuntimeResult(pc_runtime_result), [&pc_runtime_result](auto& root){
        root["data"]["project_id"] = pc_runtime_result.snapshot.project_id;
        root["data"]["protocol_id"] = pc_runtime_result.snapshot.protocol_id;
        root["data"]["config_state"] = pc_runtime_result.snapshot.config_state;
    });
}

void ProtocolHandler::ReconfigProtocol(kit_muduo::TcpConnectionPtr conn, kit_muduo::HttpContextPtr ctx) noexcept
{
    auto req = ctx->request();
    auto resp = ctx->response();
    resp->setVersion(Version::kHttp11);
    resp->setStateCode(StateCode::k200Ok);
    resp->body().setContentType(ContentType::kJsonType);

    WriteOpResult write_result;
    ReconfigProtocolReq request; // 表单

    PC_F_DEBUG("\n%s\n", req->body().toString().c_str());

    // 自动根据req中的 content-type类型去解析对象
    bool ok = ctx->Bind(&request);
    if(!ok)
    {
        PJ_F_ERROR("body bind error! \n");

        WriteOpResponseHelper(ctx, write_result.allErr().failed(-200, "body parse error"));
        return;
    }

    int64_t protocol_id = 0;
    if(!ParseProtocolIdFromRoute(ctx, protocol_id))
    {
        WriteOpResponseHelper(ctx, write_result.allErr().failed(-200, "query param fail"));
        return;
    }

    ProtocolAccessInfo access_info;
    if(!CheckProtocolAccess(ctx, svc_.get(), protocol_id, true, false, access_info))
    {
        WriteForbidden(ctx);
        return;
    }

    // DTO转换 避免对外暴露领域模型Entity
    auto p = std::make_shared<kit_domain::Protocol>();
    p->m_id = protocol_id;
    p->m_name = std::move(request.header.name);
    p->m_type = access_info.protocol_type;
    p->m_projectId = access_info.project_id;
    p->m_status = access_info.protocol_status;
    p->m_configState = request.header.config_state;
    p->m_reqBodyType = request.header.req_body_type;
    p->m_respBodyType = request.header.resp_body_type;
    p->m_reqBodyDataStatus = request.protocol_req_body.empty() ? 0 : 1;
    p->m_respBodyDataStatus = request.protocol_resp_body.empty() ? 0 : 1;
    p->m_reqCfg = std::move(request.protocol_req_cfg);
    p->m_respCfg = std::move(request.protocol_resp_cfg);
    p->m_reqBodyData = std::move(request.protocol_req_body);
    p->m_respBodyData = std::move(request.protocol_resp_body);
    p->m_isEndian = request.header.is_endian;

    ProtocolRuntimeResult pc_runtime_result;
    try 
    {
        pc_runtime_result = project_runtime_manager_->reconfigProtocol(ctx, *p);
        if(!pc_runtime_result.ok())
        {
            PC_F_ERROR("protocol runtime operation error! pjId[%ld], pcId[%ld], code[%d]: %s\n", 
                access_info.project_id, access_info.protocol_id,
                static_cast<int32_t>(pc_runtime_result.status.code),
                pc_runtime_result.status.message.c_str());
            WriteOpResponseHelper(ctx, WriteOpResult::FromPcRuntimeResult(pc_runtime_result));
            return;
        }
    }
    catch(const std::exception& e)
    {
        PC_F_ERROR("protoctol reconfig exception: %s \n", e.what());
        
        WriteOpResponseHelper(ctx, write_result.allErr().failed(-300, "service failed"));
        return;
    }

    WriteOpResponseHelper(ctx, WriteOpResult::FromPcRuntimeResult(pc_runtime_result), [&pc_runtime_result](auto& root){
        root["data"]["project_id"] = pc_runtime_result.snapshot.project_id;
        root["data"]["protocol_id"] = pc_runtime_result.snapshot.protocol_id;
        root["data"]["config_state"] = pc_runtime_result.snapshot.config_state;
    });
}


void ProtocolHandler::SingleProtocol(kit_muduo::TcpConnectionPtr conn, kit_muduo::HttpContextPtr ctx) noexcept
{
    auto req = ctx->request();
    auto resp = ctx->response();
    resp->setVersion(Version::kHttp11);
    resp->setStateCode(StateCode::k200Ok);
    resp->body().setContentType(ContentType::kJsonType);

    PC_DEBUG() << std::endl << req->body().toString() << std::endl;

    int64_t protocol_id = 0;
    try {
        // TODO boost万能转换
        protocol_id = std::stol(ctx->routeParam("protocol_id"));
        if(protocol_id <= 0)
            throw;

    } catch(const std::exception& e) {

        PJ_F_ERROR("query param transform fail! protocol_id=%d , %s\n", protocol_id, e.what());
        
        resp->body().appendData(R"({"code": -200, "message":"query param transform fail"})");
        return;
    }

    // DTO转换 避免对外暴露领域模型Entity
    Protocol protocol;
    // 查测试服务 信息
    try 
    {
        protocol = svc_->GetById(ctx, protocol_id);
        if(protocol.m_id < 0)
            throw std::runtime_error("protocol.id <= 0");
        if(!CheckProjectAccess(ctx, pj_svc_.get(), protocol.m_projectId, true, false))
        {
            WriteForbidden(ctx);
            return;
        }
    }
    catch(const std::exception& e)
    {
        PC_F_ERROR("service GetByUser exception: %s \n", e.what());
        resp->body().appendData(R"({"code": -300, "message":"service failed"})");
        return;
    }

    // 查询时不需要所有数据 返回
    // Body数据等实际需要查看时再进行请求查询
    nljson root;
    root["code"] = 0;// TODO domain错误码统一化 
    root["message"] = "success";
    root["data"].push_back(CovertProtocolVo(protocol));

    resp->body().appendData(root.dump());
    PC_DEBUG() << std::endl << root.dump(4) << std::endl;
}


void ProtocolHandler::List(kit_muduo::TcpConnectionPtr conn, kit_muduo::HttpContextPtr ctx) noexcept
{
    auto req = ctx->request();
    auto resp = ctx->response();
    resp->setVersion(Version::kHttp11);
    resp->setStateCode(StateCode::k200Ok);
    resp->body().setContentType(ContentType::kJsonType);

    ProtocolListReq request = {0}; //json

    PC_DEBUG() << std::endl << req->body().toString() << std::endl;

    // 自动根据req中的 content-type类型去解析对象
    bool ok = ctx->Bind(&request);
    if(!ok)
    {
        PC_F_ERROR("body bind error! \n");

        resp->body().appendData(R"({"code": -200, "message":"body parse error"})");
        return;
    }

    // DTO转换 避免对外暴露领域模型Entity
    std::vector<Protocol> protocols;
    if(!CheckProjectAccess(ctx, pj_svc_.get(), request.project_id, true, false))
    {
        WriteForbidden(ctx);
        return;
    }
    // 查测试服务 信息
    try 
    {
        auto current_user = CurrentUserFromContext(ctx);
        if(current_user.IsAdmin() && request.include_inactive)
        {
            protocols = svc_->GetByProject(ctx, request.project_id, ProtocolStatus::kValid, request.offset, request.limit);
            auto inactive_protocols = svc_->GetByProject(ctx, request.project_id, ProtocolStatus::kInvalid, 0, request.limit);
            protocols.insert(protocols.end(), inactive_protocols.begin(), inactive_protocols.end());
        }
        else
        {
            protocols = svc_->GetByProject(ctx, request.project_id, ProtocolStatus::kValid, request.offset, request.limit);
        }
    }
    catch(const std::exception& e)
    {
        PC_F_ERROR("service GetByUser exception: %s \n", e.what());
        resp->body().appendData(R"({"code": -300, "message":"service failed"})");
        return;
    }


    nljson root;
    root["code"] = 0;
    root["message"] = "success";
    root["data"] = CovertProtocolVos(protocols);

    resp->body().appendData(root.dump());

    PC_DEBUG() << std::endl << root.dump(4) << std::endl;
}


void ProtocolHandler::DetailName(kit_muduo::TcpConnectionPtr conn, kit_muduo::HttpContextPtr ctx) noexcept
{
    auto req = ctx->request();
    auto resp = ctx->response();
    resp->setVersion(Version::kHttp11);
    resp->setStateCode(StateCode::k200Ok);
    resp->body().setContentType(ContentType::kJsonType);

    WriteOpResult write_result;
    ProtocolDetailNameReq request;

    PC_DEBUG()  << std::endl << req->body().toString() << std::endl;

    // 自动根据req中的 content-type类型去解析对象
    bool ok = ctx->Bind(&request);
    if(!ok)
    {
        PC_F_ERROR("body bind error! \n");
        WriteOpResponseHelper(ctx, write_result.failed(-200, "body parse error"));
        return;
    }

    int64_t protocol_id = 0;
    if(!ParseProtocolIdFromRoute(ctx, protocol_id))
    {
        WriteOpResponseHelper(ctx, write_result.failed(-200, "query param fail"));
        return;
    }

    ProtocolAccessInfo access_info;
    if(!CheckProtocolAccess(ctx, svc_.get(), protocol_id, true, false, access_info))
    {
        WriteForbidden(ctx);
        return;
    }

    try {

        bool ok = svc_->UpdateName(ctx, protocol_id, request.name);
        if(!ok)
        {
            throw std::runtime_error(" UpdateName error");
        }

    } catch(const std::exception& e) {

        PC_F_ERROR("service UpdateName exception: %s \n", e.what());

        WriteOpResponseHelper(ctx, write_result.failed(-300, "service failed"));
        return;
    }

    WriteOpResponseHelper(ctx, write_result.persistedOk().success());
    return;
}



void ProtocolHandler::DetailCfg(kit_muduo::TcpConnectionPtr conn, kit_muduo::HttpContextPtr ctx) noexcept
{
    auto req = ctx->request();
    auto resp = ctx->response();
    resp->setVersion(Version::kHttp11);
    resp->setStateCode(StateCode::k200Ok);
    resp->body().setContentType(ContentType::kJsonType);

    WriteOpResult write_result;
    DetailCfgReq request;

    PC_DEBUG() << std::endl << req->body().toString() << std::endl;

    // 自动根据req中的 content-type类型去解析对象
    bool ok = ctx->Bind(&request);
    if(!ok)
    {
        PC_F_ERROR("body bind error! \n");

        WriteOpResponseHelper(ctx, write_result.failed(-200, "body parse error"));
        return;
    }

    int64_t protocol_id = 0;
    if(!ParseProtocolIdFromRoute(ctx, protocol_id))
    {
        WriteOpResponseHelper(ctx, write_result.failed(-200, "query param fail"));
        return;
    }

    const ProtocolSide side = request.side;

    ProtocolAccessInfo access_info;
    if(!CheckProtocolAccess(ctx, svc_.get(), protocol_id, true, false, access_info))
    {
        WriteForbidden(ctx);
        return;
    }
    int64_t project_id = access_info.project_id;


    ProtocolRuntimeResult pc_runtime_result;
    try {

        pc_runtime_result = project_runtime_manager_->updateProtocolCfg(ctx, project_id, protocol_id, side, request.cfg_data);
        if(!pc_runtime_result.ok())
        {
            PC_F_ERROR("protocol runtime operation error! pjId[%ld], pcId[%ld], code[%d]: %s\n", 
                access_info.project_id, access_info.protocol_id,
                static_cast<int32_t>(pc_runtime_result.status.code),
                pc_runtime_result.status.message.c_str());
            WriteOpResponseHelper(ctx, WriteOpResult::FromPcRuntimeResult(pc_runtime_result));
            return;
        }

    } catch(const std::exception& e) {

        PC_F_ERROR("service UpdateProtocolCfg exception: %s \n", e.what());

        WriteOpResponseHelper(ctx, write_result.failed(-300, "service failed"));
        return;
    }


    WriteOpResponseHelper(ctx, WriteOpResult::FromPcRuntimeResult(pc_runtime_result), [&pc_runtime_result](auto& root){
        root["data"]["project_id"] = pc_runtime_result.snapshot.project_id;
        root["data"]["protocol_id"] = pc_runtime_result.snapshot.protocol_id;
        root["data"]["config_state"] = pc_runtime_result.snapshot.config_state;
    });
}

void ProtocolHandler::DetailBody(kit_muduo::TcpConnectionPtr conn, kit_muduo::HttpContextPtr ctx) noexcept
{
    auto req = ctx->request();
    auto resp = ctx->response();
    resp->setVersion(Version::kHttp11);
    resp->setStateCode(StateCode::k200Ok);
    resp->body().setContentType(ContentType::kJsonType);

    WriteOpResult write_result;
    DetailReq request;
    // 注意 请求/响应 走同一个服务处理 url不同

    PC_DEBUG() << std::endl << req->body().toString() << std::endl;

    // 自动根据req中的 content-type类型去解析对象
    bool ok = ctx->Bind(&request);
    if(!ok)
    {
        PC_F_ERROR("body bind error! \n");

        WriteOpResponseHelper(ctx, write_result.failed(-200, "body parse error"));
        return;
    }

    int64_t protocol_id = 0;
    if(!ParseProtocolIdFromRoute(ctx, protocol_id))
    {
        WriteOpResponseHelper(ctx, write_result.failed(-200, "query param fail"));
        return;
    }

    const ProtocolSide side = request.header.side;
    const ProtocolBodyType body_type = request.header.body_type;
    const auto& body_data = request.cfg_data;

    // 用户权限校验
    ProtocolAccessInfo access_info;
    if(!CheckProtocolAccess(ctx, svc_.get(), protocol_id, true, false, access_info))
    {
        WriteForbidden(ctx);
        return;
    }
    int64_t project_id = access_info.project_id;

    ProtocolRuntimeResult pc_runtime_result;
    try  {

        pc_runtime_result = project_runtime_manager_->updateProtocolBody(ctx, project_id, protocol_id, side, body_type, body_data);
        if(!pc_runtime_result.ok())
        {
            PC_F_ERROR("protocol runtime operation error! pjId[%ld], pcId[%ld], code[%d]: %s\n", 
                access_info.project_id, access_info.protocol_id,
                static_cast<int32_t>(pc_runtime_result.status.code),
                pc_runtime_result.status.message.c_str());
            WriteOpResponseHelper(ctx, WriteOpResult::FromPcRuntimeResult(pc_runtime_result));
            return;
        }

    } catch(const std::exception& e) {
        PC_F_ERROR("service UpdateBody exception: %s \n", e.what());

        WriteOpResponseHelper(ctx, write_result.failed(-300, "service failed"));
        return;
    }

    WriteOpResponseHelper(ctx, WriteOpResult::FromPcRuntimeResult(pc_runtime_result), [&pc_runtime_result](auto& root){
        root["data"]["project_id"] = pc_runtime_result.snapshot.project_id;
        root["data"]["protocol_id"] = pc_runtime_result.snapshot.protocol_id;
        root["data"]["config_state"] = pc_runtime_result.snapshot.config_state;
    });
}



void ProtocolHandler::ProtocolCnt(kit_muduo::TcpConnectionPtr conn, kit_muduo::HttpContextPtr ctx) noexcept
{
    auto req = ctx->request();
    auto resp = ctx->response();
    resp->setVersion(Version::kHttp11);
    resp->setStateCode(StateCode::k200Ok);
    resp->body().setContentType(ContentType::kJsonType);

    PC_DEBUG() << std::endl << req->body().toString() << std::endl;


    int64_t project_id = 0;
    try {
        // TODO boost万能转换
        project_id = std::stol(ctx->routeParam("project_id"));
        if(project_id <= 0)
            throw;

    } catch(const std::exception& e) {

        PC_F_ERROR("query param transform fail! project_id=%d , %s\n", project_id, e.what());
        
        resp->body().appendData(R"({"code": -200, "message":"query param transform fail"})");
        return;
    }

    int32_t protocol_cnt = -1;
    if(!CheckProjectAccess(ctx, pj_svc_.get(), project_id, true, false))
    {
        WriteForbidden(ctx);
        return;
    }
    try 
    {
        protocol_cnt = svc_->GetProtocolCnt(ctx, project_id, ProtocolStatus::kValid);
        if(protocol_cnt < 0)
            throw;
    }
    catch(const std::exception& e)
    {
        PC_F_ERROR("service get protocol count failed: %s \n", e.what());
        resp->body().appendData(R"({"code": -300, "message":"service failed"})");
        return;
    }

    nljson root;
    root["code"] = 0;
    root["message"] = "success";
    root["data"]["protocol_cnt"] = protocol_cnt;


    resp->body().appendData(root.dump());
    PJ_DEBUG() << std::endl << root.dump(4) << std::endl;

}


void ProtocolHandler::GetCfg(kit_muduo::TcpConnectionPtr conn, kit_muduo::HttpContextPtr ctx) noexcept
{
    auto req = ctx->request();
    auto resp = ctx->response();
    resp->setVersion(Version::kHttp11);
    resp->setStateCode(StateCode::k200Ok);
    resp->body().setContentType(ContentType::kJsonType);

    int64_t protocol_id = 0;
    try {
        // TODO boost万能转换
        protocol_id = std::stol(ctx->routeParam("protocol_id"));
        if(protocol_id <= 0)
        {
            throw std::invalid_argument("quest param error");
        }

    } catch(const std::exception& e) {

        PJ_F_ERROR("query param transform fail! protocol_id=%d , %s\n", protocol_id, e.what());
        
        resp->body().appendData(R"({"code": -200, "message":"query param error"})");
        return;
    }

    ProtocolAccessInfo access_info;
    if(!CheckProtocolAccess(ctx, svc_.get(),  protocol_id, true, false, access_info))
    {
        WriteForbidden(ctx);
        return;
    }

    nljson protocol_cfg;
    try {

        protocol_cfg = svc_->GetCfgById(ctx, protocol_id);

    } catch(const std::exception& e){

        PC_F_ERROR("service GetCfg failed: %s \n", e.what());
        resp->body().appendData(R"({"code": -300, "message":"service failed"})");
        return;
    }

    nljson root;
    root["code"] = 0;
    root["message"] = "success";
    root["data"] = protocol_cfg;

    resp->body().appendData(root.dump());

    PC_DEBUG() << std::endl << root.dump(4) << std::endl;

}

void ProtocolHandler::QueryCommonFields(kit_muduo::TcpConnectionPtr conn, kit_muduo::HttpContextPtr ctx) noexcept
{
    auto req = ctx->request();
    auto resp = ctx->response();
    resp->setVersion(Version::kHttp11);
    resp->setStateCode(StateCode::k200Ok);
    resp->body().setContentType(ContentType::kJsonType);

    DetailReqHeader request;

    PC_DEBUG() << std::endl << req->body().toString() << std::endl;

    bool ok = ctx->Bind(&request);
    if(!ok)
    {
        PC_F_ERROR("body bind error! \n");

        resp->body().appendData(R"({"code": -200, "message":"body parse error"})");
        return;
    }

    int64_t protocol_id = 0;
    if(!ParseProtocolIdFromRoute(ctx, protocol_id))
    {
        resp->body().appendData(R"({"code": -200, "message":"query param fail"})");
        return;
    }

    ProtocolAccessInfo access_info;
    if(!CheckProtocolAccess(ctx, svc_.get(), protocol_id, true, false, access_info))
    {
        WriteForbidden(ctx);
        return;
    }

    if(ProtocolType::kCustomTcp != access_info.protocol_type)
    {
        resp->body().appendData(R"({"code": -200, "message":"request param error"})");
        return;
    }

    if(ProtocolSide::kRequest != request.side
        && ProtocolSide::kResponse !=  request.side)
    {
        resp->body().appendData(R"({"code": -200, "message":"request param error"})");
        return;
    }


    nljson common_fields_json;
    try 
    {

        common_fields_json = svc_->GetTcpCommonFieldsById(ctx, protocol_id, request.side);

    }
    catch(const std::exception& e)
    {
        PC_F_ERROR("service GetTcpCommonFieldsById failed: %s \n", e.what());
        resp->body().appendData(R"({"code": -300, "message":"service failed"})");
        return;
    }

    nljson root;
    root["code"] = 0;
    root["message"] = "success";
    root["data"] = common_fields_json;

    resp->body().appendData(root.dump());

    PC_DEBUG() << std::endl << root.dump(4) << std::endl;

}


void ProtocolHandler::GetProtocolBodyType(kit_muduo::TcpConnectionPtr conn, kit_muduo::HttpContextPtr ctx) noexcept
{
    auto req = ctx->request();
    auto resp = ctx->response();
    resp->setVersion(Version::kHttp11);
    resp->setStateCode(StateCode::k200Ok);
    resp->body().setContentType(ContentType::kJsonType);

    DetailReqHeader request; //json

    PC_DEBUG() << std::endl << req->body().toString() << std::endl;

    int64_t protocol_id = 0;
    if(!ParseProtocolIdFromRoute(ctx, protocol_id))
    {
        resp->body().appendData(R"({"code": -200, "message":"query param fail"})");
        return;
    }

    ProtocolSide side = ProtocolSide::kRequest;
    if(!ParseProtocolSideFromQuery(ctx, side))
    {
        // 兼容旧调用方，允许 POST JSON body 传 side。
        bool ok = ctx->Bind(&request);
        if(!ok)
        {
            PC_F_ERROR("body bind error! \n");
            resp->body().appendData(R"({"code": -200, "message":"body parse error"})");
            return;
        }
        side = request.side;
    }

    ProtocolAccessInfo access_info;
    if(!CheckProtocolAccess(ctx, svc_.get(), protocol_id, true, false, access_info))
    {
        WriteForbidden(ctx);
        return;
    }

    if(ProtocolSide::kRequest != side
        && ProtocolSide::kResponse !=  side)
    {
        resp->body().appendData(R"({"code": -100, "message":"request param error"})");
        return;
    }


    ProtocolBodyType body_type;
    try 
    {

        body_type =  svc_->GetBodyTypeById(ctx, protocol_id, side);
        if(body_type <= ProtocolBodyType::kUnknown || body_type > ProtocolBodyType::kBinary)
        {
            throw std::logic_error("GetBodyTypeById failed");
        }
    }
    catch(const std::exception& e)
    {
        PC_F_ERROR("service GetBodyTypeById exception: %s \n", e.what());
        resp->body().appendData(R"({"code": -300, "message":"service failed"})");
        return;
    }
    nljson root;
    root["code"] = 0;
    root["message"] = "success";
    root["data"]["body_type"] = ProtocolBodyTypeToString(body_type);

    resp->body().appendData(root.dump());
    PC_DEBUG() << root.dump(4) << std::endl;
}


void ProtocolHandler::GetProtocolBodyData(kit_muduo::TcpConnectionPtr conn, kit_muduo::HttpContextPtr ctx) noexcept
{
    auto req = ctx->request();
    auto resp = ctx->response();
    resp->setVersion(Version::kHttp11);
    resp->setStateCode(StateCode::k200Ok);

    DetailReqHeader request; //json

    PC_DEBUG() << std::endl << req->body().toString() << std::endl;

    int64_t protocol_id = 0;
    if(!ParseProtocolIdFromRoute(ctx, protocol_id))
    {
        resp->body().appendData(R"({"code": -200, "message":"query param fail"})");
        return;
    }

    ProtocolSide side = ProtocolSide::kRequest;
    if(!ParseProtocolSideFromQuery(ctx, side))
    {
        // 兼容旧调用方，允许 POST JSON body 传 side。
        bool ok = ctx->Bind(&request);
        if(!ok)
        {
            PC_F_ERROR("body bind error! \n");
            resp->body().appendData(R"({"code": -200, "message":"body parse error"})");
            return;
        }
        side = request.side;
    }

    ProtocolAccessInfo access_info;
    if(!CheckProtocolAccess(ctx, svc_.get(), protocol_id, true, false, access_info))
    {
        WriteForbidden(ctx);
        return;
    }

    if(ProtocolSide::kRequest != side
        && ProtocolSide::kResponse != side)
    {
        resp->body().appendData(R"({"code": -100, "message":"request param error"})");
        return;
    }

    std::vector<char> body_data;
    try 
    {
        bool ok = svc_->GetBodyDataById(ctx, protocol_id, side, body_data);
        if(!ok)
        {
            throw std::logic_error("GetBodyDataById failed");
        }
    }
    catch(const std::exception& e)
    {
        PC_F_ERROR("service GetBodyDataById exception: %s \n", e.what());
        
        resp->body().appendData(R"({"code": -300, "message":"service failed"})");
        return;
    }
    // TODO 这里可能存在分块传输问题

    if(body_data.size()) 
    {
        resp->body().setContentType(ContentType::kOctetStream);
        resp->body().appendData(body_data);
    }
    else
    {
        resp->setStateCode(StateCode::k204NoContent);
    }
    PC_DEBUG() << resp->toString() << std::endl;

}


void ProtocolHandler::GetProtocolBodyInfo(kit_muduo::TcpConnectionPtr conn, kit_muduo::HttpContextPtr ctx) noexcept
{
    auto req = ctx->request();
    auto resp = ctx->response();
    resp->setVersion(Version::kHttp11);
    resp->setStateCode(StateCode::k200Ok);
    resp->body().setContentType(ContentType::kJsonType);

    DetailReqHeader request; //json

    PC_DEBUG() << std::endl << req->body().toString() << std::endl;

    // 自动根据req中的 content-type类型去解析对象
    bool ok = ctx->Bind(&request);
    if(!ok)
    {
        PC_F_ERROR("body bind error! \n");

        resp->body().appendData(R"({"code": -200, "message":"body parse error"})");
        return;
    }

    int64_t protocol_id = 0;
    if(!ParseProtocolIdFromRoute(ctx, protocol_id))
    {
        resp->body().appendData(R"({"code": -200, "message":"query param fail"})");
        return;
    }

    ProtocolAccessInfo access_info;
    if(!CheckProtocolAccess(ctx, svc_.get(), protocol_id, true, false, access_info))
    {
        WriteForbidden(ctx);
        return;
    }

    if(ProtocolSide::kRequest != request.side
        && ProtocolSide::kResponse !=  request.side)
    {
        resp->body().appendData(R"({"code": -100, "message":"request param error"})");
        return;
    }

    ProtocolBodyType body_type;
    std::vector<char> body_data;
    try {

        ok = svc_->GetBodyInfoById(ctx, protocol_id, request.side, body_type, body_data);
        if(!ok)
            throw std::logic_error("GetBodyDataById failed");
    } catch(const std::exception& e) {

        PC_F_ERROR("service GetBodyDataById exception: %s \n", e.what());
        
        resp->body().appendData(R"({"code": -300, "message":"service failed"})");
        return;
    }

    // TODO 这里可能存在分块传输问题

    resp->body().setContentType(ContentType::kMultiForm);
    // DEBUG 手动输入一部分数据 先测试一下
    resp->body().appendData(
R"(------WebKitFormBoundaryNQJ0YrO2NeaUfM7n
Content-Disposition: form-data;name="body_type"
Content-Type: text/plain

json
------WebKitFormBoundaryNQJ0YrO2NeaUfM7n
Content-Disposition: form-data; name="body_data"
Content-Type: application/octet-stream

{"key": "val"}
------WebKitFormBoundaryNQJ0YrO2NeaUfM7n--
)");
    // DEBUG 手动输入一部分数据


    PC_DEBUG() << resp->toString() << std::endl;

}

void ProtocolHandler::RestoreProtocol(kit_muduo::TcpConnectionPtr conn, kit_muduo::HttpContextPtr ctx) noexcept
{
    auto resp = ctx->response();
    resp->setVersion(Version::kHttp11);
    resp->setStateCode(StateCode::k200Ok);
    resp->body().setContentType(ContentType::kJsonType);

    WriteOpResult write_result;

    int64_t protocol_id = 0;
    try {
        protocol_id = std::stol(ctx->routeParam("protocol_id"));
    } catch(const std::exception &) {

        WriteOpResponseHelper(ctx, write_result.allErr().failed(-200, "query param fail"));
        return;
    }

    ProtocolAccessInfo access_info;
    if(!CheckProtocolAccess(ctx, svc_.get(), protocol_id, false, true, access_info))
    {
        WriteForbidden(ctx);
        return;
    }

    // 恢复协议必须要测试服务先恢复
    if(ProjectStatus::kValid != access_info.project_status)
    {
        WriteOpResponseHelper(ctx, write_result.allErr().failed(-300, "service failed"));
        return;
    }

    // 恢复协议必须要测试服务处于停止状态
    if(ProjectRuntimeState::kRunning == access_info.project_runtime_state)
    {
        WriteOpResponseHelper(ctx, write_result.allErr().failed(-300, "请先停止测试服务后再恢复协议项"));
        return;
    }

    if(!svc_->ReCover(ctx, protocol_id)
        || !svc_->UpdateConfigState(ctx, protocol_id, ProtocolConfigState::kOff))
    {
        WriteOpResponseHelper(ctx, write_result.allErr().failed(-300, "service failed"));
        return;
    }

    WriteOpResponseHelper(ctx, write_result.persistedOk().success(), [&access_info](auto &root){
        root["data"]["project_id"] = access_info.project_id;
        root["data"]["protocol_id"] = access_info.protocol_id;
        root["data"]["config_state"] = ProtocolConfigState::kOff;
    });
}



}   // namespace kit_domain
