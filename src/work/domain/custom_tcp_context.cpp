/**
 * @file custom_tcp_context.cpp
 * @brief 自定义TCP上下文
 * @author ljk5
 * @version 1.0
 * @date 2025-11-03 16:20:15
 * @copyright Copyright (c) 2025 HIKRayin
 */
#include "web/web_log.h"
#include "base/time_stamp.h"
#include "net/buffer.h"
#include "domain/custom_tcp_context.h"
#include "domain/custom_tcp_message.h"
#include "domain/custom_tcp_pattern.h"
#include "domain/project_server.h"
#include "domain/protocol_item.h"

namespace kit_domain {

CustomTcpContext::CustomTcpContext(CustomTcpProjectServer *server)
    :server_(server)
    ,state_(kExpectHeader)
    ,remain_bytes_len_(0)
    ,request_(std::make_shared<CustomTcpMessage>())
    ,response_(std::make_shared<CustomTcpMessage>())
{
    assert(server_);


    PJ_F_DEBUG("CustomTcpContext::construct() %p\n", this);
}

CustomTcpContext::~CustomTcpContext()
{
    PJ_F_DEBUG("CustomTcpContext::~CustomTcpContext() %p\n", this);
}

bool CustomTcpContext::parseRequest(kit_muduo::Buffer &buf, kit_muduo::TimeStamp receiveTime)
{
    return parseRequest(buf.resetAllAsData(), receiveTime);
}

static void ShowField(std::shared_ptr<CustomTcpPatternFieldBase> field)
{
    CUSTOM_F_DEBUG("name[%s], idx[%d], type[%s] byte_pos[%d], byte_len[%d], value[%s] Field extract success!\n", 
        field->name().c_str(),field->idx(), field->getTypeEnum().toStrs(), field->byte_pos(), field->byte_len(), field->toHexString().c_str());
}

bool CustomTcpContext::parseRequest(const std::vector<char> &data, kit_muduo::TimeStamp receiveTime)
{
    // 这里只是查看 TcpConnect上的缓冲区数据 并没有进行读取操作
    buffer_.append(data.data(), data.size());
    const std::vector<char>& complete_data = buffer_.lookAllAsData();

    auto pattern = server_->getPatternInfo();
    int32_t head_bytes_len = pattern->headByteLen();


    if(complete_data.size() < head_bytes_len)
    {
        CUSTOM_F_INFO("data is not complete %d/%d \n", complete_data.size(), head_bytes_len);
        return true;
    }

    CUSTOM_DEBUG() << "complete_data:" << complete_data.size() << ", " << kit_muduo::BytesToHexString(std::vector<uint8_t>(complete_data.begin(), complete_data.end()), " ") << std::endl;

    while(state_ != kGotAll)
    {
        if(kExpectHeader == state_)
        {

            // 1. 解析出起始魔数字段  对照配置的起始魔数进行校验
            auto cfg_field = pattern->startMagicNumField();
            if(!cfg_field)
            {
                CUSTOM_ERROR() << "StartMagicNumField  is null!" << std::endl;
                return false;
            }

            auto real_start_magic_num_field = cfg_field->extract(complete_data, !KIT_IS_LOCAL_BIG_ENDIAN());
            if(!real_start_magic_num_field)
            {
                CUSTOM_ERROR() << "StartMagicNumField extract error!" << std::endl;
                return false;
            }
            ShowField(real_start_magic_num_field);


            // 2. 解析出功能码 对照配置功能码 并进行协议项索引+
            cfg_field = pattern->functionCodeField();
            if(!cfg_field)
            {
                CUSTOM_ERROR() << "FunctionCodeField is null!" << std::endl;
                return false;
            }

            // !!!!!!!!!!!!!!!!!!
            // BUG: 这里接口提取有问题!
            // bytes ---> int32 --> HexString  这个流程是有问题的
            // !!!!!!!!!!!!!!!!!!

            auto real_func_code_field = cfg_field->extract(complete_data, !KIT_IS_LOCAL_BIG_ENDIAN());
            if(!real_func_code_field)
            {
                CUSTOM_ERROR() << "FunctionCodeField extract error!" << std::endl;
                return false;
            } 

            // DEBUG
            ShowField(real_func_code_field);

            // 注意当前项目均按大端排序展示 不管实际的内存序到底是什么
            // 找出功能码对应的协议项
            const std::string func_code_str = real_func_code_field->toHexString(!KIT_IS_LOCAL_BIG_ENDIAN());

            CUSTOM_DEBUG() << "findByFuncCode: " << func_code_str << std::endl;

            auto tcp_item = server_->findByFuncCode(func_code_str);
            if(!tcp_item)
            {
                CUSTOM_F_ERROR("FuncCode not found! %s \n", func_code_str.c_str());
                return false;
            }

            // 初始化字段数组
            request_->setFieldNums(tcp_item->getReq()->getFieldNums());

            // 起始标识符 加入到字段列表中
            request_->addField(real_start_magic_num_field->idx(), real_start_magic_num_field);

            // 功能码 加入到字段列表中
            request_->addField(real_func_code_field->idx(), real_func_code_field);

            // 3.根据每种格式不同进行长度信息收取
            // 长度信息可能是没有的
            cfg_field = pattern->lengthInfoField();
            if(cfg_field)
            {
                auto real_length_info_field = cfg_field->extract(complete_data, !KIT_IS_LOCAL_BIG_ENDIAN());
                if(!real_length_info_field)
                {
                    CUSTOM_ERROR() << "LengthInfoField extract error!" << std::endl;
                    return false;
                } 
                request_->addField(real_length_info_field->idx(), real_length_info_field);

                // DEBUG
                ShowField(request_->getField(real_length_info_field->idx()));
                
                // 通过长度信息 获取到剩下的body应该解析的长度
                remain_bytes_len_ = pattern->calRemainBytesLen(real_length_info_field);

                CUSTOM_DEBUG() << "remain_bytes_len_: " << remain_bytes_len_ << std::endl;

            }

            // 4. 收取其他剩余的普通头部字段信息
            for(int i = 0;i < request_->getFieldNums(); ++i)
            {
                // 从配置好的字段列表里获取
                auto cfg_field = tcp_item->getReq()->getField(i);
                if(!cfg_field)
                {
                    CUSTOM_F_ERROR("Field is null! idx[%d]\n", i);
                    continue;
                }

                if(!cfg_field->is_special())
                {
                    // 提取值完毕后 又放入到request_
                    auto real_field = cfg_field->extract(complete_data, !KIT_IS_LOCAL_BIG_ENDIAN());
                    if(!real_field)
                    {
                        CUSTOM_F_ERROR("Field extract error! name[%s], idx[%d], byte_pos[%d] byte_len[%d]\n", 
                            cfg_field->name().c_str(),cfg_field->idx(), cfg_field->byte_pos(), cfg_field->byte_len());
                        return false;
                    }
                    real_field->setSepcial(false); // 置为普通字段
                    request_->addField(real_field->idx(), real_field);
                    // DEBUG
                    ShowField(request_->getField(real_field->idx()));
                }
            }

            // 头解析没有出错 将当前所有字段的长度减去
            buffer_.reset(request_->getHeaderBytes());
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
            if(complete_data.size() >= remain_bytes_len_)
            {
                // 注意: buffer里可能还有残余数据 不能全部清除 需要保留下来给下一个请求使用
                request_->body().appendData((char*)complete_data.data() + head_bytes_len, remain_bytes_len_);
                // 减去剩余body长度
                buffer_.reset(remain_bytes_len_);

                remain_bytes_len_ = 0;
                state_ = TcpParseState::kGotAll;
            }
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