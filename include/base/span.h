/**
 * @file span.h
 * @brief C++14/17适配span
 * @author Kewin Li
 * @version 1.0
 * @date 2026-09-08 21:01:08
 * @copyright Copyright (c) 2026 Kewin Li
 */
#ifndef __KIT_MUDUO_BASE_SPAN_H__
#define __KIT_MUDUO_BASE_SPAN_H__

#if defined(_MSVC_LANG)
#define KIT_MUDUO_SPAN_CXX_STANDARD _MSVC_LANG
#else
#define KIT_MUDUO_SPAN_CXX_STANDARD __cplusplus
#endif

#if KIT_MUDUO_SPAN_CXX_STANDARD < 201402L
#error "base/span.h requires C++14 or later"
#elif KIT_MUDUO_SPAN_CXX_STANDARD >= 202002L

#include <cstddef>
#include <span>

namespace kit_muduo {

using std::dynamic_extent;

template<typename ElementType, std::size_t Extent = dynamic_extent>
using Span = std::span<ElementType, Extent>;

} // namespace kit_muduo

#else

#include <array>
#include <cassert>
#include <cstddef>
#include <iterator>
#include <type_traits>
#include <utility>

namespace kit_muduo {

constexpr std::size_t dynamic_extent = static_cast<std::size_t>(-1);

template<typename ElementType, std::size_t Extent = dynamic_extent>
class Span;

namespace span_detail {

template<typename...>
struct MakeVoid
{
    using type = void;
};

template<typename... Types>
using VoidT = typename MakeVoid<Types...>::type;

template<typename From, typename To>
struct IsElementCompatible : std::is_convertible<From (*)[], To (*)[]>
{};

template<typename Pointer, typename ElementType>
struct IsCompatibleDataPointer : std::false_type
{};

template<typename From, typename ElementType>
struct IsCompatibleDataPointer<From*, ElementType>
    : IsElementCompatible<From, ElementType>
{};

template<typename Type>
struct IsSpan : std::false_type
{};

template<typename ElementType, std::size_t Extent>
struct IsSpan<Span<ElementType, Extent>> : std::true_type
{};

template<typename Type>
struct IsStdArray : std::false_type
{};

template<typename ElementType, std::size_t Size>
struct IsStdArray<std::array<ElementType, Size>> : std::true_type
{};

template<typename Container, typename ElementType, typename = void>
struct IsCompatibleContainer : std::false_type
{};

template<typename Container, typename ElementType>
struct IsCompatibleContainer<Container, ElementType,
    VoidT<decltype(std::declval<Container&>().data()),
        decltype(std::declval<Container&>().size())>>
{
private:
    using ContainerType = typename std::remove_cv<Container>::type;
    using DataPointer = typename std::remove_cv<typename std::remove_reference<
        decltype(std::declval<Container&>().data())>::type>::type;

public:
    static constexpr bool value =
        !IsSpan<ContainerType>::value
        && !IsStdArray<ContainerType>::value
        && IsCompatibleDataPointer<DataPointer, ElementType>::value
        && std::is_convertible<decltype(std::declval<Container&>().size()),
            std::size_t>::value;
};

template<std::size_t Extent, std::size_t Offset, std::size_t Count>
struct SubspanExtent
{
    static constexpr std::size_t value = Count != dynamic_extent
        ? Count
        : (Extent != dynamic_extent ? Extent - Offset : dynamic_extent);
};

} // namespace span_detail

template<typename ElementType, std::size_t Extent>
class Span
{
    static_assert(std::is_object<ElementType>::value,
        "Span element type must be an object type");

public:
    using element_type = ElementType;
    using value_type = typename std::remove_cv<ElementType>::type;
    using size_type = std::size_t;
    using difference_type = std::ptrdiff_t;
    using pointer = element_type*;
    using const_pointer = const element_type*;
    using reference = element_type&;
    using const_reference = const element_type&;
    using iterator = pointer;
    using reverse_iterator = std::reverse_iterator<iterator>;

    static constexpr size_type extent = Extent;

    template<std::size_t E = Extent,
        typename std::enable_if<E == dynamic_extent || E == 0, int>::type = 0>
    constexpr Span() noexcept
        :data_(nullptr),
        size_(0)
    {}

    constexpr Span(const Span&) noexcept = default;
    constexpr Span& operator=(const Span&) noexcept = default;

    template<typename U, std::size_t E = Extent,
        typename std::enable_if<E == dynamic_extent
            && span_detail::IsElementCompatible<U, element_type>::value,
            int>::type = 0>
    constexpr Span(U* data, size_type size) noexcept
        :data_(data),
        size_(size)
    {}

    template<typename U, std::size_t E = Extent,
        typename std::enable_if<E != dynamic_extent
            && span_detail::IsElementCompatible<U, element_type>::value,
            int>::type = 0>
    explicit constexpr Span(U* data, size_type size) noexcept
        :data_(data),
        size_(size)
    {
        assert(size == Extent);
    }

    template<typename U, std::size_t E = Extent,
        typename std::enable_if<E == dynamic_extent
            && span_detail::IsElementCompatible<U, element_type>::value,
            int>::type = 0>
    constexpr Span(U* first, U* last) noexcept
        :Span(first, first == last ? 0 : static_cast<size_type>(last - first))
    {
        assert(first <= last);
    }

    template<typename U, std::size_t E = Extent,
        typename std::enable_if<E != dynamic_extent
            && span_detail::IsElementCompatible<U, element_type>::value,
            int>::type = 0>
    explicit constexpr Span(U* first, U* last) noexcept
        :Span(first, first == last ? 0 : static_cast<size_type>(last - first))
    {
        assert(first <= last);
    }

    template<typename U, std::size_t Size,
        typename std::enable_if<(Extent == dynamic_extent || Extent == Size)
            && span_detail::IsElementCompatible<U, element_type>::value,
            int>::type = 0>
    constexpr Span(U (&array)[Size]) noexcept
        :data_(array),
        size_(Size)
    {}

    template<typename U, std::size_t Size,
        typename std::enable_if<(Extent == dynamic_extent || Extent == Size)
            && span_detail::IsElementCompatible<U, element_type>::value,
            int>::type = 0>
    constexpr Span(std::array<U, Size>& array) noexcept
        :data_(array.data()),
        size_(Size)
    {}

    template<typename U, std::size_t Size,
        typename std::enable_if<(Extent == dynamic_extent || Extent == Size)
            && span_detail::IsElementCompatible<const U, element_type>::value,
            int>::type = 0>
    constexpr Span(const std::array<U, Size>& array) noexcept
        :data_(array.data()),
        size_(Size)
    {}

    template<typename Container, std::size_t E = Extent,
        typename std::enable_if<E == dynamic_extent
            && span_detail::IsCompatibleContainer<Container, element_type>::value,
            int>::type = 0>
    constexpr Span(Container& container)
        :data_(container.data()),
        size_(static_cast<size_type>(container.size()))
    {}

    template<typename Container, std::size_t E = Extent,
        typename std::enable_if<E != dynamic_extent
            && span_detail::IsCompatibleContainer<Container, element_type>::value,
            int>::type = 0>
    explicit constexpr Span(Container& container)
        :data_(container.data()),
        size_(static_cast<size_type>(container.size()))
    {
        assert(size_ == Extent);
    }

    template<typename U, std::size_t OtherExtent,
        typename std::enable_if<
            span_detail::IsElementCompatible<U, element_type>::value
            && (Extent == dynamic_extent || OtherExtent != dynamic_extent)
            && (Extent == dynamic_extent || Extent == OtherExtent),
            int>::type = 0>
    constexpr Span(const Span<U, OtherExtent>& other) noexcept
        :data_(other.data()),
        size_(other.size())
    {}

    template<typename U, std::size_t OtherExtent,
        typename std::enable_if<
            span_detail::IsElementCompatible<U, element_type>::value
            && Extent != dynamic_extent && OtherExtent == dynamic_extent,
            int>::type = 0>
    explicit constexpr Span(const Span<U, OtherExtent>& other) noexcept
        :data_(other.data()),
        size_(other.size())
    {
        assert(size_ == Extent);
    }

    constexpr iterator begin() const noexcept { return data_; }

    constexpr iterator end() const noexcept { return offsetPointer(size_); }

    reverse_iterator rbegin() const noexcept { return reverse_iterator(end()); }

    reverse_iterator rend() const noexcept { return reverse_iterator(begin()); }

    constexpr reference front() const noexcept
    {
        assert(!empty());
        return data_[0];
    }

    constexpr reference back() const noexcept
    {
        assert(!empty());
        return data_[size_ - 1];
    }

    constexpr reference operator[](size_type index) const noexcept
    {
        assert(index < size_);
        return data_[index];
    }

    constexpr pointer data() const noexcept { return data_; }

    constexpr size_type size() const noexcept { return size_; }

    constexpr size_type size_bytes() const noexcept
    {
        return size_ * sizeof(element_type);
    }

    constexpr bool empty() const noexcept { return size_ == 0; }

    template<std::size_t Count>
    constexpr Span<element_type, Count> first() const noexcept
    {
        static_assert(Extent == dynamic_extent || Count <= Extent,
            "Span::first count exceeds the fixed extent");
        assert(Count <= size_);
        return Span<element_type, Count>(data_, Count);
    }

    constexpr Span<element_type, dynamic_extent> first(size_type count) const noexcept
    {
        assert(count <= size_);
        return Span<element_type, dynamic_extent>(data_, count);
    }

    template<std::size_t Count>
    constexpr Span<element_type, Count> last() const noexcept
    {
        static_assert(Extent == dynamic_extent || Count <= Extent,
            "Span::last count exceeds the fixed extent");
        assert(Count <= size_);
        return Span<element_type, Count>(offsetPointer(size_ - Count), Count);
    }

    constexpr Span<element_type, dynamic_extent> last(size_type count) const noexcept
    {
        assert(count <= size_);
        return Span<element_type, dynamic_extent>(offsetPointer(size_ - count), count);
    }

    template<std::size_t Offset, std::size_t Count = dynamic_extent>
    constexpr Span<element_type,
        span_detail::SubspanExtent<Extent, Offset, Count>::value>
    subspan() const noexcept
    {
        static_assert(Extent == dynamic_extent || Offset <= Extent,
            "Span::subspan offset exceeds the fixed extent");
        static_assert(Count == dynamic_extent || Extent == dynamic_extent
                || Count <= Extent - Offset,
            "Span::subspan count exceeds the fixed extent");

        assert(Offset <= size_);
        assert(Count == dynamic_extent || Count <= size_ - Offset);
        const size_type result_size = Count == dynamic_extent
            ? size_ - Offset
            : Count;
        return Span<element_type,
            span_detail::SubspanExtent<Extent, Offset, Count>::value>(
                offsetPointer(Offset), result_size);
    }

    constexpr Span<element_type, dynamic_extent> subspan(
        size_type offset, size_type count = dynamic_extent) const noexcept
    {
        assert(offset <= size_);
        assert(count == dynamic_extent || count <= size_ - offset);
        const size_type result_size = count == dynamic_extent
            ? size_ - offset
            : count;
        return Span<element_type, dynamic_extent>(
            offsetPointer(offset), result_size);
    }

private:
    constexpr pointer offsetPointer(size_type offset) const noexcept
    {
        return offset == 0 ? data_ : data_ + offset;
    }

private:
    pointer data_;
    size_type size_;
};

template<typename ElementType, std::size_t Extent>
constexpr typename Span<ElementType, Extent>::size_type
    Span<ElementType, Extent>::extent;

#if KIT_MUDUO_SPAN_CXX_STANDARD >= 201703L

template<typename ElementType, std::size_t Size>
Span(ElementType (&)[Size]) -> Span<ElementType, Size>;

template<typename ElementType, std::size_t Size>
Span(std::array<ElementType, Size>&) -> Span<ElementType, Size>;

template<typename ElementType, std::size_t Size>
Span(const std::array<ElementType, Size>&) -> Span<const ElementType, Size>;

template<typename ElementType>
Span(ElementType*, std::size_t) -> Span<ElementType>;

template<typename ElementType>
Span(ElementType*, ElementType*) -> Span<ElementType>;

template<typename Container>
Span(Container&) -> Span<typename std::remove_pointer<
    typename std::remove_cv<typename std::remove_reference<
        decltype(std::declval<Container&>().data())>::type>::type>::type>;

#endif

} // namespace kit_muduo

#endif

#undef KIT_MUDUO_SPAN_CXX_STANDARD

#endif // KIT_MUDUO_BASE_SPAN_H_
