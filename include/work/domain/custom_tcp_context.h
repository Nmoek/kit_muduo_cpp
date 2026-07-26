/**
 * @file custom_tcp_context.h
 * @brief 自定义TCP上下文
 * @author ljk5
 * @version 1.0
 * @date 2025-11-03 15:19:07
 * @copyright Copyright (c) 2025 HIKRayin
 */
#ifndef __KIT_CUSTOM_TCP_CONTEXT_H__
#define __KIT_CUSTOM_TCP_CONTEXT_H__

#include "net/call_backs.h"
#include "protocol_interaction.h"

#include <string>
#include <memory>
#include <vector>


namespace kit_muduo {

class Buffer;
class TimeStamp;

} // namespace kit_muduo



namespace kit_domain {

class CustomTcpProjectServer;
class CustomTcpPattern;
class CustomTcpMessage;
struct CustomPatternInfo;
class CustomTcpProtocolItem;
class CustomTcpContext;

enum class CustomTcpParseStatus
{
    kOk,
    kParseError,
    kFuncCodeNotFound,
    kInternalError,
};

using ProcessCallback = std::function<void(kit_muduo::TcpConnectionPtr, std::shared_ptr<CustomTcpContext>)>;


struct CustomTcpParseResult
{
    CustomTcpParseStatus status{CustomTcpParseStatus::kOk};
    ProcessCallback cb{nullptr};
    std::string message{"parse ok"};

    bool ok() const { return status == CustomTcpParseStatus::kOk; }

    InteractionResult toInterResult() const
    {
        switch (status) 
        {
            case CustomTcpParseStatus::kOk:
                return InteractionResult::kMatched;
            case CustomTcpParseStatus::kParseError:
                return InteractionResult::kParseError;
            case CustomTcpParseStatus::kFuncCodeNotFound:
                return InteractionResult::kRouteNotFound;
            default:
                return InteractionResult::kInternalError;
        }
    }
};

struct CustomTcpParseLimits
{
    size_t max_error_capture_bytes{1 * 1024};
};


class CustomTcpContext 
{
public:
    /**
     * @brief 解析有限状态机
     */
    enum TcpParseState
    {
        kExpectHeader,
        kExpectBody, // 这部分可能没有
        kGotAll,
    };

    CustomTcpContext(CustomTcpProjectServer *server);
    ~CustomTcpContext();


    CustomTcpParseResult parseRequest(kit_muduo::Buffer &buf, kit_muduo::TimeStamp receiveTime);

    CustomTcpParseResult parseRequest(const std::vector<char> &data, kit_muduo::TimeStamp receiveTime);

    bool parseResponse(const std::string &data, const CustomPatternInfo& parse_pattern_info, kit_muduo::TimeStamp receiveTime);
    bool parseResponse(kit_muduo::Buffer &buf, const CustomPatternInfo& parse_pattern_info, kit_muduo::TimeStamp receiveTime);

    TcpParseState state() const { return state_; }
    void setState(TcpParseState state) { state_ = state; }

    bool gotAll() const { return kGotAll == state_; }

    std::shared_ptr<CustomTcpMessage> request() const { return request_; }

    std::shared_ptr<CustomTcpMessage> response() const { return response_; }

    /**
     * @brief 重置上下文
     */
    void reset();

    std::vector<uint8_t>& rawCapture() { return raw_capture_; }
    const std::vector<uint8_t>& rawCapture() const { return raw_capture_; }

    const CustomTcpParseLimits& limits() const { return limits_; }

private:
    /// @brief 这里需要通过功能码反查到配置的格式字段
    CustomTcpProjectServer* server_;
    /// @brief tcp请求解析状态
    TcpParseState  state_{kExpectHeader};
    /// @brief 最后的解析结果
    CustomTcpParseResult result_;
    /// @brief 剩余应收长度
    int64_t remain_bytes_len_;
    /*注意: 这里请求/响应报文生成的时间点 应该是实际解析到功能的时候 */
    /// @brief tcp请求报文
    std::shared_ptr<CustomTcpMessage> request_;
    /// @brief tcp响应报文
    std::shared_ptr<CustomTcpMessage> response_;
    /// @brief 用于解析失败捕获raw bytes
    std::vector<uint8_t> raw_capture_;
    /// @brief tcp解析限制配置
    CustomTcpParseLimits limits_;
};
using CustomTcpContextPtr = std::shared_ptr<CustomTcpContext>;

} // namespace kit_domain
#endif //__KIT_CUSTOM_TCP_CONTEXT_H__