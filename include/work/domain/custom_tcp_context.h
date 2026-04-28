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

#include <string>
#include <memory>


namespace kit_muduo {

class Buffer;
class TimeStamp;

} // namespace kit_muduo



namespace kit_domain {

class CustomTcpProjectServer;
class CustomTcpPattern;
class CustomTcpMessage;
struct CustomPatternInfo;

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


    bool parseRequest(kit_muduo::Buffer &buf, kit_muduo::TimeStamp receiveTime);

    bool parseRequest(const std::vector<char> &data, kit_muduo::TimeStamp receiveTime);

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


private:
    /// @brief 这里需要通过功能码反查到配置的格式字段
    CustomTcpProjectServer* server_;
    /// @brief tcp请求解析状态
    TcpParseState  state_{kExpectHeader};
    /// @brief 临时缓冲区 处理可能存在的截断的报文
    kit_muduo::Buffer buffer_;
    /// @brief 剩余应收长度
    int64_t remain_bytes_len_;
    /*注意: 这里请求/响应报文生成的时间点 应该是实际解析到功能的时候 */
    /// @brief tcp请求报文
    std::shared_ptr<CustomTcpMessage> request_;
    /// @brief tcp响应报文
    std::shared_ptr<CustomTcpMessage> response_;
};

} // namespace kit_domain
#endif //__KIT_CUSTOM_TCP_CONTEXT_H__