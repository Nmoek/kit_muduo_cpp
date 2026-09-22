/**
 * @file defer.h
 * @brief 作用域退出时执行回调的 RAII 工具。
 */
#ifndef __KIT_DEFER_H__
#define __KIT_DEFER_H__

#include <type_traits>
#include <utility>

namespace kit_muduo {

/**
 * @brief 在析构时执行一次无参回调。
 *
 * 示例：Defer cleanup([fd]() noexcept { ::close(fd); });
 * 必须声明为具名局部变量；临时对象会在当前语句结束时执行回调。
 * 正常退出代码块、提前 return 和异常栈展开均触发回调；多个对象逆序执行。
 * 与 Go defer 不同，执行时机是所在代码块退出，而非固定在函数返回时。
 * 值捕获保存注册时的值；引用捕获读取执行时的值，被引用对象必须仍然存活。
 * 回调不得向外抛出异常，否则 noexcept 析构函数会触发 std::terminate。
 */
template <typename Callback>
class [[nodiscard]] Defer
{
    static_assert(std::is_invocable_v<Callback &>,
                  "Defer requires a callback callable with no arguments");

public:
    explicit Defer(Callback callback)
        noexcept(std::is_nothrow_move_constructible_v<Callback>)
        : callback_(std::move(callback))
    {
    }

    ~Defer() noexcept
    {
        callback_();
    }

    Defer(const Defer &) = delete;
    Defer &operator=(const Defer &) = delete;
    Defer(Defer &&) = delete;
    Defer &operator=(Defer &&) = delete;

private:
    Callback callback_;
};

} // namespace kit_muduo

#endif // KIT_BASE_DEFER_H_
