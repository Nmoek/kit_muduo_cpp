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
#include "net/http/http_content_codec.h"
#include "net/http/multiform.h"
#include "service/svc_project.h"
#include "service/svc_protocol.h"
#include "web/protocol_vo.h"
#include "web/web_common.h"
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

};

// 重配置请求结构 复用新增
using ReconfigProtocolReq = AddProtocolReq;

inline void from_multiform(const MultiForm &form, AddProtocolReq &req)
{
    kit_muduo::http::ThrowIfFailed(DecodeMultiPartHelper(
        form.at("protocol_cfg_header"),
        req.header,
        {kit_muduo::http::ContentCodecFormat::kJson}));

    kit_muduo::http::ThrowIfFailed(DecodeMultiPartHelper(
        form.at("protocol_req_cfg"),
        req.protocol_req_cfg,
        {kit_muduo::http::ContentCodecFormat::kJson}));

    kit_muduo::http::ThrowIfFailed(DecodeMultiPartHelper(
        form.at("protocol_resp_cfg"),
        req.protocol_resp_cfg,
        {kit_muduo::http::ContentCodecFormat::kJson}));

    kit_muduo::http::ThrowIfFailed(DecodeMultiPartToRaw(
        form.at("protocol_req_body"),
        req.protocol_req_body));

    kit_muduo::http::ThrowIfFailed(DecodeMultiPartToRaw(
        form.at("protocol_resp_body"),
        req.protocol_resp_body));
}


struct LaunchAndWithdrawsProtocolReq {
    ProtocolConfigState runtime_enabled;

    NLOHMANN_DEFINE_TYPE_INTRUSIVE(LaunchAndWithdrawsProtocolReq, runtime_enabled)
};

/**
 * @brief List 用于Body解析
 */
struct ProtocolListReq {
    int64_t                  project_id;      // 所属测试服务id
    ProtocolStatus           status{ProtocolStatus::kValid};  // 状态
    bool                     include_inactive{false}; // 管理员列表是否包含已删除协议项
    int32_t                  offset;          // 页码
    int32_t                  limit;           // 页大小
    
    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(ProtocolListReq, project_id, status, include_inactive, offset, limit)
};


struct ProtocolDetailNameReq {
    std::string name;

    NLOHMANN_DEFINE_TYPE_INTRUSIVE(ProtocolDetailNameReq, name)
};

/*复用 */


struct DetailCfgReq {
    ProtocolSide     side;         // 校验到底是请求配置还是响应配置
    nljson           cfg_data;     // 协议配置数据 必须是json

    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(DetailCfgReq, side, cfg_data)
};


struct DetailReqHeader {
    ProtocolSide side;    // 校验到底是请求配置还是响应配置
    ProtocolBodyType body_type;  // body数据格式类型

    // 带默认值 = 未解析到的字段也不会抛异常
    NLOHMANN_DEFINE_TYPE_INTRUSIVE(DetailReqHeader, side, body_type)
};

struct DetailReq {
    DetailReqHeader header;
    std::vector<char> cfg_data;

    NLOHMANN_DEFINE_TYPE_INTRUSIVE(DetailReq, header, cfg_data)

};


inline void from_multiform(const MultiForm &form, DetailReq &req)
{
    kit_muduo::http::ThrowIfFailed(DecodeMultiPartHelper(
        form.at("detail_header"),
        req.header,
        {kit_muduo::http::ContentCodecFormat::kJson}));

    kit_muduo::http::ThrowIfFailed(DecodeMultiPartToRaw(
        form.at("detail_cfg_data"),
        req.cfg_data));

}


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


static void WriteProtocolJsonError(HttpContextPtr ctx, int32_t code, const std::string &message)
{
    WriteJsonError(ctx, code, message, true);
}


void ProtocolHandler::AddProtocol(kit_muduo::TcpConnectionPtr conn, kit_muduo::HttpContextPtr ctx) noexcept
{
    auto req = ctx->request();
    auto resp = ctx->response();
    resp->setVersion(Version::kHttp11);
    resp->setStateCode(StateCode::k200Ok);
    resp->setContentMeta(MakeContentMeta(KnownMediaType::kApplicationJson));

    WriteOpResult write_result;
    AddProtocolReq request; // 表单

    auto bind_result = ctx->bindMultipart(request);
    if(!bind_result.ok)
    {
        PJ_F_ERROR("body bind error: %s\n", bind_result.message.c_str());

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
    resp->setContentMeta(MakeContentMeta(KnownMediaType::kApplicationJson));

    WriteOpResult write_result;
    LaunchAndWithdrawsProtocolReq request;

    auto bind_result = ctx->bindJson(request);
    if(!bind_result.ok)
    {
        PJ_F_ERROR("body bind error: %s\n", bind_result.message.c_str());

        WriteOpResponseHelper(ctx, write_result.allErr().failed(-200, "body parse error"));
        return;
    }

    int64_t protocol_id = 0;
    if(!ParseRouteInt64(ctx, "protocol_id", protocol_id))
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
    resp->setContentMeta(MakeContentMeta(KnownMediaType::kApplicationJson));

    WriteOpResult write_result;

    int64_t protocol_id = 0;
    if(!ParseRouteInt64(ctx, "protocol_id", protocol_id))
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
    resp->setContentMeta(MakeContentMeta(KnownMediaType::kApplicationJson));

    WriteOpResult write_result;
    ReconfigProtocolReq request; // 表单

    PC_F_DEBUG("\n%s\n", req->bodyString().c_str());

    auto bind_result = ctx->bindMultipart(request);
    if(!bind_result.ok)
    {
        PJ_F_ERROR("body bind error: %s\n", bind_result.message.c_str());

        WriteOpResponseHelper(ctx, write_result.allErr().failed(-200, "body parse error"));
        return;
    }

    int64_t protocol_id = 0;
    if(!ParseRouteInt64(ctx, "protocol_id", protocol_id))
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
    // p->m_status = access_info.protocol_status;
    // p->m_configState = request.header.config_state;
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
    resp->setContentMeta(MakeContentMeta(KnownMediaType::kApplicationJson));

    PC_DEBUG() << std::endl << req->bodyString() << std::endl;

    int64_t protocol_id = 0;
    if(!ParseRouteInt64(ctx, "protocol_id", protocol_id))
    {
        WriteProtocolJsonError(ctx, -200, "query param transform fail");
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
        WriteProtocolJsonError(ctx, -300, "service failed");
        return;
    }

    // 查询时不需要所有数据 返回
    // Body数据等实际需要查看时再进行请求查询
    nljson root;
    root["code"] = 0;// TODO domain错误码统一化 
    root["message"] = "success";
    root["data"].push_back(CovertProtocolVo(protocol));

    WriteJsonResponse(ctx, root);
    PC_DEBUG() << std::endl << root.dump(4) << std::endl;
}


void ProtocolHandler::List(kit_muduo::TcpConnectionPtr conn, kit_muduo::HttpContextPtr ctx) noexcept
{
    auto req = ctx->request();
    auto resp = ctx->response();
    resp->setVersion(Version::kHttp11);
    resp->setStateCode(StateCode::k200Ok);
    resp->setContentMeta(MakeContentMeta(KnownMediaType::kApplicationJson));

    ProtocolListReq request; //json

    PC_DEBUG() << std::endl << req->bodyString() << std::endl;

    auto bind_result = ctx->bindJson(request);
    if(!bind_result.ok)
    {
        PC_F_ERROR("body bind error: %s\n", bind_result.message.c_str());

        WriteProtocolJsonError(ctx, -200, "body parse error");
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
        WriteProtocolJsonError(ctx, -300, "service failed");
        return;
    }


    nljson root;
    root["code"] = 0;
    root["message"] = "success";
    root["data"] = CovertProtocolVos(protocols);

    WriteJsonResponse(ctx, root);

    PC_DEBUG() << std::endl << root.dump(4) << std::endl;
}


void ProtocolHandler::DetailName(kit_muduo::TcpConnectionPtr conn, kit_muduo::HttpContextPtr ctx) noexcept
{
    auto req = ctx->request();
    auto resp = ctx->response();
    resp->setVersion(Version::kHttp11);
    resp->setStateCode(StateCode::k200Ok);
    resp->setContentMeta(MakeContentMeta(KnownMediaType::kApplicationJson));

    WriteOpResult write_result;
    ProtocolDetailNameReq request;

    PC_DEBUG()  << std::endl << req->bodyString() << std::endl;

    auto bind_result = ctx->bindJson(request);
    if(!bind_result.ok)
    {
        PC_F_ERROR("body bind error: %s\n", bind_result.message.c_str());
        WriteOpResponseHelper(ctx, write_result.failed(-200, "body parse error"));
        return;
    }

    int64_t protocol_id = 0;
    if(!ParseRouteInt64(ctx, "protocol_id", protocol_id))
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
    resp->setContentMeta(MakeContentMeta(KnownMediaType::kApplicationJson));

    WriteOpResult write_result;
    DetailCfgReq request;

    PC_DEBUG() << std::endl << req->bodyString() << std::endl;

    auto bind_result = ctx->bindJson(request);
    if(!bind_result.ok)
    {
        PC_F_ERROR("body bind error: %s\n", bind_result.message.c_str());

        WriteOpResponseHelper(ctx, write_result.failed(-200, "body parse error"));
        return;
    }

    int64_t protocol_id = 0;
    if(!ParseRouteInt64(ctx, "protocol_id", protocol_id))
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
    resp->setContentMeta(MakeContentMeta(KnownMediaType::kApplicationJson));

    WriteOpResult write_result;
    DetailReq request;
    // 注意 请求/响应 走同一个服务处理 url不同

    PC_DEBUG() << std::endl << req->bodyString() << std::endl;

    auto bind_result = ctx->bindMultipart(request);
    if(!bind_result.ok)
    {
        PC_F_ERROR("body bind error: %s\n", bind_result.message.c_str());

        WriteOpResponseHelper(ctx, write_result.failed(-200, "body parse error"));
        return;
    }

    int64_t protocol_id = 0;
    if(!ParseRouteInt64(ctx, "protocol_id", protocol_id))
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
    resp->setContentMeta(MakeContentMeta(KnownMediaType::kApplicationJson));

    PC_DEBUG() << std::endl << req->bodyString() << std::endl;


    int64_t project_id = 0;
    if(!ParseRouteInt64(ctx, "project_id", project_id))
    {
        WriteProtocolJsonError(ctx, -200, "query param transform fail");
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
        WriteProtocolJsonError(ctx, -300, "service failed");
        return;
    }

    nljson root;
    root["code"] = 0;
    root["message"] = "success";
    root["data"]["protocol_cnt"] = protocol_cnt;


    WriteJsonResponse(ctx, root);
    PJ_DEBUG() << std::endl << root.dump(4) << std::endl;

}


void ProtocolHandler::GetCfg(kit_muduo::TcpConnectionPtr conn, kit_muduo::HttpContextPtr ctx) noexcept
{
    auto req = ctx->request();
    auto resp = ctx->response();
    resp->setVersion(Version::kHttp11);
    resp->setStateCode(StateCode::k200Ok);
    resp->setContentMeta(MakeContentMeta(KnownMediaType::kApplicationJson));

    int64_t protocol_id = 0;
    if(!ParseRouteInt64(ctx, "protocol_id", protocol_id))
    {
        WriteProtocolJsonError(ctx, -200, "query param error");
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
        WriteProtocolJsonError(ctx, -300, "service failed");
        return;
    }

    nljson root;
    root["code"] = 0;
    root["message"] = "success";
    root["data"] = protocol_cfg;

    WriteJsonResponse(ctx, root);

    PC_DEBUG() << std::endl << root.dump(4) << std::endl;

}

void ProtocolHandler::QueryCommonFields(kit_muduo::TcpConnectionPtr conn, kit_muduo::HttpContextPtr ctx) noexcept
{
    auto req = ctx->request();
    auto resp = ctx->response();
    resp->setVersion(Version::kHttp11);
    resp->setStateCode(StateCode::k200Ok);
    resp->setContentMeta(MakeContentMeta(KnownMediaType::kApplicationJson));

    DetailReqHeader request;

    PC_DEBUG() << std::endl << req->bodyString() << std::endl;

    auto bind_result = ctx->bindJson(request);
    if(!bind_result.ok)
    {
        PC_F_ERROR("body bind error: %s\n", bind_result.message.c_str());

        WriteProtocolJsonError(ctx, -200, "body parse error");
        return;
    }

    int64_t protocol_id = 0;
    if(!ParseRouteInt64(ctx, "protocol_id", protocol_id))
    {
        WriteProtocolJsonError(ctx, -200, "query param fail");
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
        WriteProtocolJsonError(ctx, -200, "request param error");
        return;
    }

    if(ProtocolSide::kRequest != request.side
        && ProtocolSide::kResponse !=  request.side)
    {
        WriteProtocolJsonError(ctx, -200, "request param error");
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
        WriteProtocolJsonError(ctx, -300, "service failed");
        return;
    }

    nljson root;
    root["code"] = 0;
    root["message"] = "success";
    root["data"] = common_fields_json;

    WriteJsonResponse(ctx, root);

    PC_DEBUG() << std::endl << root.dump(4) << std::endl;

}


void ProtocolHandler::GetProtocolBodyType(kit_muduo::TcpConnectionPtr conn, kit_muduo::HttpContextPtr ctx) noexcept
{
    auto req = ctx->request();
    auto resp = ctx->response();
    resp->setVersion(Version::kHttp11);
    resp->setStateCode(StateCode::k200Ok);
    resp->setContentMeta(MakeContentMeta(KnownMediaType::kApplicationJson));

    PC_DEBUG() << std::endl << req->bodyString() << std::endl;

    int64_t protocol_id = 0;
    if(!ParseRouteInt64(ctx, "protocol_id", protocol_id))
    {
        WriteProtocolJsonError(ctx, -200, "query param fail");
        return;
    }

    ProtocolSide side = ProtocolSide::kRequest;
    if(!ParseProtocolSideFromQuery(ctx, side))
    {
        WriteProtocolJsonError(ctx, -200, "query param fail");
        return;
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
        WriteProtocolJsonError(ctx, -100, "request param error");
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
        WriteProtocolJsonError(ctx, -300, "service failed");
        return;
    }
    nljson root;
    root["code"] = 0;
    root["message"] = "success";
    root["data"]["body_type"] = ProtocolBodyTypeToString(body_type);

    WriteJsonResponse(ctx, root);
    PC_DEBUG() << root.dump(4) << std::endl;
}


void ProtocolHandler::GetProtocolBodyData(kit_muduo::TcpConnectionPtr conn, kit_muduo::HttpContextPtr ctx) noexcept
{
    auto req = ctx->request();
    auto resp = ctx->response();
    resp->setVersion(Version::kHttp11);
    resp->setStateCode(StateCode::k200Ok);

    PC_DEBUG() << std::endl << req->bodyString() << std::endl;

    int64_t protocol_id = 0;
    if(!ParseRouteInt64(ctx, "protocol_id", protocol_id))
    {
        WriteProtocolJsonError(ctx, -200, "query param fail");
        return;
    }

    ProtocolSide side = ProtocolSide::kRequest;
    if(!ParseProtocolSideFromQuery(ctx, side))
    {
        WriteProtocolJsonError(ctx, -200, "query param fail");
        return;
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
        WriteProtocolJsonError(ctx, -100, "request param error");
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
        
        WriteProtocolJsonError(ctx, -300, "service failed");
        return;
    }
    // TODO 这里可能存在分块传输问题

    if(body_data.size()) 
    {
        resp->setOctetStream(std::vector<uint8_t>(body_data.begin(), body_data.end()));
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
    resp->setContentMeta(MakeContentMeta(KnownMediaType::kApplicationJson));

    DetailReqHeader request; //json

    PC_DEBUG() << std::endl << req->bodyString() << std::endl;

    // 自动根据req中的 content-type类型去解析对象
    auto bind_result = ctx->bindJson(request);
    if(!bind_result.ok)
    {
        PC_F_ERROR("body bind error: %s\n", bind_result.message.c_str());

        WriteProtocolJsonError(ctx, -200, "body parse error");
        return;
    }

    int64_t protocol_id = 0;
    if(!ParseRouteInt64(ctx, "protocol_id", protocol_id))
    {
        WriteProtocolJsonError(ctx, -200, "query param fail");
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
        WriteProtocolJsonError(ctx, -100, "request param error");
        return;
    }

    ProtocolBodyType body_type;
    std::vector<char> body_data;
    try {

        bool ok = svc_->GetBodyInfoById(ctx, protocol_id, request.side, body_type, body_data);
        if(!ok)
            throw std::logic_error("GetBodyDataById failed");
    } catch(const std::exception& e) {

        PC_F_ERROR("service GetBodyDataById exception: %s \n", e.what());
        
        WriteProtocolJsonError(ctx, -300, "service failed");
        return;
    }

    // TODO 这里可能存在分块传输问题

    auto multipart_meta = MakeContentMeta(KnownMediaType::kMultipartFormData);
    SetContentTypeParam(multipart_meta, "boundary", "----WebKitFormBoundaryNQJ0YrO2NeaUfM7n");
    resp->setContentMeta(std::move(multipart_meta));
    // DEBUG 手动输入一部分数据 先测试一下
    resp->appendBodyData(
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
    resp->setContentMeta(MakeContentMeta(KnownMediaType::kApplicationJson));

    WriteOpResult write_result;

    int64_t protocol_id = 0;
    if(!ParseRouteInt64(ctx, "protocol_id", protocol_id))
    {
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
