/**
 * @file multiform.h
 * @brief MultiForm格式序列/反序列化
 * @author Kewin Li
 * @version 1.0
 * @date 2026-06-17 23:19:04
 * @copyright Copyright (c) 2026 Kewin Li
 */
#ifndef __KIT_MULTIFORM_H__
#define __KIT_MULTIFORM_H__

#include "net/http/http_content.h"

#include <stdexcept>
#include <type_traits>
#include <unordered_map>
#include <vector>

namespace kit_muduo::http {

/**
 * @brief multipart/form-data类型转换器
 * @tparam T 
 */
/**
 * @brief Represents a single part in multipart form data
 */
struct FormPart {
    /// @brief Content-Disposition部分'name'
    std::string name;
    /// @brief 上传文件时Content-Disposition部分'filename'
    std::string filename;
    /// @brief 每个FormPart带的Content元数据
    ContentMeta meta;
    /// @brief 每个FormPart的实际数据
    std::vector<uint8_t> data;
    /// @brief 其他头部字段
    std::unordered_map<std::string, std::string> headers;
    
    bool isFile() const 
    { 
        return !filename.empty();
    }

    bool isDataEmpty() const
    {
        return data.empty();
    }

    const std::vector<uint8_t>& bytes() const
    {
        return data;
    }

    std::string strs() const
    {
        return std::string(data.begin(), data.end());
    }

    ContentView toContentView() const
    {
        return {
            .data = data.data(),
            .size = data.size(),
            .meta = meta,
        };
    }

};

/**
 * @brief 标准异常封装
 */
class MultiFormException: public std::runtime_error
{
public:
    explicit MultiFormException(const std::string &message)
        : std::runtime_error(message)
    {

    }
};

class MultiForm;

namespace detail {

template<typename T>
auto TestAdlFormMultiform(int) -> decltype(from_multiform(std::declval<const MultiForm&>(), std::declval<T&>()), std::true_type{});

template<typename T>
std::false_type TestAdlFormMultiform(...);

template<typename T>
constexpr bool kHasAdlFormMultiform = decltype(TestAdlFormMultiform<T>(0))::value;

};

class MultiForm
{
public:
    // 注意: 这样数据结构是允许name重复
    using PartList = std::vector<FormPart>;
    using FieldMap = std::unordered_map<std::string, PartList>;

    MultiForm() = default;

    /// @brief 从已经组织好的 part 列表构造 multipart 表单
    explicit MultiForm(PartList parts);

    /// @brief 从协议项保存的 fields 描述构造 multipart 表单
    explicit MultiForm(const std::vector<char>& config_data);

    /// @brief 按 multipart/form-data 线格式序列化，返回不含 HTTP 头的 body
    std::vector<uint8_t> serialize(const std::string& boundary) const;

    static MultiForm parse(const uint8_t *data, size_t len, std::string boundary);

    static MultiForm parse(const std::string &body, std::string boundary);

    static MultiForm parse(const std::vector<uint8_t> &data, std::string boundary);

    bool empty() const { return fields_.empty(); }

    bool contains(const std::string &name) const { return fields_.find(name) != fields_.end(); }

    size_t count(const std::string &name) const
    {
        auto it = fields_.find(name);
        return it == fields_.end() ? 0 : it->second.size();
    }

    const FieldMap& fields() const { return fields_; }

    const PartList& all(const std::string &name) const;

    const FormPart& at(const std::string &name) const;

    template<typename T>
    T get() const
    {
        T out{};
        get_to(out);
        return out;
    }

    template<typename T>
    void get_to(T& out) const
    {
        ConvertObject(*this, out);
    }

private:

    void addPart(FormPart part);

    template<typename T>
    static void ConvertObject(const MultiForm &form, T&out)
    {
        if constexpr (detail::kHasAdlFormMultiform<T>)
        {
            // 注意: 这里使用ADL机制
            from_multiform(form, out);
        }
        else
        {
            throw MultiFormException("multipart target unsupported");
        }
    }


private:
    FieldMap fields_;
    PartList ordered_parts_;
};


}
#endif //__KIT_MULTIFORM_H__
