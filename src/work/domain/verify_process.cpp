/**
 * @file verify_process.cpp
 * @brief 协议验证过程抽象接口
 * @author ljk5
 * @version 1.0
 * @date 2025-08-27 16:35:47
 * @copyright Copyright (c) 2025 HIKRayin
 */
#include "domain/verify_process.h"

namespace kit_domain {

std::unordered_map<VerifyMode, VerifyProcess::VerifyFunc> VerifyProcess::_verify_process_map = {
    // 简化模式
    {VerifyMode::SAMPLE, [](ProtocolType protocol_type){ return std::make_unique<SampleVerifyProcess>(protocol_type);}},
    // 严格模式
    {VerifyMode::STRICT, [](ProtocolType protocol_type){ return std::make_unique<SampleVerifyProcess>(protocol_type);}},
};



bool SampleVerifyProcess::Verify(std::shared_ptr<ProtocolItem> real_protocol, std::shared_ptr<ProtocolItem> cfg_protocol)
{
    return false;

}


bool StrictVerifyProcess::Verify(std::shared_ptr<ProtocolItem> real_protocol, std::shared_ptr<ProtocolItem> cfg_protocol)
{
    return false;

}

}