/**
 * @file config_codec.h
 * @brief 配置序列化/反序列化处理策略
 * @author Kewin Li
 * @version 1.0
 * @date 2026-07-30 18:24:26
 * @copyright Copyright (c) 2026 Kewin Li
 */
#ifndef __KIT_CONFIG_CODEC_H__
#define __KIT_CONFIG_CODEC_H__

#include "base/config_context.h"

#include <string>
#include <type_traits>
#include <typeinfo>
#include <vector>
#include <set>
#include <map>
#include <unordered_map>
#include <unordered_set>
#include <list>

namespace kit_muduo {

template<typename T>
inline constexpr bool kDependentFalse = false;


template<typename T>
inline constexpr bool kConfigScalar = std::is_same_v<T, std::string> || std::is_same_v<T, bool> || (std::is_arithmetic_v<T> && !std::is_same_v<T, char>);


template<typename T, typename Policy, typename Enable = void>
struct ConfigCodec
{
    static_assert(kDependentFalse<T>,
        "ConfigCodec<T,NodePolicy> requires an explicit specialization");
};

template<typename T, typename Policy>
struct ConfigCodec<T, Policy, 
    std::enable_if_t<kConfigScalar<T>>>
{
    using Node = typename Policy::Node;

    static T Decode(const Node &node)
    {
        return Policy::template Decode<T>(node);
    }

    static Node Encode(const T& value)
    {
        return Policy::template Encode<T>(value);
    }

};

namespace config_codec_detail {

inline std::string SequencePath(size_t index)
{
    return "[" + std::to_string(index) + "]";
}

inline std::string MapPath(const std::string& key)
{
    std::string escaped;
    for(char ch : key)
    {
        // 增加转义 字符
        if(ch == '\\' || ch == '"')
        {
            escaped.push_back('\\');
        }
        escaped.push_back(ch);
    }
    return "[\"" + escaped + "\"]";
}

/**
 * @brief 序列容器通用 解码处理模版函数
 * @tparam Container 
 * @tparam Element 
 * @tparam Policy 
 * @param node 
 * @return Container 
 */
template<typename Sequence, typename Element, typename Policy>
Sequence DecodeSequence(const typename Policy::Node &node)
{
    using Node = typename Policy::Node;
    if(!Policy::IsSequence(node))
    {
        throw ConfigError(Policy::Context(node), "expected sequence");
    }

    Sequence result;

    // 访问者模式, 把不同格式的处理隐藏到适配器类后面
    Policy::VisitSequence(node, [&result](size_t index, const Node &child){
        try {
            result.emplace_back(ConfigCodec<Element, Policy>::Decode(child));
        } catch(const ConfigError& e) {
            throw e.prependPath(SequencePath(index));
        }
    });

    return result;

}

template<typename Sequence, typename Element, typename Policy>
typename Policy::Node EncodeSequence(const Sequence &contaniner)
{
    auto node = Policy::MakeSequence();
    size_t index = 0;

    for(auto &ele : contaniner)
    {
        try {
            Policy::Append(node, ConfigCodec<Element, Policy>::Encode(ele));
        } catch(const ConfigError &e) {

            throw e.prependPath(SequencePath(index));
        }
        ++index;
    }
    return node;
}

template<typename Map, typename Val, typename Policy>
Map DecodeStringMap(const typename Policy::Node &node)
{
    using Node = typename Policy::Node;

    if(!Policy::IsMap(node))
    {
        throw ConfigError(Policy::Context(node), "expected map");
    }

    Map result;
    Policy::VisitMap(node, 
        [&](const std::string &key, const Node &child)
    {
        try {
            auto val = ConfigCodec<Val, Policy>::Decode(child);

            if(!result.emplace(key, std::move(val)).second)
            {
                throw ConfigError(Policy::Context(child), "deplicate map key: " + key);
            }

        }catch(const ConfigError &e) {
            throw e.prependPath(MapPath(key));
        }
    });

    return result;
}

template<typename Map, typename Val, typename Policy>
typename Policy::Node EncodeStringMap(const Map &map)
{
    auto node = Policy::MakeMap();
    std::string tmp_key;
    for(auto &[key, val] : map)
    {
        tmp_key = key;
        try {
            Policy::Put(node, key, ConfigCodec<Val, Policy>::Encode(val));
        }catch(const ConfigError &e) {
            throw e.prependPath(MapPath(tmp_key));
        }
    }
    return node;
}


} // namespace config_codec_detail


/**
 * @brief 模板偏特化 Node <----> vector<>
 * @tparam T 
 * @tparam Policy 
 */
template<typename T, typename Policy>
class ConfigCodec<std::vector<T>, Policy>
{
public:
    using Sequence = std::vector<T>;
    using Node = typename Policy::Node;

    static Sequence Decode(const Node& node)
    {
        return config_codec_detail::DecodeSequence<Sequence, T, Policy>(node);
    }

    static Node Encode(const Sequence &value)
    {
        return config_codec_detail::EncodeSequence<Sequence, T, Policy>(value);
    }

};


/**
 * @brief 模板偏特化 Node <----> list<>
 * @tparam T 
 * @tparam Policy 
 */
template<typename T, typename Policy>
class ConfigCodec<std::list<T>, Policy>
{
public:
    using Sequence = std::list<T>;
    using Node = typename Policy::Node;

    static Sequence Decode(const Node& node)
    {
        return config_codec_detail::DecodeSequence<Sequence, T, Policy>(node);
    }

    static Node Encode(const Sequence &value)
    {
        return config_codec_detail::EncodeSequence<Sequence, T, Policy>(value);
    }

};

// TODO set/unordered_set容器 暂不实现


/**
 * @brief 模板偏特化 string <---->map<>
 * @tparam T1 注意 T1必须是标量, 否则map无法排序查找
 * @tparam T2 
 * @tparam Policy 
 */
template<class T, typename Policy>
class ConfigCodec<std::map<std::string, T>, Policy>
{
public:
    using Map = std::map<std::string, T>;
    using Node = typename Policy::Node;

    static Map Decode(const Node& node)
    {
        return config_codec_detail::DecodeStringMap<Map, T, Policy>(node);
    }

    static typename Policy::Node Encode(const Map& value)
    {
        return config_codec_detail::EncodeStringMap<Map, T, Policy>(value);
    }

};

/**
 * @brief 模板偏特化 string---->unordered_map<>
 * @tparam T 
 * @tparam Policy 
 */
template<class T, typename Policy>
class ConfigCodec<std::unordered_map<std::string, T>, Policy>
{
public:
    using Map = std::unordered_map<std::string, T>;
    using Node = typename Policy::Node;

    static Map Decode(const Node& node)
    {
        return config_codec_detail::DecodeStringMap<Map, T, Policy>(node);
    }

    static typename Policy::Node Encode(const Map& value)
    {
        return config_codec_detail::EncodeStringMap<Map, T, Policy>(value);
    }

};

}
#endif  //__KIT_CONFIG_CODEC_H__