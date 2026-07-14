/**
 * @file custom_tcp_context.cpp
 * @brief 自定义TCP上下文
 * @author ljk5
 * @version 1.0
 * @date 2025-11-03 16:20:15
 * @copyright Copyright (c) 2025 HIKRayin
 */
#include "domain/custom_tcp_field_type_traits.h"
#include "domain/domain_log.h"
#include "base/time_stamp.h"
#include "net/buffer.h"
#include "domain/custom_tcp_context.h"
#include "domain/custom_tcp_message.h"
#include "domain/custom_tcp_pattern.h"
#include "domain/custom_tcp_project_server.h"
#include <stdexcept>

using namespace kit_muduo;

namespace kit_domain {

namespace {

inline CustomTcpProjectServer *CheckServerNull(CustomTcpProjectServer *server)
{
    if(!server)
    {
        std::invalid_argument("custom tcp context construct failed: server null!");
    }
    return server;
}
}

CustomTcpContext::CustomTcpContext(CustomTcpProjectServer *server)
    :server_(CheckServerNull(server))
    ,state_(kExpectHeader)
    ,result_(CustomTcpParseResult{
        .status = CustomTcpParseStatus::kOk,
        .message = "parse ok",
    })
    ,remain_bytes_len_(0)
{
    request_ = std::make_shared<CustomTcpMessage>(server_->GetPatternInfo());
    response_ = std::make_shared<CustomTcpMessage>(server_->GetPatternInfo());

    CUSTOM_F_DEBUG("CustomTcpContext::construct() %p\n", this);
}

CustomTcpContext::~CustomTcpContext()
{
    CUSTOM_F_DEBUG("CustomTcpContext::~CustomTcpContext() %p\n", this);
}

CustomTcpParseResult CustomTcpContext::parseRequest(const std::vector<char> &data, kit_muduo::TimeStamp receiveTime)
{
    Buffer buf;
    buf.append(data.data(), data.size());
    return parseRequest(buf, receiveTime);
}

inline static void ShowField(const FieldValue& field_value)
{
    const auto& spec = field_value.spec;
    CUSTOM_F_DEBUG("name[%s], type[%s] byte_pos[%d], byte_len[%d], value[%s] Field extract success!\n",
        spec.name.c_str(), FieldTypeToString(spec.type).c_str(), spec.byte_pos, spec.byte_len, field_value.hex().c_str());
}

CustomTcpParseResult CustomTcpContext::parseRequest(kit_muduo::Buffer &buf, kit_muduo::TimeStamp receiveTime)
{
    // 这里只是查看 TcpConnect上的缓冲区数据 并没有进行读取操作
    const auto& tmp =  buf.lookAllAsData();
    const std::vector<uint8_t> complete_data(tmp.begin(), tmp.end());

    auto pattern = server_->GetPatternInfo();

    while(state_ != kGotAll)
    {
        if(kExpectHeader == state_)
        {

            // 1. 头部字段全解析
            auto parse_result = pattern->parseHeader(complete_data);
            if(!parse_result.ok())
            {
                if(CustomTcpPattern::ParseHeaderResult::kNonMinLength == parse_result.status)
                {
                    CUSTOM_F_INFO("tcp data is not complete: %ld \n", complete_data.size());
                    return CustomTcpParseResult{
                        .status = CustomTcpParseStatus::kOk,
                        .message = "need more data",
                    };
                }
                else
                {
                    CUSTOM_F_INFO("tcp data parse error: %d \n", parse_result.status);
                    return CustomTcpParseResult{
                        .status = CustomTcpParseStatus::kParseError,
                        .message = "parse error",
                    };
                }

            }

            const auto& cb = server_->findCBByFuncCode(parse_result.function_code);
            if(nullptr == cb)
            {
                CUSTOM_F_ERROR("func code not found! %s \n", parse_result.function_code.c_str());
                return CustomTcpParseResult{
                    .status = CustomTcpParseStatus::kFuncCodeNotFound,
                    .message = "func code not found",
                };
            }
            request_->setFunctionCodeHex(parse_result.function_code);
            // 待执行业务函数回调
            result_.cb = std::move(cb);

            // 2. 字段赋值
            for(auto &field_value : parse_result.fields_value)
            {
                // DEBUG 调试信息打印
                ShowField(field_value);
                request_->addField(field_value);
            }

            // 3.根据每种格式不同进行长度信息收取
            // 长度信息可能是没有的
            remain_bytes_len_ = parse_result.remain_body_bytes;

            CUSTOM_DEBUG() << "remain_bytes_len_: " << remain_bytes_len_ << std::endl;

            // 头解析没有出错 将当前所有字段的长度减去
            buf.reset(pattern->spec().header_bytes);
            if(remain_bytes_len_ > 0)
            {
                state_ = TcpParseState::kExpectBody;
            }
            else  // 说明是不带body的类型
            {
                request_->setRecordTime(receiveTime);
                remain_bytes_len_ = 0;
                state_ = TcpParseState::kGotAll;
            }
        }
        else if(TcpParseState::kExpectBody == state_)
        {
            if(remain_bytes_len_ < 0)
            {
                CUSTOM_F_ERROR("remain_bytes_len invalid\n");
                return CustomTcpParseResult{
                    .status = CustomTcpParseStatus::kInternalError,
                    .message = "body length invalid",
                };
            }

            if(buf.readableBytes() < remain_bytes_len_)
            {
                CUSTOM_F_DEBUG("buffer data not enough! readableBytes[%ld]  < remain_bytes_len[%ld]\n", buf.readableBytes(), remain_bytes_len_);
                return CustomTcpParseResult{
                    .status = CustomTcpParseStatus::kOk,
                    .message = "need more data",
                };
            }
            // 注意: buffer里可能还有残余数据 不能全部清除 需要保留下来给下一个请求使用
            request_->appendBodyData(buf.peek(), remain_bytes_len_);
            // 减去剩余body长度
            buf.reset(remain_bytes_len_);

            request_->setRecordTime(receiveTime);
            remain_bytes_len_ = 0;
            state_ = TcpParseState::kGotAll;
        }
    }

    return result_;
}

bool CustomTcpContext::parseResponse(const std::string &data, const CustomPatternInfo& parse_pattern_info, kit_muduo::TimeStamp receiveTime)
{
    return false;

}
bool CustomTcpContext::parseResponse(kit_muduo::Buffer &buf, const CustomPatternInfo& parse_pattern_info, kit_muduo::TimeStamp receiveTime)
{
    return false;

}

void CustomTcpContext::reset()
{
    /*注意 buffer不能清*/
    state_ = kExpectHeader;
    request_.reset();
    response_.reset();
    result_ = CustomTcpParseResult{};
    request_ = std::make_shared<CustomTcpMessage>(server_->GetPatternInfo());
    response_ = std::make_shared<CustomTcpMessage>(server_->GetPatternInfo());
}



}
