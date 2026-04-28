/**
 * @file test_variant.cpp
 * @brief 自定义类型包装器测试
 * @author ljk5
 * @version 1.0
 * @date 2026-01-14 15:34:23
 * @copyright Copyright (c) 2026 HIKRayin
 */

#include "gtest/gtest.h"
#include "base/variant.h"
#include "test_log.h"

#include "nlohmann/json.hpp"


#include <iostream>

using nljson = nlohmann::json;
using namespace kit_muduo;

template<typename T>
class TypedField;

// 字段基类
class Field {
public:
    virtual ~Field() = default;
    
    // 从字节数组解析
    virtual void from_bytes(const std::vector<uint8_t>& data) = 0;
    
    // 转换为字节数组
    virtual std::vector<uint8_t> to_bytes() const = 0;
    
    // 获取字节长度
    virtual size_t size() const = 0;
    
    // 通过抽象类设置真值（类型安全的模板方法）
    template<typename T>
    void set_value(const T& value) {
        auto* derived = dynamic_cast<TypedField<T>*>(this);
        if (!derived) {
            throw std::runtime_error("Type mismatch in set_value");
        }
        derived->set_value(value);
    }
    
    // 通过抽象类获取真值（类型安全的模板方法）
    template<typename T>
    T get_value() {
        auto* derived = dynamic_cast<TypedField<T>*>(this);
        if (!derived) {
            throw std::runtime_error("Type mismatch in get_value");
        }
        return derived->get_value();
    }
    
    // 获取类型信息（用于运行时类型检查）
    virtual const std::type_info& type_info() const = 0;
    
    // 通用的值设置接口（使用void*，需要谨慎使用）
    virtual void set_value_generic(const void* value) = 0;
    
    // 通用的值获取接口（使用void*，需要谨慎使用）
    virtual void get_value_generic(void* value) const = 0;
};
    
    // 模板字段类
template<typename T>
class TypedField : public Field {
public:
    TypedField() = default;
    
    explicit TypedField(T value) : value_(value) {}
    
    // 设置真值（具体实现）
    void set_value(T value) {
        value_ = value;
    }
    
    // 获取真值（具体实现）
    T get_value() const {
        return value_;
    }
    
    // 从字节数组解析
    void from_bytes(const std::vector<uint8_t>& data) override {
        if (data.size() < sizeof(T)) {
            throw std::runtime_error("Data size too small");
        }
        std::memcpy(&value_, data.data(), sizeof(T));
    }
    
    // 转换为字节数组
    std::vector<uint8_t> to_bytes() const override {
        std::vector<uint8_t> result(sizeof(T));
        std::memcpy(result.data(), &value_, sizeof(T));
        return result;
    }
    
    // 获取字节长度
    size_t size() const override {
        return sizeof(T);
    }
    
    // 获取类型信息
    const std::type_info& type_info() const override {
        return typeid(T);
    }
    
    // 通用的值设置接口
    void set_value_generic(const void* value) override {
        if (value) {
            value_ = *static_cast<const T*>(value);
        }
    }
    
    // 通用的值获取接口
    void get_value_generic(void* value) const override {
        if (value) {
            *static_cast<T*>(value) = value_;
        }
    }

private:
    T value_;
};

// 具体类型别名定义
using Int8Field = TypedField<int8_t>;
using UInt8Field = TypedField<uint8_t>;
using Int16Field = TypedField<int16_t>;
using UInt16Field = TypedField<uint16_t>;
using Int32Field = TypedField<int32_t>;
using UInt32Field = TypedField<uint32_t>;
using Int64Field = TypedField<int64_t>;
using UInt64Field = TypedField<uint64_t>;
using FloatField = TypedField<float>;
using DoubleField = TypedField<double>;

// 智能字段句柄

class FieldHandle {
public:
    explicit FieldHandle(std::shared_ptr<Field> field) 
        : field_(field) 
    {
        // 构造时验证类型
        // if (field_->type_info() != typeid(T)) {
        //     throw std::runtime_error("Field type mismatch in handle construction");
        // }
    }
    
    // 赋值操作 - 类型安全
    template<typename T>
    FieldHandle& operator=(const T& value) {
        field_->set_value(value);
        return *this;
    }
    
    // 隐式转换到值类型
    template<typename T>
    operator T() const {
        return field_->get_value<T>();
    }
    
    // 访问底层字段的方法
    Field& field() { return *field_; }
    const Field& field() const { return *field_; }

    std::shared_ptr<Field> fieldPtr() const { return field_; }

    // 代理到底层字段的接口
    std::vector<uint8_t> to_bytes() const { return field_->to_bytes(); }
    void from_bytes(const std::vector<uint8_t>& data) { field_->from_bytes(data); }
    size_t size() const { return field_->size(); }

// 工厂函数
    template<typename T>
    static FieldHandle create_field(T initial_value = T()) {
        return FieldHandle(std::make_shared<TypedField<T>>(initial_value));
    }

private:
    std::shared_ptr<Field> field_;
};



using TestVariantType = kit_muduo::KitVariant<int32_t, double, std::string, int64_t>;
TestVariantType v;

template<class T>
void test_set_val(const T& val)
{
    try {
        v = val;
    }catch(const std::exception &e) {
        TEST_DEBUG() << "赋值失败！" << e.what() << ", " << val << std::endl;
    }
}

template<class T>
T test_get_val()
{
    return v.get<T>();
}

TEST(TestVariant, test)
{

    TEST_DEBUG() << "默认构造： " << v.index() << ", size= " << sizeof(v) << std::endl;

    // v = 3.14159;  // 按浮点型赋值
    test_set_val(3.14159);


    TEST_DEBUG() << "第二种参数访问： " << v.index() << ", " 
        << v.get<1>() << ", " 
        << v.get<double>() << ", " 
        << typeid(v.get<1>()).name() << std::endl;



    test_set_val((int64_t)100);
    // TEST_DEBUG() << "第一种参数访问： " << v.get<0>() << ", " << test_get_val<int32_t>() << ", 类型:" << typeid(v.get<0>()).name() << std::endl;

    int32_t val = test_get_val<int32_t>();
    TEST_DEBUG() << "第四种种参数访问： " << val << "," << v.get<3>() << ", " << test_get_val<int64_t>() << ", 类型:" << typeid(v.get<3>()).name() << std::endl;
    
    // TEST_DEBUG() << "第三种类参数访问： " << v.get<2>() << ", " << v.get<std::string>() << ", 类型:" << typeid(v.get<2>()).name() << std::endl;
    // v = 11;
    // TEST_DEBUG() << "第一个参数访问： " << v.get<0>() << ", " << v.get<int32_t>() << ", 类型判断:" << (v.get<int>())<< std::endl;

    // v = 3.14159;
    // TEST_DEBUG() << "第二个参数访问： " << v.get<1>() << ", " << v.get<double>() << std::endl;

    // v = std::string("hello word");
    // TEST_DEBUG() << "第二个参数访问： " << v.get<2>() << ", " << v.get<std::string>() << std::endl;

    // try {
    //     std::string tmp = v.get<2>();
    //     TEST_DEBUG() << tmp << std::endl;
    // }catch(const std::exception& e) {
    //     TEST_DEBUG() << "访问不存在的参数 5： " << e.what() << std::endl;
    // }

    // try {
    //     int64_t tmp = v.get<int64_t>();
    //     TEST_DEBUG() << tmp << std::endl;

    // }catch(const std::exception& e) {
    //     TEST_DEBUG() << "访问不存在的参数 int64_t： " << e.what() <<  std::endl;
    // }


}



TEST(TestVariant, DISABLED_test2)
{
    // 创建时就确定具体类型
    auto int_field = FieldHandle::create_field<int32_t>(0);
    auto int64_field = FieldHandle::create_field<int64_t>(0);
    auto float_field = FieldHandle::create_field<float>(0.0f);
    
    // 使用时完全类型安全，不需要显式指定类型
    int_field = (size_t)42;           // 编译器知道是 int32_t
    // int_field.fieldPtr()->set_value((size_t)666);

    int64_field = (int32_t)888;
    // int64_field.fieldPtr()->set_value((int32_t)888);

    float_field = 3.14f;      // 编译器知道是 float
    
    // 可以像普通值一样使用
    int64_t value = int64_field;
    std::cout << "Value: " << value << std::endl;
    
}


int main(int argc, char* argv[])
{
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}