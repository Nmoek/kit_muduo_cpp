/**
 * @file variant.h
 * @brief 自定义实现的类型包装器
 * @author ljk5
 * @version 1.0
 * @date 2026-01-14 14:35:57
 * @copyright Copyright (c) 2026 HIKRayin
 */
#ifndef __KIT_VARIANT_H__
#define __KIT_VARIANT_H__

#include <cstddef>

#if __cplusplus >= 201703L
#include <variant>

namespace kit_muduo {

    template<class... Types>
    class KitVariant : public std::variant<Types...> {
    public:
        using std::variant<Types...>::variant;
        using std::variant<Types...>::operator=;

        size_t index() const noexcept {
            return std::variant<Types...>::index();
        }

        template<class T>
        bool holds_alternative() const {
            return std::holds_alternative<T>(*this);
        }

        template<class T>
        T& get() {
            return std::get<T>(*this);
        }

        template<class T>
        const T& get() const {
            return std::get<T>(*this);
        }

        template<size_t I>
        std::variant_alternative_t<I, std::variant<Types...>>& get() {
            return std::get<I>(*this);
        }

        template<size_t I>
        const std::variant_alternative_t<I, std::variant<Types...>>& get() const {
            return std::get<I>(*this);
        }
    };

}
#else

#include <new>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace kit_muduo {

/**
 * @brief 类型列表工具
 * @tparam Types 
 */
template<class... Types>
struct type_list{ };

/**
 * @brief 获取类型列表中第N个类型
 * @tparam N 
 * @tparam T 
 * @tparam Types 
 */
template<size_t N, class T, class... Types>
struct type_at {
    using type = typename type_at<N - 1, Types...>::type;
};

template<class T, class... Types>
struct type_at<0, T, Types...> {
    using type = T;
};

/**
 * @brief 获取当前类型在类型列表中的索引
 * @tparam T 
 * @tparam Types 
 */
template<class T, class... Types>
struct index_of;


template<class T, class... Types>
struct index_of<T, T, Types...> {
    static const size_t value = 0;
};

template<class T, class U, class... Types>
struct index_of<T, U, Types...> {
    static const size_t value = 1 + index_of<T, Types...>::value;
};

/**
 * @brief 类型是否包含在类型列表中
 * @tparam T 
 * @tparam Types 
 */
template<class T, class... Types>
struct contains;


template<class T, class U, class... Types>
struct contains<T, U, Types...>: std::integral_constant<bool, std::is_same<T, U>::value || contains<T, Types...>::value> { };

template<class T>
struct contains<T>: std::false_type { };

/**
 * @brief std::max({sizeof(Types)...}) 折叠表达式替代
 * @tparam First 
 * @tparam Rests 
 */
template<size_t First, size_t... Rests>
struct max_sizeof;

template<size_t First, size_t Second, size_t... Rests>
struct max_sizeof<First, Second, Rests...> {
    static const size_t value = (First > max_sizeof<Second, Rests...>::value ? First : max_sizeof<Second, Rests...>::value);
};

template<size_t First>
struct max_sizeof<First> {
    static const size_t value = First;
};

/**
 * @brief std::max({alignof(Types)...}) 折叠表达式替代
 * @tparam First 
 * @tparam Rests 
 */
template<size_t First, size_t... Rests>
struct max_alignof;

template<size_t First, size_t Second, size_t... Rests>
struct max_alignof<First, Second, Rests...> {
    static const size_t value = (First > max_alignof<Second, Rests...>::value ? First : max_alignof<Second, Rests...>::value);
};

template<size_t First>
struct max_alignof<First> {
    static const size_t value = First;
};


template<class... Types>
class KitVariant 
{

private:
    // c++17写法
    // static constexpr size_t data_size = (sizeof...(Types) > 0 ? std::max({sizeof(Types)...}) : 1);

    // static constexpr size_t data_align = std::max({alignof(Types)...});

    static const size_t data_size = (sizeof...(Types) > 0 ? max_sizeof<sizeof(Types)...>::value : 1);

    static const size_t data_align = max_alignof<alignof(Types)...>::value;

    using storage_type = typename std::aligned_storage<data_size, data_align>::type;

    template<class T>
    using enable_if_contains = typename std::enable_if<contains<T, Types...>::value>::type;

    template<size_t I>
    using type_at_index = typename type_at<I, Types...>::type;

    // 替代constexpr 作为条件编译. 非常重要!
    template<size_t I>
    struct index_tag { static const size_t value = I; };

    /** 内存布局
        ---------------------------
        |     data_   |   index_  |
        |  联合体空间  |   size_t  |
        --------------------------
    */
    storage_type data_;
    size_t index_;

public:
    /**
     * @brief 默认构造(只构造第一个类型)
     */
    KitVariant()
        :index_(0)
    {
        static_assert(sizeof...(Types) > 0, "variant must contain at least one type");

        construct_impl(index_tag<0>());
    }

    ~KitVariant()
    {
        destroy();
    }

    template<typename T, typename = enable_if_contains<T>>
    KitVariant(const T& value)
        :index_(index_of<T, Types...>::value)
    {
        new (&data_) T(value);
    }


    template<typename T, typename = enable_if_contains<T>>
    KitVariant(T&& value)
        :index_(index_of<T, Types...>::value)
    {
        new (&data_) T(std::forward<T>(value));
    }

    KitVariant(const KitVariant& other)
        :index_(other.index_)
    {
        copy_construct(other);
    }

    KitVariant(KitVariant&& other)
        :index_(other.index_)
    {
        move_construct(other);
    }

    KitVariant& operator=(const KitVariant& other)
    {
        if (this == &other)
            return *this;
        
        destroy();
        index_ = other.index_;
        copy_construct(other);
        return *this;
    }

    KitVariant& operator=(KitVariant&& other)
    {
        if (this == &other)
            return *this;

        destroy();
        index_ = other.index_;
        move_construct(other);
        return *this;
    }

    template<typename T, typename = enable_if_contains<T>>
    KitVariant& operator=(const T& value)
    {
        destroy();
        index_ = index_of<T, Types...>::value;
        new (&data_) T(value);
        return *this;
    }


    template<typename T, typename = enable_if_contains<T>>
    KitVariant& operator=(T&& value)
    {
        destroy();
        index_ = index_of<T, Types...>::value;
        new (&data_) T(std::forward<T>(value));
        return *this;
    }

    size_t index() { return index_; }

    /**
     * @brief 检查是否包含某个类型
     * @tparam T 
     * @return true 
     * @return false 
     */
    template<class T>
    bool holds_alternative() const { return index_ == index_of<T, Types...>::value; }

    /**
     * @brief 获取真值(类型安全)
     * @tparam T 
     * @return T& 
     */
    template<class T>
    T& get()
    {
        if(!holds_alternative<T>())
        {
            throw std::runtime_error("bad variant access");
        }

        return *reinterpret_cast<T*>(&data_);
    }

    template<class T>
    const T& get() const
    {
        if(!holds_alternative<T>())
        {
            throw std::runtime_error("bad variant access");
        }

        return *reinterpret_cast<const T*>(&data_);
    }

    /**
     * @brief 通过索引访问真值
     * @tparam I 
     * @return type_at_index<T>& 
     */
    template<size_t I>
    type_at_index<I>& get()
    {
        static_assert(I < sizeof...(Types), "index out of bounds");
        if(index_ != I)
        {
            throw std::runtime_error("bad variant access");
        }

        return *reinterpret_cast<type_at_index<I>*>(&data_);
    }

    template<size_t I>
    const type_at_index<I>& get() const
    {
        static_assert(I < sizeof...(Types), "index out of bounds");
        if(index_ != I)
        {
            throw std::runtime_error("bad variant access");
        }

        return *reinterpret_cast<const type_at_index<I>*>(&data_);
    }



private:

    template<size_t I>
    void construct_impl(index_tag<I>)
    {
        // 原地构造函数
        new (&data_) type_at_index<I>();
    }

    void construct_impl(index_tag<sizeof...(Types)>)
    {
        throw std::runtime_error("invalid variant index");
    }

    void copy_construct(const KitVariant& other)
    {
        copy_construct_impl(other, index_tag<0>());
    }

    template<size_t I>
    void copy_construct_impl(const KitVariant& other, index_tag<I>)
    {

        if(I == index_)
        {
            using T = type_at_index<I>;
            new (&data_) T(other.get<I>());
            return;
        }
        
        // 依次递归拷贝
        copy_construct_impl(other, index_tag<I + 1>());
    }

    void copy_construct_impl(const KitVariant& other, index_tag<sizeof...(Types)>) 
    {
        throw std::runtime_error("invalid variant index");
    }

    void move_construct(KitVariant&& other)
    {
        move_construct_impl(std::move(other), index_tag<0>());
    }

    template<size_t I>
    void move_construct_impl(KitVariant && other, index_tag<I> )
    {
        if(I == index_)
        {
            using T = type_at_index<I>;
            new (&data_) T(std::move(other.get<I>()));
            return;
        }

        move_construct_impl(std::move(other), index_tag<I + 1>());
        
    }

    void move_construct_impl(KitVariant && other, index_tag<sizeof...(Types)>) 
    {
        throw std::runtime_error("invalid variant index");
    }

    template<size_t I>
    void destroy_impl(index_tag<I>)
    {

        if(I == index_)  // 递归遍历
        {
            using T = type_at_index<I>;

            // 手动调析构函数
            reinterpret_cast<T*>(&data_)->~T();
            return;
        }
      
        destroy_impl(index_tag<I + 1>());
    }

    void destroy_impl(index_tag<sizeof...(Types)>)
    {
        throw std::runtime_error("invalid variant index");
    }

    void destroy()
    {
        // 模版递归展开
        destroy_impl(index_tag<0>());
    }


};


}
#endif

#endif