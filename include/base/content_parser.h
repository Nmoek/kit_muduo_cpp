/**
 * @file content_parser.h
 * @brief 文档/报文解析
 * @author Kewin Li
 * @version 1.0
 * @date 2025-06-12 01:39:43
 * @copyright Copyright (c) 2025 Kewin Li
 */
#ifndef __KIT_DOC_PARSER_H__
#define __KIT_DOC_PARSER_H__

#include <string>
#include "nlohmann/json.hpp"
#include "base/base_log.h"


namespace kit_muduo {

using nljson = nlohmann::json;

template<typename T>
class NlohmannJsonConverter;

class ContentConverter
{
public:
    virtual ~ContentConverter() = default;

    /**
     * @brief 具体文本格式 ==> 自定义对象转换
     * @param[in] data 文本数据
     * @return true 
     * @return false 
     */
    virtual bool contentToObj(const std::string& data) noexcept = 0;

    /**
     * @brief 自定义对象转换 ==> 具体文本格式
     * @return std::string 
     */
    virtual std::string objToContent() noexcept = 0;
    
public:
    /**
     * @brief 简单工厂创建解析器
     * @param type
     * @return ContentConverter::Ptr
     */
    template<typename T>
    static std::shared_ptr<ContentConverter> Creator(int32_t type)
    {
        switch (type)
        {
            // case http::ContentType::kJsonType:
            case 1:  // TODO 统一重新抽象 文本格式类型
            {
                return std::make_shared<NlohmannJsonConverter<T>>();
            }
    
            default:
                return nullptr;
        }
        // 返回空说明 不需要解析/类型错误等其他问题
        return nullptr;
    }

};


/**
 * @brief nlohmann json类型转换器
 * @tparam T 表示 对象T <---> json
 */
template<typename T>
class NlohmannJsonConverter: public ContentConverter
{
public:
    NlohmannJsonConverter() = default;
    explicit NlohmannJsonConverter(const T& object)
        :_object(object)
    {}
    
    // explicit NlohmannJsonConverter(const nljson& root, const T& object)
    //     :_root(root)
    //     ,_object(object)
    // {
    //     _success = !_root.empty();
    // }

    ~NlohmannJsonConverter() = default;

    /**
     * @brief 具体文本格式 ==> 自定义对象转换
     * @param[in] data 文本数据
     * @return true 
     * @return false 

     注意: 这里接口不能这么设计 是因为虚接口无法进行多态 得迂回处理
     bool ContentToObj(const std::string& data, T *obj) = 0; 不支持
     */
    bool contentToObj(const std::string& data) noexcept override
    {
        try
        {
            _root = nljson::parse(data);
            // 转换成功后 需要主动调用一下getObj()
            // TODO 只能转换基础类型 复合类型无法转换
            _object = _root.get<T>();
        }
        catch(const std::exception& e)
        {
            PARSER_ERROR() << "contentToObj parse error! " << e.what() << std::endl;
            return false;
        }
        return true;
    }

    /**
     * @brief 自定义对象转换 ==> 具体文本格式
     * @return std::string 
     */
    std::string objToContent() noexcept override
    {
        std::string content;
        try
        {
            _root = _object;
            content = _root.dump();
        }
        catch(const std::exception& e)
        {
            PARSER_ERROR() << "objToContent parse error! " << e.what() << std::endl;
            return "";
        }
        
        return content;
    }

    T getObj() const { return _object; }
    void setObj(const T& obj) { _object = obj; }

    void setRoot(const nljson& root) { _root = root; }
    nljson getRoot() const { return _root; }

private:
    /// @brief json句柄
    nljson _root;
    /// @brief 解析对象
    T _object;
};

/**
 * @brief multipart/form-data类型转换器
 * @tparam T 
 */
/**
 * @brief Represents a single part in multipart form data
 */
struct FormPart {
    std::string name;               // Field name from Content-Disposition
    std::string filename;           // Filename if this is a file upload
    std::string content_type;       // Content-Type header value
    std::vector<char> data;         // Part data content (can be binary)
    std::unordered_map<std::string, std::string> headers; // Other headers
    
    bool is_file() const { return !filename.empty(); }
};

/**
 * @brief Parser for multipart/form-data content
 */
class MultiFormConvert 
{
public:
    using PartMap = std::unordered_map<std::string, FormPart>;

    /**
     * @brief Parse multipart form data
     * @param content The raw multipart form data content
     * @param content_type The Content-Type header containing boundary
     * @return Vector of parsed form parts
     */
    static PartMap parse(const std::string& content, 
                                     const std::string& content_type);


private:
    /**
     * @brief Extract boundary from Content-Type header
     */
    static std::string extract_boundary(const std::string& content_type);
    
    /**
     * @brief Parse a single part of multipart data
     */
    static FormPart parse_part(const std::string& part_data);
    
    /**
     * @brief Parse Content-Disposition header
     */
    static void parse_content_disposition(const std::string& line, FormPart& part);
    
    /**
     * @brief Parse other part headers
     */
    static void parse_header_line(const std::string& line, FormPart& part);
};


//TODO xml

}   //kit_muduo


#endif