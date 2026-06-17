/**
 * @file content_codec.cpp
 * @brief 基础数据格式序列化/反序列化
 * @author Kewin Li
 * @version 1.0
 * @date 2026-06-15 15:43:02
 * @copyright Copyright (c) 2026 Kewin Li
 */
#include "base/base_log.h"
#include "base/content_codec.h"

#include <algorithm>
#include <cctype>

namespace kit_muduo {

namespace {

inline void AlltoLowwer(std::string &raw_content_type)
{
    for(auto &c : raw_content_type)
    {
        c = std::isalpha(c) ? std::tolower(c) : c;
    }
}

inline std::string TrimSpace(const std::string &str)
{
    size_t i = -1;
    size_t j = str.size();
    while(str[++i] == ' ');
    while(str[--j] == ' ');
    return j >= i ? str.substr(i, j - i + 1) : "";
}

/**
 * @brief 将参数分离
 * @param params 
 */
inline std::pair<std::string, std::string> ParseParamsHelper(const std::string &params)
{
    std::pair<std::string, std::string> p;
    auto pos = params.find("=");
    if(std::string::npos == pos)
    {
        return p;
    }
    p.first = TrimSpace(params.substr(0, pos));
    p.second = TrimSpace(params.substr(pos + 1));

    return p;
}

void GetContentParams(ContentMeta &meta, const std::string &raw_content_type)
{
    size_t st = 0;
    auto pos = raw_content_type.find(";", st);
    if(std::string::npos == pos)
    {
        meta.media_type = raw_content_type;
        return;
    }
    // 原始media_type类型
    meta.media_type = raw_content_type.substr(0, pos);

    st = pos;
    while(st + 1 < raw_content_type.size() && (pos = raw_content_type.find(";", st + 1)) != std::string::npos)
    {
        auto p = ParseParamsHelper(raw_content_type.substr(st + 1, pos - st - 1));
        if(!p.first.empty())
        {
            meta.params.emplace(p.first, p.second);
        }
        st = pos;
    }
    // 注意处理尾部最后一对参数
    if(st + 1 < raw_content_type.size())
    {
        auto p = ParseParamsHelper(raw_content_type.substr(st + 1));
        if(!p.first.empty())
        {
            meta.params.emplace(p.first, p.second);
        }
    }

}

}

ContentMeta ParseContentMetaFromHttpHeader(std::string raw_content_type)
{
    ContentMeta meta;

    meta.raw_content_type = raw_content_type;

    GetContentParams(meta, raw_content_type);

    AlltoLowwer(meta.media_type);

    CODEC_F_DEBUG("raw_content_type: %s,  media_type: %s\n", raw_content_type.c_str(), meta.media_type.c_str());


    if(std::string::npos != raw_content_type.find("json"))
    {
        meta.format = ContentFormat::kJson;
    }
    else if(std::string::npos != raw_content_type.find("multipart"))
    {
        meta.format = ContentFormat::kMultipart;
    }
    else if(std::string::npos != raw_content_type.find("xml"))
    {
        meta.format = ContentFormat::kXml;
    }
    else if(std::string::npos != raw_content_type.find("plain"))
    {
        meta.format = ContentFormat::kPlainText;
    }
    else if(std::string::npos != raw_content_type.find("octet-stream"))
    {
        meta.format = ContentFormat::kOctetStream;
    }

    return meta;
}

ContentCodecResult ParseJsonPartToRaw(const MultiFormParser::PartMap &parts, const std::string &field_name, nlohmann::json &out_json)
{
    FormPart *part = nullptr;
    auto result = FindRequirePart(parts, field_name, part);
    if(!result.ok || !part)
    {
        return result;
    }

    try {
        out_json = nlohmann::json::parse(part->data);

    } catch(const std::exception &e) {

        return ContentCodecResult::Failed(ContentCodecErrorCode::kInvalidField, std::string("invalid json form field: ") + e.what(), field_name);
    }
    return ContentCodecResult::Success();
}


ContentCodecResult ParseOctetStreamPartToRaw(const MultiFormParser::PartMap &parts, const std::string &field_name, std::vector<char> &out, bool required)
{
    auto it = parts.find(field_name);
    if(it == parts.end())
    {
        return !required ? ContentCodecResult::Success() : ContentCodecResult::Failed( ContentCodecErrorCode::kMissingField,
            "missing multipart field: " + field_name,field_name);
    }
    out = it->second.data;
    return ContentCodecResult::Success();
}









}