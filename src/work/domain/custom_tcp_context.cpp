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
#include "domain/project_server.h"
#include "domain/protocol_item.h"

using namespace kit_muduo;

namespace kit_domain {

CustomTcpContext::CustomTcpContext(CustomTcpProjectServer *server)
    :server_(server)
    ,state_(kExpectHeader)
    ,remain_bytes_len_(0)
    ,request_(std::make_shared<CustomTcpMessage>())
    ,response_(std::make_shared<CustomTcpMessage>())
{
    assert(server_);


    CUSTOM_F_DEBUG("CustomTcpContext::construct() %p\n", this);
}

CustomTcpContext::~CustomTcpContext()
{
    CUSTOM_F_DEBUG("CustomTcpContext::~CustomTcpContext() %p\n", this);
}

bool CustomTcpContext::parseRequest(const std::vector<char> &data, kit_muduo::TimeStamp receiveTime)
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

bool CustomTcpContext::parseRequest(kit_muduo::Buffer &buf, kit_muduo::TimeStamp receiveTime)
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
                    return true;
                }
                else
                {
                    CUSTOM_F_INFO("tcp data parse error: %d \n", parse_result.status);
                    return false;
                }

            }


            if(!server_->findByFuncCode(parse_result.function_code))
            {
                CUSTOM_F_ERROR("FuncCode not found! %s \n", parse_result.function_code.c_str());
                return false;
            }
            request_->setFunctionCodeHex(parse_result.function_code);
            
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
                remain_bytes_len_ = 0;
                state_ = TcpParseState::kGotAll;
            }
        }
        else if(TcpParseState::kExpectBody == state_)
        {
            if(remain_bytes_len_ < 0)
            {
                CUSTOM_F_ERROR("remain_bytes_len invalid\n");
                return false;
            }

            if(buf.readableBytes() < remain_bytes_len_)
            {
                CUSTOM_F_DEBUG("buffer data not enough! readableBytes[%ld]  < remain_bytes_len[%ld]\n", buf.readableBytes(), remain_bytes_len_);
                return true;
            }
            // 注意: buffer里可能还有残余数据 不能全部清除 需要保留下来给下一个请求使用
            request_->appendBodyData(buf.peek(), remain_bytes_len_);
            // 减去剩余body长度
            buf.reset(remain_bytes_len_);

            remain_bytes_len_ = 0;
            state_ = TcpParseState::kGotAll;

        }
    }

    return true;
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
    request_ = std::make_shared<CustomTcpMessage>();
    response_ = std::make_shared<CustomTcpMessage>();
}



}
