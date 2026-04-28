/**
 * @file verify_process.h
 * @brief 协议验证过程抽象接口
 * @author ljk5
 * @version 1.0
 * @date 2025-08-27 14:34:05
 * @copyright Copyright (c) 2025 HIKRayin
 */
#ifndef __KIT_DOMAIN_VERIFY_PROCESS_H__
#define __KIT_DOMAIN_VERIFY_PROCESS_H__

#include "domain/type.h"

#include <memory>
#include <unordered_map>
#include <functional>

namespace kit_domain {

class ProtocolItem;

class VerifyProcess
{
public:
    using UPtr = std::unique_ptr<VerifyProcess>;
    using VerifyFunc = std::function<VerifyProcess::UPtr(ProtocolType)>;

    explicit VerifyProcess(ProtocolType protocol_type)
        :_protocol_type(protocol_type)
    { }

    virtual bool Verify(std::shared_ptr<ProtocolItem> real_protocol, std::shared_ptr<ProtocolItem> cfg_protocol) = 0;

public:
    static std::unordered_map<VerifyMode, VerifyFunc> _verify_process_map;

protected:
    /// @brief 标识当前什么类型协议的验证过程
    ProtocolType _protocol_type;

};


class SampleVerifyProcess: public VerifyProcess
{
public:
    explicit SampleVerifyProcess(ProtocolType protocol_type)
        :VerifyProcess(protocol_type)
    {}

    bool Verify(std::shared_ptr<ProtocolItem> real_protocol, std::shared_ptr<ProtocolItem> cfg_protocol) override;

};


class StrictVerifyProcess: public VerifyProcess
{
public:
    explicit StrictVerifyProcess(ProtocolType protocol_type)
        :VerifyProcess(protocol_type)
        {}

    bool Verify(std::shared_ptr<ProtocolItem> real_protocol, std::shared_ptr<ProtocolItem> cfg_protocol) override;
};

}   //namespace kit_domain
#endif  //__KIT_DOMAIN_VERIFY_PROCESS_H__