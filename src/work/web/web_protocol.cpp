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

using namespace kit_muduo;
using namespace kit_muduo::http;
using nljson = nlohmann::json;

namespace kit_domain {

/***************Body解析临时变量定义 其他模块不允许引用**************** */

struct AddProtocolReqHeader {
    std::string name;                          // 测试协议名称
    std::string type;                           // 测试协议类型 HTTP/TCP
    int64_t project_id;                        // 所属测试服务Id
    std::string req_body_type;                 // 请求协议体类型 json/xml/plain
    std::string resp_body_type;                 // 响应协议体类型 json/xml/plain
    
    // TCP特有
    int32_t is_endian;                          // 是否进行大小端转换 1进行 0不进行 频繁查询更新字段
    
    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(AddProtocolReqHeader, name, type, project_id, req_body_type, resp_body_type, is_endian)
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
        
        // 必填
        auto it = parts.find("protocol_cfg_header");
        if(it == parts.end()) 
        {
            PC_F_ERROR("multiform name: protocol_cfg_header not found! \n");
            return false;
        }
        req.header = nljson::parse(it->second.data).get<AddProtocolReqHeader>();

        it = parts.find("protocol_req_cfg");
        if(it != parts.end())
        {
            req.protocol_req_cfg = nljson::parse(it->second.data);
        }
        else
        {
            PC_F_WARN("multiform name: protocol_req_cfg not found! \n");
        }

        it = parts.find("protocol_resp_cfg");
        if(it != parts.end())
        {
            req.protocol_resp_cfg = nljson::parse(it->second.data);
        }
        else
        {
            PC_F_WARN("multiform name: protocol_resp_cfg not found! \n");
        }

        it = parts.find("protocol_req_body");
        if(it != parts.end())
        {
            req.protocol_req_body = std::move(it->second.data);
        }
        else
        {
            PC_F_WARN("multiform name: protocol_req_body not found! \n");
        }

        it = parts.find("protocol_resp_body");
        if(it != parts.end())
        {
            req.protocol_resp_body = std::move(it->second.data);
        }
        else
        {
            PC_F_WARN("multiform name: protocol_resp_body not found! \n");
        }
        return true;
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
    int32_t                  offset;          // 页码
    int32_t                  limit;           // 页大小
    
    NLOHMANN_DEFINE_TYPE_INTRUSIVE(ProtocolListReq, project_id, offset, limit)

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
    int32_t                 req_or_resp;    // 校验到底是请求配置还是响应配置
    nljson                  cfg_data{nljson::object()};       // 协议配置数据 必须是json

    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(DetailCfgReq, id, project_id, req_or_resp, cfg_data)

    static bool from_multi_form(const MultiFormConvert::PartMap &parts, DetailCfgReq &req)
    {
        PC_WARN() << "DetailCfgReq dont support from_multi_form" << std::endl;
        return false;
    }
};


struct DetailReqHeader {
    int64_t id;             // 协议项id
    int64_t project_id;     // 协议项所属测试服务id
    int32_t req_or_resp;    // 校验到底是请求配置还是响应配置
    std::string type;       // 协议项种类
    std::string body_type;  // body数据格式类型

    // 带默认值 = 未解析到的字段也不会抛异常
    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(DetailReqHeader, id, project_id, req_or_resp, type, body_type)

    static bool from_multi_form(const MultiFormConvert::PartMap &parts, DetailReqHeader &req)
    {
        PC_WARN() << "DetailReqHeader dont support from_multi_form" << std::endl;
        return false;
    }
};

struct DetailReq {
    DetailReqHeader header;
    std::vector<char> cfg_data;

    enum {
        kRequest     = 1,
        kResponse    = 2,
    };

    NLOHMANN_DEFINE_TYPE_INTRUSIVE(DetailReq, header, cfg_data)

    static bool from_multi_form(const MultiFormConvert::PartMap &parts, DetailReq &req)
    {
        auto it = parts.find("detail_header");
        if(it == parts.end()) 
        {
            PC_F_ERROR("multiform name: detail_req_header not found! \n");
            return false;
        }
        req.header = std::move(nljson::parse(it->second.data).get<DetailReqHeader>());

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
ProtocolHandler::ProtocolHandler(std::shared_ptr<ProtocolSvcInterface> svc)
    :_svc(svc)
{ }

ProtocolHandler::~ProtocolHandler()
{ }

ProtocolHandler* ProtocolHandler::Instance(std::shared_ptr<ProtocolSvcInterface> svc)
{
    static ProtocolHandler h(svc);
    return &h;
}

void ProtocolHandler::RegisterRoutes(std::shared_ptr<kit_muduo::http::HttpServer> server)
{
#define XX(WORK_FUNC) \
    std::bind(&ProtocolHandler::WORK_FUNC, this, std::placeholders::_1, std::placeholders::_2)

    // 新增测试项协议
    server->Post("/protocols/add", XX(AddProtocol));

    // 获取单个测试项协议
    server->Get("/protocols/:protocol_id", XX(SingleProtocol));

    // 删除测试项协议
    server->Post("/protocols/del", XX(DelProtocol));
    
    // 获取整个测试项协议列表 按细节拆分 不能全量返回
        // 按二进制 / 已确定 协议拆分为不同的VO数据结构
        // 不返回实际的Body数据
    server->Post("/protocols/list", XX(List));

    // 单独修改协议项某个细节
    server->Post("/protocols/details/name", XX(DetailName)); // TODO 增加[?key1=value2&key2=val2]参数风格
    server->Post("/protocols/details/cfg", XX(DetailCfg));
    server->Post("/protocols/details/body", XX(DetailBody));

    // 单独获取协议项某个细节
    server->Get("/protocols/:protocol_id/details/cfg", XX(GetCfg));


    server->Post("/protocols/details/tcp/common_fields", XX(QueryCommonFields)); //TCP专属
    
    // 单独获取协议项请求体配置
    // 将body格式和body数据合并查询
    server->Post("/protocols/details/body_type", XX(GetProtocolBodyType));
    server->Post("/protocols/details/body_data", XX(GetProtocolBodyData));
    
    server->Post("/protocols/details/body_info", XX(GetProtocolBodyInfo));

    // server->Get("/protocols/:project_id/cnt", XX(ProtocolCnt));



#undef XX
}


void ProtocolHandler::AddProtocol(kit_muduo::TcpConnectionPtr conn, kit_muduo::HttpContextPtr ctx) noexcept
{
    auto req = ctx->request();
    auto resp = ctx->response();
    resp->setVersion(Version::kHttp11);
    resp->setStateCode(StateCode::k200Ok);
    resp->body().setContentType(ContentType::kJsonType);

    AddProtocolReq request; // 表单

    // PC_DEBUG() << std::endl << req->body().toString() << std::endl;

    // 自动根据req中的 content-type类型去解析对象
    bool ok = ctx->Bind(&request);
    if(!ok)
    {
        PJ_F_ERROR("body bind error! \n");
        resp->body().appendData(R"({"code": -200, "message":"body parse error"})");
        return;
    }

    // TODO: 协议添加去重？？
    // DTO转换 避免对外暴露领域模型Entity
    auto p = std::make_shared<kit_domain::Protocol>();
    p->m_id = -1;
    p->m_name = std::move(request.header.name);
    p->m_type = ProtocolTypeFromString(request.header.type);
    p->m_projectId = request.header.project_id;
    p->m_status = ProtocolStatus::ACTIVE;
    p->m_reqBodyType = ProtocolBodyTypeFromString(request.header.req_body_type);
    p->m_respBodyType = ProtocolBodyTypeFromString(request.header.resp_body_type);
    p->m_reqBodyDataStatus = request.protocol_req_body.empty() ? 0 : 1;
    p->m_respBodyDataStatus = request.protocol_resp_body.empty() ? 0 : 1;
    p->m_reqCfg = std::move(request.protocol_req_cfg);
    p->m_respCfg = std::move(request.protocol_resp_cfg);
    p->m_reqBodyData = std::move(request.protocol_req_body);
    p->m_respBodyData = std::move(request.protocol_resp_body);
    p->m_isEndian = request.header.is_endian;

    int64_t protocol_id = -1;
    try 
    {
        // 生成对应协议种类的报文
        protocol_id = _svc->Add(ctx, *p);
        if(protocol_id < 0)
            throw std::runtime_error("protocol_id < 0");
    }
    catch(const std::exception& e)
    {
        PC_F_ERROR("protoctol add exception: %s \n", e.what());
        // TODO: 自定义业务错误码统一处理
        resp->body().appendData(R"({"code": -300, "message":"service failed"})");
        return;
    }
    // 更新一下主键id
    p->m_id = protocol_id;


    //1. 先以http server进行调试 后续将tcp加入后再决定如何统一接口
    //2. TODO: 还要考虑客户端接口设计
    //3. TODO 接口重写 获取测试服务信息 要么从库中获取，要么从缓存获取
    auto project_service = GetApp()->FindServer(p->m_projectId);
    if(!project_service)
    {
        //TODO: 兜底措施 没创建需要重新创建对应的测试服务
        PC_F_WARN("project not found! %d \n",p->m_projectId);
        resp->body().appendData(R"({"code": -300, "message":"project not found"})");
        return;
    }

    // 工厂模式生成协议项对象ProtocolItem
    std::shared_ptr<ProtocolItem> protocol_item = nullptr;
    try {

        protocol_item = ProtocolItemFactory::Create(p, project_service);
        if(!protocol_item)
            throw;
    }catch(const std::exception& e) {

        //TODO 需要删除该协议项

        PC_F_ERROR("create ProtocolItem faild, %s! protocol_type[%d] protocol_id[%d] project_id[%d] \n", e.what(), static_cast<int32_t>(p->m_type), protocol_id, p->m_projectId);

        // TODO: 自定义业务错误码统一处理
        resp->body().appendData(R"({"code": -300, "message":"protocolitem create failed"})");
        return;
    }


    // 需要向对应的测试服务器上注册协议
    auto loop = project_service->getLoop();
    loop->queueInLoop([project_service, protocol_item](){

        PC_F_DEBUG("AddProtocolItem: [%d][%s][%d] \n", protocol_item->getId(), protocol_item->getName().c_str(), protocol_item->getProjectId());

        // 1. 针对业务服务的增删改查操作 需要先操作数据库后进行业务同步
        // 2. 业务操作失败需要重试，并重新查数据库进行兜底
        project_service->AddProtocolItem(protocol_item);
    
    });



    nljson root;
    root["code"] = 0;
    root["message"] = "success";
    root["data"]["protocol_id"] = protocol_id;

    resp->body().appendData(root.dump());
    PC_DEBUG() << std::endl << root.dump(4) << std::endl;

}

void ProtocolHandler::DelProtocol(kit_muduo::TcpConnectionPtr conn, kit_muduo::HttpContextPtr ctx) noexcept
{
    auto req = ctx->request();
    auto resp = ctx->response();
    resp->setVersion(Version::kHttp11);
    resp->setStateCode(StateCode::k200Ok);
    resp->body().setContentType(ContentType::kJsonType);

    DelProtocolReq request; //json

    PC_DEBUG() << std::endl << req->body().toString() << std::endl;

    // 自动根据req中的 content-type类型去解析对象
    bool ok = ctx->Bind(&request);
    if(!ok)
    {
        PJ_F_ERROR("body bind error! \n");
        resp->body().appendData(R"({"code": -200, "message":"body parse error"})");
        return;
    }

    // DTO转换 避免对外暴露领域模型Entity

    int64_t protocol_id = request.id;
    int64_t project_id = request.project_id;
    try 
    {
        // 生成对应协议种类的报文
        ok = _svc->Del(ctx, protocol_id);
    }
    catch(const std::exception& e)
    {
        PJ_F_ERROR("service add exception: %s \n", e.what());
    }

    if(!ok)
    {
        PJ_F_ERROR("service add failed \n");

        // TODO: 自定义业务错误码统一处理
        resp->body().appendData(R"({"code": -300, "message":"service failed"})");
        return;
    }

    // 向测试服务器通信 传递协议配置信息
    auto project_service = GetApp()->FindServer(request.project_id);
    if(project_service)
    {
        // 需要向对应的测试服务器上添加信息
        auto loop = project_service->getLoop();
        loop->queueInLoop([=](){
            PC_F_DEBUG("DelProtocolItem: [%d][%d] \n", protocol_id, project_id);

            project_service->DelProtocolItem(protocol_id);
        });
       
    }
    else
    {
        PC_F_WARN("project not found! %d \n", project_id);
        // 数据库可以操作 但是服务器可能操作不了 应该可以降级处理
        // 服务器重新拉起的过程需要去读一次数据库 保证数据一致性
        // resp->body().appendData(R"({"code": -300, "message":"project not found"})");
        // return;
    }

    resp->body().appendData(R"({"code": 0, "message":"success"})");
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
        protocol_id = std::stol(ctx->Param("protocol_id"));
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
        protocol = _svc->GetById(ctx, protocol_id);
        if(protocol.m_id < 0)
            throw std::runtime_error("protocol.id <= 0");
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
    // 查测试服务 信息
    try 
    {
        protocols = _svc->GetByProject(ctx, request.project_id, request.offset, request.limit);
    }
    catch(const std::exception& e)
    {
        PC_F_ERROR("service GetByUser exception: %s \n", e.what());
        resp->body().appendData(R"({"code": -300, "message":"service failed"})");
        return;
    }


    nljson root;
    root["code"] = 0; // TODO domain错误码统一化
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

    ProtocolDetailNameReq request; //json

    PC_DEBUG()  << std::endl << req->body().toString() << std::endl;


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
    try {
        if(HttpRequest::Method::kPost == req->method()())
        {
            bool ok = _svc->UpdateName(ctx, request.id, request.name);

            if(!ok)
            {
                PJ_F_ERROR("service UpdateName failed \n");
        
                root["code"] = -300;
                root["message"] = "service failed";
            }

        }
        else if(HttpRequest::Method::kGet == req->method()()) // BUG 这样写是有问题的
        {
            auto pc = _svc->GetById(ctx, request.id);
            root["data"]["id"] = pc.m_id;
            root["data"]["name"] = pc.m_name;
            root["data"]["project_id"] = pc.m_projectId;
        }

    } catch(const std::exception& e) {

        PC_F_ERROR("service UpdateName exception: %s \n", e.what());
        root["code"] = -300;
        root["message"] = "service failed";
    }

    resp->body().appendData(root.dump());

    return;
}



void ProtocolHandler::DetailCfg(kit_muduo::TcpConnectionPtr conn, kit_muduo::HttpContextPtr ctx) noexcept
{
    auto req = ctx->request();
    auto resp = ctx->response();
    resp->setVersion(Version::kHttp11);
    resp->setStateCode(StateCode::k200Ok);
    resp->body().setContentType(ContentType::kJsonType);

    DetailCfgReq request; //json
    /*
    {
        id: 
        project_id: 
        req_or_resp: 
        cfg_data: {
            
        }
    }
    */

    PC_DEBUG() << std::endl << req->body().toString() << std::endl;

    // 自动根据req中的 content-type类型去解析对象
    bool ok = ctx->Bind(&request);
    if(!ok)
    {
        PC_F_ERROR("body bind error! \n");

        resp->body().appendData(R"({"code": -200, "message":"body parse error"})");
        return;
    }

    if(DetailReq::kRequest != request.req_or_resp
        && DetailReq::kResponse !=  request.req_or_resp)
    {
        resp->body().appendData(R"({"code": -100, "message":"request param error"})");
        return;
    }
    

    PC_DEBUG() << "request.cfg_json: " << request.cfg_data.dump() << std::endl;

    ok = false;
    try {
       
        // ok = _svc->UpdateCfg(ctx,  request.id, request.req_or_resp, request.cfg_data.dump());
        ok = _svc->UpdateCfg(ctx,  request.id, request.req_or_resp, request.cfg_data);
        if(!ok) { throw std::logic_error("UpdateCfg failed"); }

    } catch(const std::exception& e) {

        PC_F_ERROR("service UpdateCfg exception: %s \n", e.what());
        // TODO: 自定义业务错误码统一处理
        resp->body().appendData(R"({"code": -300, "message":"service failed"})");
        return;
    }
    int64_t protocol_id = request.id;
    int64_t project_id = request.project_id;
    int32_t req_or_resp = request.req_or_resp;

    // TODO 更新服务器上的协议配置信息
    auto project_service = GetApp()->FindServer(request.project_id);
    if(project_service)
    {
        // 需要向对应的测试服务器上添加信息
        auto loop = project_service->getLoop();
        loop->queueInLoop([project_service,
            protocol_id, 
            project_id,
            req_or_resp,
            cfg_json = std::move(request.cfg_data)](){

            PC_F_DEBUG("UpdateCfgProtocolItem: protocol_id[%d] project_id[%d] req_or_resp[%d]\n", protocol_id, project_id, req_or_resp);

            if(DetailReq::kRequest == req_or_resp)
            {
                project_service->UpdateReqCfgProtocolItem(protocol_id, cfg_json);
            }
            else if(DetailReq::kResponse == req_or_resp)
            {
                project_service->UpdateRespCfgProtocolItem(protocol_id, cfg_json);
            }

        });
       
    }
    else
    {
        PC_F_WARN("project not found! %d \n", project_id);
        resp->body().appendData(R"({"code": -300, "message":"project not found"})");
        return;
    } 

    resp->body().appendData(R"({"code": 0, "message":"success"})");

}

void ProtocolHandler::DetailBody(kit_muduo::TcpConnectionPtr conn, kit_muduo::HttpContextPtr ctx) noexcept
{
    auto req = ctx->request();
    auto resp = ctx->response();
    resp->setVersion(Version::kHttp11);
    resp->setStateCode(StateCode::k200Ok);
    resp->body().setContentType(ContentType::kJsonType);

    DetailReq request; //json
    // 注意 请求/响应 走同一个服务处理 url不同

    PC_DEBUG() << std::endl << req->body().toString() << std::endl;

    // 自动根据req中的 content-type类型去解析对象
    bool ok = ctx->Bind(&request);
    if(!ok)
    {
        PC_F_ERROR("body bind error! \n");

        resp->body().appendData(R"({"code": -200, "message":"body parse error"})");
        return;
    }

    // ProtocolType type = ProtocolTypeFromString(request.header.type);
    ProtocolBodyType body_type = ProtocolBodyTypeFromString(request.header.body_type);

    if((DetailReq::kRequest != request.header.req_or_resp
        && DetailReq::kResponse !=  request.header.req_or_resp) || (body_type <= ProtocolBodyType::UNKNOWN_BODY_TYPE || body_type > ProtocolBodyType::BINARY_BODY_TYPE))
    {
        resp->body().appendData(R"({"code": -100, "message":"request param error"})");
        return;
    }

    ok = false;
    try 
    {
        ok = _svc->UpdateBody(ctx, request.header.id, request.header.req_or_resp, static_cast<int32_t>(body_type), request.cfg_data);
        if(!ok)
            throw std::logic_error("UpdateBody failed");
    }
    catch(const std::exception& e)
    {
        PC_F_ERROR("service UpdateBody exception: %s \n", e.what());
        resp->body().appendData(R"({"code": -300, "message":"service failed"})");
        return;
    }
    int64_t protocol_id = request.header.id;
    int64_t project_id = request.header.project_id;
    int32_t req_or_resp = request.header.req_or_resp;

    // TODO 更新服务器上的协议配置信息
    auto project_service = GetApp()->FindServer(request.header.project_id);
    if(project_service)
    {
        // 需要向对应的测试服务器上添加信息
        auto loop = project_service->getLoop();

        loop->queueInLoop([protocol_id,
            project_id,
            req_or_resp,
            project_service,
            body_type,
            data = std::move(request.cfg_data)](){

            PC_F_DEBUG("UpdateBodyProtocolItem: protocol_id[%d] project_id[%d] req_or_resp[%d] body_type[%d]\n", protocol_id, project_id, req_or_resp, body_type);

            if(DetailReq::kRequest == req_or_resp)
                project_service->UpdateReqBodyProtocolItem(protocol_id, body_type, data);
            else if(DetailReq::kResponse == req_or_resp)
                project_service->UpdateRespBodyProtocolItem(protocol_id, body_type, data);

        });
       
    }
    else
    {
        PC_F_WARN("project not found! %d \n", project_id);
        
        //TODO 需要去重启服务
        resp->body().appendData(R"({"code": -300, "message":"project not found"})");
        return;
    }

    resp->body().appendData(R"({"code": 0, "message":"success"})");
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
        project_id = std::stol(ctx->Param("project_id"));
        if(project_id <= 0)
            throw;

    } catch(const std::exception& e) {

        PC_F_ERROR("query param transform fail! project_id=%d , %s\n", project_id, e.what());
        
        resp->body().appendData(R"({"code": -200, "message":"query param transform fail"})");
        return;
    }

    int32_t protocol_cnt = -1;
    try 
    {
        protocol_cnt = _svc->GetProtocolCnt(ctx, project_id);
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
        protocol_id = std::stol(ctx->Param("protocol_id"));
        if(protocol_id <= 0)
            throw;

    } catch(const std::exception& e) {

        PJ_F_ERROR("query param transform fail! protocol_id=%d , %s\n", protocol_id, e.what());
        
        resp->body().appendData(R"({"code": -200, "message":"query param transform fail"})");
        return;
    }

    nljson protocol_cfg;
    try {
        protocol_cfg = _svc->GetCfgById(ctx, protocol_id);

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

    if(DetailReq::kRequest != request.req_or_resp
        && DetailReq::kResponse !=  request.req_or_resp)
    {
        resp->body().appendData(R"({"code": -200, "message":"request param error"})");
        return;
    }


    nljson common_fields_json;
    try 
    {
        common_fields_json = _svc->GetTcpCommonFieldsById(ctx, request.id, request.req_or_resp);

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

    // ProtocolType type = ProtocolTypeFromString(request.type);

    if(DetailReq::kRequest != request.req_or_resp
        && DetailReq::kResponse !=  request.req_or_resp)
    {
        resp->body().appendData(R"({"code": -100, "message":"request param error"})");
        return;
    }
    int64_t protocol_id = request.id;
    // int64_t project_id = request.project_id;
    int32_t req_or_resp = request.req_or_resp;

    ProtocolBodyType body_type;
    try 
    {

        body_type =  _svc->GetBodyTypeById(ctx, protocol_id, req_or_resp);
        if(body_type <= ProtocolBodyType::UNKNOWN_BODY_TYPE || body_type > ProtocolBodyType::BINARY_BODY_TYPE)
            throw std::logic_error("GetBodyTypeById failed");
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

    // ProtocolType type = ProtocolTypeFromString(request.type);

    if(DetailReq::kRequest != request.req_or_resp
        && DetailReq::kResponse !=  request.req_or_resp)
    {
        resp->body().appendData(R"({"code": -100, "message":"request param error"})");
        return;
    }
    int64_t protocol_id = request.id;
    // int64_t project_id = request.project_id;
    int32_t req_or_resp = request.req_or_resp;

    std::vector<char> body_data;
    try 
    {
        ok = _svc->GetBodyDataById(ctx, protocol_id, req_or_resp, body_data);
        if(!ok)
            throw std::logic_error("GetBodyDataById failed");
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

    // ProtocolType type = ProtocolTypeFromString(request.type);

    if(DetailReq::kRequest != request.req_or_resp
        && DetailReq::kResponse !=  request.req_or_resp)
    {
        resp->body().appendData(R"({"code": -100, "message":"request param error"})");
        return;
    }

    ProtocolBodyType body_type;
    std::vector<char> body_data;
    try {
        ok = _svc->GetBodyInfoById(ctx, request.id, request.req_or_resp, body_type, body_data);
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



}   // namespace kit_domain