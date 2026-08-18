/**
 * @file config_codec_policy.h
 * @brief 配置序列化/反序列化差异化策略
 * @author Kewin Li
 * @version 1.0
 * @date 2026-07-30 23:50:32
 * @copyright Copyright (c) 2026 Kewin Li
 */

#ifndef __KIT_CONFIG_CODEC_POLICY_H__
#define __KIT_CONFIG_CODEC_POLICY_H__

#include "base/config_context.h"
#include "nlohmann/json.hpp"
#include "yaml-cpp/exceptions.h"
#include "yaml-cpp/node/node.h"
#include "yaml-cpp/node/parse.h"
#include "yaml-cpp/node/type.h"
#include "yaml-cpp/yaml.h"

#include <algorithm>
#include <sstream>


namespace kit_muduo{



struct ConfigYamlPolicy
{
    using Node = YAML::Node;

    static std::string PolicyType() { return "YAML"; }
    static Node Parse(const std::string& text)
    {
        try {
            return YAML::Load(text);

        } catch(const YAML::Exception& error) {
            throw MakeError(error, nullptr, "parse");
        }
    }

    static std::string Serialize(const Node& node)
    {
        try {
            std::stringstream stream;
            stream << node;
            return stream.str();

        } catch(const YAML::Exception& error) {
            throw MakeError(error, &node, "serialize");
        }
    }

    static std::string ToInputText(const Node& node) { return Serialize(node); }

    static std::string Key(const Node& node)
    {
        if(!IsMap(node) || node.begin() == node.end())
        {
            throw ConfigError(
                Context(node),
                "YAML map key is unavailable");
        }
        return DecodeKey(node.begin()->first);
    }

    static std::string Key(YAML::const_iterator& it)
    {
        return DecodeKey(it->first);
    }

    static std::string Key(YAML::iterator& it)
    {
        return DecodeKey(it->first);
    }

    static bool IsNull(const Node& node)
    {
        return Type(node) == YAML::NodeType::Null;
    }

    static bool IsScalar(const Node& node)
    {
        return Type(node) == YAML::NodeType::Scalar;
    }

    static bool IsSequence(const Node& node)
    {
        return Type(node) == YAML::NodeType::Sequence;
    }

    static bool IsMap(const Node& node)
    {
        return Type(node) == YAML::NodeType::Map;
    }

    static size_t Size(const Node& node)
    {
        try {
            return node.size();

        } catch(const YAML::Exception& error) {
            throw MakeError(error, &node, "read size");
        }
    }

    static ConfigContext Context(const Node& node) noexcept
    {
        try {
            return ContextFromMark(node.Mark());

        } catch(const YAML::Exception&) {
            return {};
        }
    }

    template<typename T>
    static T Decode(const Node &node)
    {
        try {
            return node.as<T>();

        } catch(const YAML::Exception& error) {
            throw MakeError(error, &node, "decode");
        }
    }

    template<typename T>
    static Node Encode(const T &value)
    {
        try {
            return Node(value);

        } catch(const YAML::Exception& error) {
            throw MakeError(error, nullptr, "encode");
        }
    }

    template<class Callback>
    static void VisitSequence(const Node& node, Callback&& cb)
    {
        if(!IsSequence(node))
        {
            throw ConfigError(
                Context(node),
                "expected YAML sequence");
        }

        for(size_t i = 0;i < node.size();++i)
        {
            cb(i, node[i]);
        }
    }

    template<class Callback>
    static void VisitMap(const Node& node, Callback&& cb)
    {
        if(!IsMap(node))
        {
            throw ConfigError(
                Context(node),
                "expected YAML map");
        }

        for(auto it = node.begin();it != node.end();++it)
        {
            cb(DecodeKey(it->first), it->second);
        }
    }

    static Node MakeMap() { return YAML::Node{YAML::NodeType::Map}; }
    static Node MakeSequence() { return YAML::Node{YAML::NodeType::Sequence}; }
    static void Put(
        Node& map,
        const std::string& key,
        Node node)
    {
        try {
            map[key] = std::move(node);

        } catch(const YAML::Exception& error) {
            throw MakeError(error, &map, "put map value");
        }
    }

    static void Append(Node& sequence, Node node)
    {
        try {
            sequence.push_back(std::move(node));

        } catch(const YAML::Exception& error) {
            throw MakeError(
                error, &sequence, "append sequence value");
        }
    }

private:
    static YAML::NodeType::value Type(const Node& node)
    {
        try {
            return node.Type();

        } catch(const YAML::Exception& error) {
            throw MakeError(error, &node, "inspect node type");
        }
    }

    static ConfigContext ContextFromMark(
        const YAML::Mark& mark) noexcept
    {
        ConfigContext context;
        if(!mark.is_null())
        {
            context.line = static_cast<size_t>(mark.line + 1);
            context.column = static_cast<size_t>(mark.column + 1);
        }
        return context;
    }

    static ConfigError MakeError(
        const YAML::Exception& error,
        const Node* node,
        const std::string& operation)
    {
        ConfigContext context = ContextFromMark(error.mark);
        if(node != nullptr)
        {
            const ConfigContext fallback = Context(*node);
            context = context.withLocationIfEmpty(
                fallback.line,
                fallback.column);
        }
        return ConfigError(
            std::move(context),
            "YAML " + operation + " failed: " + error.msg);
    }

    static std::string DecodeKey(const Node& key)
    {
        try {
            return key.as<std::string>();

        } catch(const YAML::Exception& error) {
            throw MakeError(error, &key, "map key decode");
        }
    }
};

struct ConfigJsonPolicy
{
    using Node = nlohmann::json;

    static std::string PolicyType() { return "JSON"; }

    static Node Parse(const std::string& text)
    {
        try {
            return Node::parse(text);

        } catch(const nlohmann::json::parse_error& error) {
            throw ConfigError(
                ParseErrorContext(text, error.byte),
                std::string("JSON parse failed: ") + error.what());

        } catch(const nlohmann::json::exception& error) {
            throw MakeError(error, "parse");
        }
    }

    static std::string Serialize(const Node& node)
    {
        try {
            return node.dump();

        } catch(const nlohmann::json::exception& error) {
            throw MakeError(error, "serialize");
        }
    }

    static std::string ToInputText(const Node& node) { return Serialize(node); }

    static std::string Key(const Node& node)
    {
        if(!IsMap(node) || node.empty())
        {
            throw ConfigError(
                Context(node),
                "JSON object key is unavailable");
        }
        return Key(node.cbegin());
    }

    static std::string Key(const nlohmann::json::const_iterator& it)
    {
        try {
            return it.key();

        } catch(const nlohmann::json::exception& error) {
            throw MakeError(error, "object key read");
        }
    }

    static std::string Key(
        const nlohmann::json::iterator& it)
    {
        try {
            return it.key();

        } catch(const nlohmann::json::exception& error) {
            throw MakeError(error, "object key read");
        }
    }

    static bool IsNull(const Node& node) { return node.is_null(); }
    static bool IsScalar(const Node& node)
    {
        return node.is_string()
            || node.is_boolean()
            || node.is_number();
    }
    static bool IsSequence(const Node& node) { return node.is_array(); }
    static bool IsMap(const Node& node) { return node.is_object(); }

    static size_t Size(const Node& node)
    {
        try {
            return node.size();

        } catch(const nlohmann::json::exception& error) {
            throw MakeError(error, "read size");
        }
    }

    static ConfigContext Context(const Node&) noexcept
    {
        return {};
    }

    template<typename T>
    static T Decode(const Node& node)
    {
        try {
            return node.get<T>();

        } catch(const nlohmann::json::exception& error) {
            throw MakeError(error, "decode");
        }
    }

    template<typename T>
    static Node Encode(const T& value)
    {
        try {
            return Node(value);

        } catch(const nlohmann::json::exception& error) {
            throw MakeError(error, "encode");
        }
    }

    template<class Callback>
    static void VisitSequence(const Node& node, Callback&& cb)
    {
        if(!IsSequence(node))
        {
            throw ConfigError(
                Context(node),
                "expected JSON array");
        }

        for(size_t i = 0;i < node.size();++i)
        {
            cb(i, node[i]);
        }
    }

    template<class Callback>
    static void VisitMap(const Node& node, Callback&& cb)
    {
        if(!IsMap(node))
        {
            throw ConfigError(
                Context(node),
                "expected JSON object");
        }

        for(auto it = node.cbegin();it != node.cend();++it)
        {
            cb(Key(it), *it);
        }
    }

    static Node MakeMap() { return nlohmann::json::object(); }
    static Node MakeSequence() { return nlohmann::json::array(); }

    static void Put(
        Node& map,
        const std::string& key,
        Node node)
    {
        if(!IsMap(map))
        {
            throw ConfigError(
                Context(map),
                "cannot put a value into a non-object JSON node");
        }

        try {
            map[key] = std::move(node);

        } catch(const nlohmann::json::exception& error) {
            throw MakeError(error, "put object value");
        }
    }

    static void Append(Node& sequence, Node node)
    {
        if(!IsSequence(sequence))
        {
            throw ConfigError(
                Context(sequence),
                "cannot append a value to a non-array JSON node");
        }

        try {
            sequence.push_back(std::move(node));

        } catch(const nlohmann::json::exception& error) {
            throw MakeError(
                error, "append array value");
        }
    }

private:
    static ConfigContext ParseErrorContext(
        const std::string& text,
        size_t one_based_byte)
    {
        ConfigContext context;
        if(one_based_byte == 0)
        {
            return context;
        }

        const size_t error_offset =
            std::min(one_based_byte - 1, text.size());
        context.line = 1;
        context.column = 1;

        for(size_t i = 0;i < error_offset;++i)
        {
            if(text[i] == '\n')
            {
                ++context.line;
                context.column = 1;
            }
            else
            {
                ++context.column;
            }
        }
        return context;
    }

    static ConfigError MakeError(
        const nlohmann::json::exception& error,
        const std::string& operation)
    {
        return ConfigError(
            ConfigContext{},
            "JSON " + operation + " failed: " +
                error.what());
    }
};

}
#endif // __KIT__CONFIG_CODEC_POLICY_H__
