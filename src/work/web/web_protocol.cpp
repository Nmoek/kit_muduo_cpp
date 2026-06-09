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


#include <functional>
#include <memory>
#include <stdexcept>

using namespace kit_muduo;
using namespace kit_muduo::http;
using nljson = nlohmann::json;

namespace kit_domain {

/***************Body解析临时变量定义 其他模块不允许引用**************** */

struct AddProtocolReqHeader {
    std::string name;                          // 测试协议名称
    ProtocolType type;                           // 测试协议类型 HTTP/TCP
    int64_t project_id;                        // 所属测试服务Id
    ProtocolBodyType req_body_type;                 // 请求协议体类型 json/xml/plain
    ProtocolBodyType resp_body_type;                 // 响应协议体类型 json/xml/plain
    ProtocolConfigState runtime_enabled;         // 协议项是否同步上线
    
    // TCP特有
    int32_t is_endian;                          // 是否进行大小端转换 1进行 0不进行 频繁查询更新字段
    
    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(AddProtocolReqHeader, name, type, project_id, req_body_type, resp_body_type, runtime_enabled, is_endian)
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

    NLOHMANN_DEFINE_TYPE_INTRUSIVE(AddProtocolReq, header, protocol_req_cfg, protocol_resp_cfg, protocol_req_body, protocol_req_body) 

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

struct LaunchAndWithdrawsProtocolReq {
    int64_t id;
    int64_t project_id;
    ProtocolConfigState runtime_enabled;

    NLOHMANN_DEFINE_TYPE_INTRUSIVE(LaunchAndWithdrawsProtocolReq, id, project_id, runtime_enabled)

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
    int64_t id;
    int64_t project_id;

    NLOHMANN_DEFINE_TYPE_INTRUSIVE(DelProtocolReq, id, project_id)

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
    int64_t id;
    std::string name;

    NLOHMANN_DEFINE_TYPE_INTRUSIVE(ProtocolDetailNameReq, id, name)

    static bool from_multi_form(const MultiFormConvert::PartMap &parts, ProtocolDetailNameReq &req)
    {
        PC_WARN() << "ProtocolDetailNameReq dont support from_multi_form" << std::endl;
        return false;
    }
};

/*复用 */


struct DetailCfgReq {
    int64_t                 id;             // 协议项id
    int64_t                 project_id;     // 协议项所属测试服务id
    ProtocolType            type;    // 协议类型
    ProtocolSide            side;    // 校验到底是请求配置还是响应配置
    nljson                  cfg_data{nljson::object()};       // 协议配置数据 必须是json

    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(DetailCfgReq, id, project_id, type, side, cfg_data)

    static bool from_multi_form(const MultiFormConvert::PartMap &parts, DetailCfgReq &req)
    {
        PC_WARN() << "DetailCfgReq dont support from_multi_form" << std::endl;
        return false;
    }
};


struct DetailReqHeader {
    int64_t id;             // 协议项id
    int64_t project_id;     // 协议项所属测试服务id
    ProtocolSide side;    // 校验到底是请求配置还是响应配置
    ProtocolType type;       // 协议项种类
    ProtocolBodyType body_type;  // body数据格式类型

    // 带默认值 = 未解析到的字段也不会抛异常
    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(DetailReqHeader, id, project_id, side, type, body_type)

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
    server->Post("/protocols/runtime_enabled", XX(LaunchAndWithdrawsProtocol));

    // 获取单个测试项协议
    server->Get("/protocols/:protocol_id", XX(SingleProtocol));
    // 恢复软删协议项目
    server->Post("/protocols/:protocol_id/restore", XX(RestoreProtocol));

    // 删除测试项协议
    server->Post("/protocols/del", XX(DelProtocol));
    
    // 获取整个测试项协议列表 按细节拆分 不能全量返回
        // 按二进制 / 已确定 协议拆分为不同的VO数据结构
        // 不返回实际的Body数据
    server->Post("/protocols/list", XX(List));

    // 单独修改协议项某个细节
    server->Post("/protocols/details/name", XX(DetailName)); 
    server->Post("/protocols/details/cfg", XX(DetailCfg));
    server->Post("/protocols/details/body", XX(DetailBody));

    // 单独获取协议项某个细节
    server->Get("/protocols/:protocol_id/details/cfg", XX(GetCfg));


    // DEBUG: 这个接口弃用
    // server->Post("/protocols/details/tcp/common_fields", XX(QueryCommonFields)); //TCP专属
    
    // 单独获取协议项请求体配置
    // 将body格式和body数据合并查询
    server->Post("/protocols/details/body_type", XX(GetProtocolBodyType));
    server->Post("/protocols/details/body_data", XX(GetProtocolBodyData));
    
    // DEBUG: 这个接口弃用
    // server->Post("/protocols/details/body_info", XX(GetProtocolBodyInfo));

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
    p->m_configState = request.header.runtime_enabled;
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

    int64_t protocol_id = -1;
    try 
    {
        // 生成对应协议种类的报文
        protocol_id = svc_->Add(ctx, *p);
        if(protocol_id < 0)
        {
            throw std::runtime_error("protocol_id < 0");
        }
    }
    catch(const std::exception& e)
    {
        PC_F_ERROR("protoctol add exception: %s \n", e.what());
        
        WriteOpResponseHelper(ctx, write_result.allErr().failed(-300, "service failed"));
        return;
    }
    // 更新一下主键id
    p->m_id = protocol_id;

    if(ProtocolConfigState::kOff == p->m_configState)
    {
        WriteOpResponseHelper(ctx, write_result.persistedOk().success());
        return;
    }
    
    // TODO 同步上线时上线失败 上线状态要回滚
    auto project_server = project_runtime_manager_->findServer(p->m_projectId);
    if(!project_server)
    {
        PC_F_ERROR("project not found! %d \n",p->m_projectId);

        WriteOpResponseHelper(ctx, write_result.persistedOk().failed(-300, "project not found"));
        return;
    }
    
    // 工厂模式生成协议项对象ProtocolItem
    std::shared_ptr<ProtocolItem> protocol_item = nullptr;
    try {

        protocol_item = ProtocolItemFactory::Create(p, project_server);
        if(!protocol_item)
        {
            throw std::runtime_error("ProtocolItem create error");
        }
    }catch(const std::exception& e) {

        //删除该协议项
        if(!svc_->Del(ctx, protocol_id))
        {
            PC_F_ERROR("rollback del ProtocolItem faild! pcId[%ld], pjId[%ld], type[%d] \n",protocol_id, p->m_projectId, static_cast<int32_t>(p->m_type));
            write_result.persistedOk();
        }

        PC_F_ERROR("create ProtocolItem faild, %s! pcId[%ld], pjId[%ld], type[%d] \n", e.what(), protocol_id, p->m_projectId, static_cast<int32_t>(p->m_type));

        WriteOpResponseHelper(ctx, write_result.failed(-300, "protocolitem create failed"));
        return;
    }

    // 需要向对应的测试服务器上注册协议
    auto result = InvokeOnLoopSync(project_server->getLoop(), 1000, [project_server, protocol_item]() {
        PC_F_DEBUG("AddProtocolItem: [%d][%s][%d] \n", protocol_item->getId(), protocol_item->getName().c_str(), protocol_item->getProjectId());

        return project_server->AddProtocolItem(protocol_item);
    });
    if(!result.ok())
    {
        //删除该协议项
        if(!svc_->Del(ctx, protocol_id))
        {
            PC_F_ERROR("rollback del ProtocolItem faild! pcId[%ld], pjId[%ld], type[%d] \n",protocol_id, p->m_projectId, static_cast<int32_t>(p->m_type));
            // 回滚失败再把状态补回ok
            write_result.persistedOk();

        }

        PC_F_ERROR("InvokeOnLoopSync::AddProtocolItem error[%d]: %s! pjId[%d], pcId[%d] \n", result.error.toInt(), result.error.toMsg().c_str(), project_server->getProjectId(), protocol_item->getId());

        WriteOpResponseHelper(ctx, write_result.failed(-300, "protocolitem runtime failed"));
        return;
    }

    WriteOpResponseHelper(ctx, write_result.allOk().success(), [protocol_id](auto& root){
        root["data"]["protocol_id"] = protocol_id;
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

    int64_t protocol_id = request.id;
    int64_t project_id = request.project_id;
    const ProtocolConfigState will_config_state = request.runtime_enabled;

    if(protocol_id <= 0
        || project_id <= 0
        || (ProtocolConfigState::kOn != will_config_state && ProtocolConfigState::kOff != will_config_state))
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
    if(access_info.project_id != project_id)
    {
        WriteOpResponseHelper(ctx, write_result.allErr().failed(-200, "request param invalid"));
        return;
    }

    ok = false;
    std::function<RuntimeResult<void>()> loop_invoke_func = nullptr;
    std::shared_ptr<kit_domain::ProjectServer> project_server = nullptr;
    try {


        if(ProtocolConfigState::kOn == will_config_state)
        {
            if(ProtocolConfigState::kOn != access_info.protocol_config_state)
            {
                auto p = std::make_shared<Protocol>(svc_->GetById(ctx,  protocol_id));
                if(!p || p->m_id <= 0)
                {
                    throw std::runtime_error("GetById error");
                }

                project_server = project_runtime_manager_->findServer(p->m_projectId);
                if(!project_server)
                {
                    WriteOpResponseHelper(ctx, write_result.allErr().failed(-300, "project server not found"));
                    return;
                }

                // 工厂模式创建协议项
                auto protocol_item = ProtocolItemFactory::Create(p, project_server);
                if (!protocol_item) 
                {
                    PJ_F_ERROR("create ProtocolItem faild! pcId[%ld], pjId[%ld] type[%d] project_id[%d] \n", protocol_id,project_id, static_cast<int32_t>(p->m_type));

                    WriteOpResponseHelper(ctx, write_result.failed(-300, "failed to create protocl item"));
                    return;
                }

                loop_invoke_func = [project_server, 
                    protocol_item](){
                    return project_server->AddProtocolItem(protocol_item);
                };

            }
            else
            {
                PC_F_WARN("protocol item enabled! pcId[%ld] pjId[%ld] \n", protocol_id, project_id);
                WriteOpResponseHelper(ctx, write_result.allOk().success());
                return;
            }
           
        }
        else if(ProtocolConfigState::kOff == will_config_state)
        {
            if(ProtocolConfigState::kOff != access_info.protocol_config_state)
            {
                project_server = project_runtime_manager_->findServer(access_info.project_id);
                if(!project_server)
                {
                    WriteOpResponseHelper(ctx, write_result.allErr().failed(-300, "project server not found"));
                    return;
                }

                loop_invoke_func = [project_server, protocol_id](){
                    return project_server->DelProtocolItem(protocol_id);
                };

            }
            else
            {
                PC_F_WARN("protocol item disabled! pcId[%ld] pjId[%ld] \n", protocol_id, project_id);

                WriteOpResponseHelper(ctx, write_result.allOk().success());
                return;
            }
        }

        auto result = InvokeOnLoopSync(project_server->getLoop(), 1000, [loop_invoke_func]() {
            if(loop_invoke_func)
            {
                return loop_invoke_func();
            }
            RuntimeResult<void> r;
            r.error.set(RuntimeError::kInvalidArgument);
            return r;
        });
        if(!result.ok())
        {
            PC_F_ERROR("InvokeOnLoopSync::AddProtocolItem error[%d]: %s! pjId[%d], pcId[%d] \n", result.error.toInt(), result.error.toMsg().c_str(), project_id, protocol_id);

            WriteOpResponseHelper(ctx, write_result.failed(-300, "protocolitem runtime failed"));
            return;
        }

    } catch(const std::exception& e) {

        PJ_F_ERROR("project server runtime exception: %s \n", e.what());
        
        WriteOpResponseHelper(ctx, write_result.failed(-300, "service failed"));
        return;
    }
    // 更新数据库
    ok = svc_->UpdateConfigState(ctx, protocol_id, will_config_state);
    if(!ok)
    {
        PJ_F_ERROR("UpdateRuntimeEnabled error! pjId[%ld] pcId[%ld] will_enabled[%d]\n", project_id, protocol_id,static_cast<int32_t>(will_config_state));

        WriteOpResponseHelper(ctx, write_result.runOk().failed(-300, "service failed"));
        return;
    }

    // 2. 需要和开启的服务进行通信（通信方式如何选择?)，需要进行增删改协议项
    // 2.1 线程通信  复用loop队列
    // 2.2 RPC通信
    // 2.3 注册Web API

    WriteOpResponseHelper(ctx, write_result.allOk().success());
}


void ProtocolHandler::DelProtocol(kit_muduo::TcpConnectionPtr conn, kit_muduo::HttpContextPtr ctx) noexcept
{
    auto req = ctx->request();
    auto resp = ctx->response();
    resp->setVersion(Version::kHttp11);
    resp->setStateCode(StateCode::k200Ok);
    resp->body().setContentType(ContentType::kJsonType);

    WriteOpResult write_result;
    DelProtocolReq request; //json

    PC_DEBUG() << std::endl << req->body().toString() << std::endl;

    // 自动根据req中的 content-type类型去解析对象
    bool ok = ctx->Bind(&request);
    if(!ok)
    {
        PJ_F_ERROR("body bind error! \n");

        WriteOpResponseHelper(ctx, write_result.failed(-200, "body parse error"));
        return;
    }

    // DTO转换 避免对外暴露领域模型Entity
    int64_t protocol_id = request.id;
    int64_t project_id = request.project_id;

    ProtocolAccessInfo access_info;
    if(!CheckProtocolAccess(ctx, svc_.get(), protocol_id, true, false, access_info))
    {
        WriteForbidden(ctx);
        return;
    }
    if(access_info.project_id != project_id)
    {
        WriteOpResponseHelper(ctx, write_result.failed(-200, "request param invalid"));
        return;
    }

    try 
    {
        ok = svc_->Del(ctx, protocol_id);
        if(!ok)
        {
            throw std::runtime_error("del error");
        }
    }
    catch(const std::exception& e)
    {
        PJ_F_ERROR("service del exception: %s \n", e.what());

        WriteOpResponseHelper(ctx, write_result.failed(-300, "service del failed"));
        return;
    }

    if(ProtocolConfigState::kOn != access_info.protocol_config_state)
    {
        WriteOpResponseHelper(ctx, write_result.persistedOk().success());
        return;
    }
  
    auto project_server = project_runtime_manager_->findServer(project_id);
    if(!project_server)
    {
        PC_F_ERROR("project not found! %d \n", project_id);

        WriteOpResponseHelper(ctx, write_result.persistedOk().failed(-300, "project not found"));
        return;
    }
        
    // 删除测试服务器上协议项
    auto result = InvokeOnLoopSync(project_server->getLoop(), 1000, [project_server, project_id, protocol_id]()  {
        PC_F_DEBUG("DelProtocolItem: pjId[%d], pcId[%d] \n", project_id, protocol_id);
        return project_server->DelProtocolItem(protocol_id);
    });
    if(!result.ok())
    {
        PC_F_ERROR("InvokeOnLoopSync::DelProtocolItem error: %d:%s! project_id[%d], protocol_id[%d] \n", result.error.toInt(), result.error.toMsg().c_str(), project_server->getProjectId(), protocol_id);

        // 回滚删除动作
        if(!svc_->ReCover(ctx, protocol_id))
        {
            PC_F_ERROR("protocol re-ReCover error! %d \n", protocol_id);
            write_result.persistedOk();
        }

        WriteOpResponseHelper(ctx, write_result.failed(-300, "protocolitem del failed"));
        return;
    }

    WriteOpResponseHelper(ctx, write_result.allOk().success());
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

    ProtocolAccessInfo access_info;
    if(!CheckProtocolAccess(ctx, svc_.get(), request.id, true, false, access_info))
    {
        WriteForbidden(ctx);
        return;
    }

    try {

        bool ok = svc_->UpdateName(ctx, request.id, request.name);
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

    int64_t protocol_id = request.id;
    int64_t project_id = request.project_id;
    const ProtocolSide side = request.side;
    const ProtocolType type = request.type;

    ProtocolAccessInfo access_info;
    if(!CheckProtocolAccess(ctx, svc_.get(), protocol_id, true, false, access_info))
    {
        WriteForbidden(ctx);
        return;
    }

    if(access_info.project_id != project_id
        || access_info.protocol_type != type)
    {
        WriteOpResponseHelper(ctx, write_result.failed(-100, "request param error"));
        return;
    }


    if(ProtocolSide::kRequest != side
        && ProtocolSide::kResponse != side)
    {
        WriteOpResponseHelper(ctx, write_result.failed(-100, "request param error"));
        return;
    }
    
    // 特别注意: 传入的json配置项得是object
    if(!request.cfg_data.is_object())
    {
        PC_F_ERROR("cfg json is not object! %s \n", request.cfg_data.type_name());

        WriteOpResponseHelper(ctx, write_result.failed(-100, "request param error"));
        return;
    }

    // 查询当前协议是否是上线状态
    const ProtocolConfigState runtime_enabeld = access_info.protocol_config_state;
    
    ok = false;
    std::shared_ptr<ProtocolItem> protocol_item = nullptr;

    nljson old_all_cfg_json;
    nljson old_cfg_json;
    nljson new_cfg_json;

    using SvcUpdateFunc = decltype(&ProtocolSvcInterface::UpdateReqCfg);
    SvcUpdateFunc svc_func = nullptr;
    using RuntimeUpdateFunc = decltype(&ProjectServer::UpdateReqCfgProtocolItem);
    RuntimeUpdateFunc runtime_func = nullptr;
    const char* cfg_key = nullptr;

    try {

        // 1. 查出旧配置
        old_all_cfg_json = svc_->GetCfgById(ctx, request.id);

        if(ProtocolSide::kRequest == side)
        {
            cfg_key = "req_cfg";
            svc_func = &ProtocolSvcInterface::UpdateReqCfg;
            runtime_func = &ProjectServer::UpdateReqCfgProtocolItem;
        }
        else if(ProtocolSide::kResponse == side)
        {
            cfg_key = "resp_cfg";
            svc_func = &ProtocolSvcInterface::UpdateRespCfg;
            runtime_func = &ProjectServer::UpdateRespCfgProtocolItem;
        }

        // 2. 把局部新配置/全新配置和旧配置"并集"合并
        old_cfg_json = new_cfg_json = old_all_cfg_json.at(cfg_key);
        new_cfg_json.merge_patch(request.cfg_data);

        PJ_F_DEBUG("cfg json merge: pjId[%d], side[%d]: %s\n",request.id, static_cast<int32_t>(request.side), new_cfg_json.dump(4).c_str());

        if(new_cfg_json.empty())
        {
            WriteOpResponseHelper(ctx, write_result.failed(-100, "request param error"));
            return;
        }

        // TODO 从新考虑 对配置项进行校验 下面runtime添加已经存在校验逻辑

        // 3. 写数据库
        ok = std::invoke(svc_func, svc_.get(), ctx, protocol_id, type, new_cfg_json);
        if(!ok)
        {
            PC_F_ERROR("protocolitem UpdateProtocolCfg error\n");
            throw std::runtime_error("update cfg error");
        }

    } catch(const std::exception& e) {

        PC_F_ERROR("service UpdateProtocolCfg exception: %s \n", e.what());

        WriteOpResponseHelper(ctx, write_result.failed(-300, "service failed"));
        return;
    }

    if(ProtocolConfigState::kOn != runtime_enabeld)
    {
        WriteOpResponseHelper(ctx, write_result.persistedOk().success());
        return;
    }

    // 4. runtime 设置
    // 更新服务器上的协议配置信息
    // 简单起见 不要做局部更新 直接整体替换
    auto project_server = project_runtime_manager_->findServer(access_info.project_id);
    if(!project_server || !project_server->isActive())
    {
        PC_F_WARN("project not found! %d \n", project_id);

        // 数据库回滚
        ok = std::invoke(svc_func, svc_.get(), ctx, protocol_id, type, old_cfg_json);
        if(!ok)
        {
            PC_F_ERROR("protocolitem rollback UpdateProtocolCfg error\n");
            write_result.persistedOk();
        }

        WriteOpResponseHelper(ctx, write_result.failed(-300, "project not found"));
        return;
    } 
    
    auto result = InvokeOnLoopSync(project_server->getLoop(), 1000,
    [project_server,
        runtime_func,
        protocol_id,
        project_id,
        side,
        new_cfg_json](){

        PC_F_DEBUG("UpdateProtocolCfgItem: pcId[%ld], pjId[%ld], side[%d] \n", protocol_id, project_id, static_cast<int32_t>(side));

        return std::invoke(runtime_func, project_server.get(), protocol_id, new_cfg_json);

    });
    if(!result.ok())
    {
        // 数据库回滚
        ok = std::invoke(svc_func, svc_.get(), ctx, protocol_id, type, old_cfg_json);
        if(!ok)
        {
            PC_F_ERROR("protocolitem rollback UpdateProtocolCfg error\n");
            write_result.persistedOk();
        }

        PC_F_ERROR("InvokeOnLoopSync::UpdateProtocolCfgItem error: %d:%s! project_id[%d], protocol_id[%d] side[%d] \n", result.error.toInt(), result.error.toMsg().c_str(), project_server->getProjectId(), protocol_id, static_cast<int32_t>(side));

        WriteOpResponseHelper(ctx, write_result.failed(-300, "protocolitem update cfg failed"));
        return;
    }

    WriteOpResponseHelper(ctx, write_result.allOk().success());
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

    int64_t protocol_id = request.header.id;
    int64_t project_id = request.header.project_id;
    const ProtocolSide side = request.header.side;
    const ProtocolType type = request.header.type;
    const ProtocolBodyType body_type = request.header.body_type;

    // 用户权限校验
    ProtocolAccessInfo access_info;
    if(!CheckProtocolAccess(ctx, svc_.get(), protocol_id, true, false, access_info))
    {
        WriteForbidden(ctx);
        return;
    }
    if(access_info.project_id != project_id
        || access_info.protocol_type != type)
    {
        WriteOpResponseHelper(ctx, write_result.failed(-100, "request param error"));
        return;
    }

    if((ProtocolSide::kRequest != side
        && ProtocolSide::kResponse !=  side) 
        || (body_type <= ProtocolBodyType::kUnknown || body_type >= ProtocolBodyType::kMax))
    {
        WriteOpResponseHelper(ctx, write_result.failed(-100, "request param error"));
        return;
    }

    const ProtocolConfigState config_state = access_info.protocol_config_state;

    ok = false;
    ProtocolBodyType old_body_type;
    std::vector<char> old_body_data;
    try  {

        ok = svc_->GetBodyInfoById(ctx,request.header.id, request.header.side, old_body_type, old_body_data);
        if(!ok)
        {
            throw std::runtime_error("GetBodyInfoById error");
        }

        ok = svc_->UpdateBody(ctx, request.header.id, request.header.side, body_type, request.cfg_data);
        if(!ok)
        {
            throw std::runtime_error("UpdateBody error");
        }

    } catch(const std::exception& e) {
        PC_F_ERROR("service UpdateBody exception: %s \n", e.what());

        WriteOpResponseHelper(ctx, write_result.failed(-300, "service failed"));
        return;
    }

    if(ProtocolConfigState::kOn != config_state)
    {
        WriteOpResponseHelper(ctx, write_result.persistedOk(). success());
        return;
    }

    auto project_server = project_runtime_manager_->findServer(project_id);
    if(!project_server)
    {
        PC_F_WARN("project not found! %d \n", project_id);

        // 回滚
        ok = svc_->UpdateBody(ctx, request.header.id, request.header.side, old_body_type, old_body_data);
        if(!ok)
        {
            PC_F_ERROR("protocolitem rollback UpdateBody error\n");
            write_result.persistedOk();
        }

        WriteOpResponseHelper(ctx, write_result.failed(-300, "project not found"));
        return;
    }

    auto result = InvokeOnLoopSync(project_server->getLoop(), 1000, 
    [protocol_id,
        project_id,
        side,
        project_server,
        body_type,
        data = std::move(request.cfg_data)](){

        PC_F_DEBUG("UpdateBodyProtocolItem: protocol_id[%d] project_id[%d] side[%d] body_type[%d]\n", protocol_id, project_id, static_cast<int32_t>(side), body_type);

        if(ProtocolSide::kRequest == side)
        {
            return project_server->UpdateReqBodyProtocolItem(protocol_id, body_type, data);
        }
        else if(ProtocolSide::kResponse == side)
        {
            return project_server->UpdateRespBodyProtocolItem(protocol_id, body_type, data);
        }
        return RuntimeResult<void>{
            RuntimeError(RuntimeError::kProtocolTypeMismatch)
        };
    });
    if(!result.ok())
    {
        PC_F_ERROR("InvokeOnLoopSync::UpdateBodyProtocolItem error: %d:%s! project_id[%d], protocol_id[%d] \n", result.error.toInt(), result.error.toMsg().c_str(), project_server->getProjectId(), protocol_id);

        // 回滚
        ok = svc_->UpdateBody(ctx, request.header.id, request.header.side, old_body_type, old_body_data);
        if(!ok)
        {
            PC_F_ERROR("protocolitem rollback UpdateBody error\n");
            write_result.persistedOk();
        }

        WriteOpResponseHelper(ctx, write_result. failed(-300, "protocolitem update body failed"));
        return;
    }

    WriteOpResponseHelper(ctx, write_result.allOk(). success());
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

    ProtocolAccessInfo access_info;
    if(!CheckProtocolAccess(ctx, svc_.get(), request.id, true, false, access_info))
    {
        WriteForbidden(ctx);
        return;
    }

    if(access_info.project_id != request.project_id
        || ProtocolType::kCustomTcp != access_info.protocol_type)
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

        common_fields_json = svc_->GetTcpCommonFieldsById(ctx, request.id, request.side);

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

    // 自动根据req中的 content-type类型去解析对象
    bool ok = ctx->Bind(&request);
    if(!ok)
    {
        PC_F_ERROR("body bind error! \n");

        resp->body().appendData(R"({"code": -200, "message":"body parse error"})");
        return;
    }

    int64_t protocol_id = request.id;
    const ProtocolSide side = request.side;

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

    // 自动根据req中的 content-type类型去解析对象
    bool ok = ctx->Bind(&request);
    if(!ok)
    {
        PC_F_ERROR("body bind error! \n");

        resp->body().appendData(R"({"code": -200, "message":"body parse error"})");
        return;
    }

    int64_t protocol_id = request.id;
    const ProtocolSide side = request.side;

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
        ok = svc_->GetBodyDataById(ctx, protocol_id, side, body_data);
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

    ProtocolAccessInfo access_info;
    if(!CheckProtocolAccess(ctx, svc_.get(), request.id, true, false, access_info))
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

        ok = svc_->GetBodyInfoById(ctx, request.id, request.side, body_type, body_data);
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
    if(CheckProtocolAccess(ctx, svc_.get(), protocol_id, false, true, access_info))
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

    WriteOpResponseHelper(ctx, write_result.persistedOk().success());
}



}   // namespace kit_domain
