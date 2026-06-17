/**
 * @file protocol_body_pipeline.h
 * @brief 协议项Body校验器
 * @author Kewin Li
 * @version 1.0
 * @date 2026-06-16 16:35:14
 * @copyright Copyright (c) 2026 Kewin Li
 */
#ifndef __KIT_POTOCOL_BODY_PIPIELINE_H__
#define __KIT_POTOCOL_BODY_PIPIELINE_H__


#include "domain/type.h"

namespace kit_domain {

class ProtocolBodyPolicy;
struct Protocol;

struct ProtocolBodySpec 
{
    ProtocolBodyType body_type{ProtocolBodyType::kUnknown};
    const std::vector<char> &body_data;
};

struct ProtocolFullBodySpec
{
    ProtocolBodyType req_body_type{ProtocolBodyType::kUnknown};
    const std::vector<char> &req_body_data;
    ProtocolBodyType resp_body_type{ProtocolBodyType::kUnknown};
    const std::vector<char> &resp_body_data;

    ProtocolBodySpec toReqSpec() const
    {
        return {
            .body_type = req_body_type,
            .body_data = req_body_data,
        };
    }

    ProtocolBodySpec toRespSpec() const
    {
        return {
            .body_type = resp_body_type,
            .body_data = resp_body_data,
        };
    }
};

struct ProtocolBodyCheckResult
{
    bool ok{false};
    std::string message;

    static ProtocolBodyCheckResult Success(const std::string &msg = "check ok")
    {
        return {true, msg};
    }

    static ProtocolBodyCheckResult Failed(const std::string& msg = "check invalid")
    {
        return {false, msg};
    }
};



class ProtocolBodyPipeline
{
public:
    static ProtocolBodyCheckResult CheckBody(const ProtocolBodySpec &spec);
    static ProtocolBodyCheckResult CheckFullBody(const ProtocolFullBodySpec &spec);
    static ProtocolBodyCheckResult CheckFullProtocol(const Protocol &p);
private:
    static const ProtocolBodyPolicy* getPolicy(ProtocolBodyType body_type);
};

class ProtocolBodyPolicy
{
public:
    virtual ~ProtocolBodyPolicy() = default;

    ProtocolBodyCheckResult check(const ProtocolBodySpec &spec) const;

protected:
    virtual ProtocolBodyCheckResult checkNonEmptyBody(const ProtocolBodySpec &spec) const = 0;
};

class JsonBodyPolicy final: public ProtocolBodyPolicy
{
public:
    ~JsonBodyPolicy() override = default;
protected:
    ProtocolBodyCheckResult checkNonEmptyBody(const ProtocolBodySpec &spec) const override;
};

class XmlBodyPolicy final: public ProtocolBodyPolicy
{
public:
    ~XmlBodyPolicy() override = default;
protected:
    ProtocolBodyCheckResult checkNonEmptyBody(const ProtocolBodySpec &spec) const override;
};

class TextBodyPolicy final: public ProtocolBodyPolicy
{
public:
    ~TextBodyPolicy() override = default;
protected:
    ProtocolBodyCheckResult checkNonEmptyBody(const ProtocolBodySpec &spec) const override;
};

class BinaryBodyPolicy final: public ProtocolBodyPolicy
{
public:
    ~BinaryBodyPolicy() override = default;
protected:
    ProtocolBodyCheckResult checkNonEmptyBody(const ProtocolBodySpec &spec) const override;
};


}
#endif // __KIT_POTOCOL_BODY_PIPIELINE_H__