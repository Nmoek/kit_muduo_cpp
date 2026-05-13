/**
 * @file runtime_result.h
 * @brief  业务运行态
 * @author Kewin Li
 * @version 1.0
 * @date 2026-05-09 11:19:13
 * @copyright Copyright (c) 2026 Kewin Li
 */
#ifndef __KIT_RUNTIME_RESULT_H__
#define __KIT_RUNTIME_RESULT_H__

#include "base/log.h"
#include "net/event_loop.h"

#include <atomic>
#include <chrono>
#include <exception>
#include <future>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <unordered_map>

namespace kit_domain {


class RuntimeError
{
public:
    enum 
    {
        kOk = 0,

        /* invoke  */
        kInvokeInvalidArgument = 1000,
        kInvokeTimeout,
        kInvokeDeferred,
        kInvokeException,

        /* domain runtime */
        kInvalidArgument = 2000,
        kNullProtocolItem,
        kProtocolItemNotFound,
        kProtocolTypeMismatch,
        kInvalidProtocolConfig,
        kRouteNotFound,
        kRouteConflict,
        kFuncCodeNotFound,
        kFuncCodeConflict,

        kInternalError = 9000,
    };

    RuntimeError() = default;
    explicit RuntimeError(int32_t error_code) :error_code_(error_code) { }

    void set(int32_t error_code) { error_code_ = error_code; }

    bool ok() const { return error_code_ == kOk; }

    int32_t toInt() const { return error_code_; }

    std::string toMsg() const { return s_error_msg.at(error_code_); }

private:
    static const std::unordered_map<int32_t, std::string> s_error_msg;

    int32_t error_code_{kOk};
};




template<class T = void>
struct RuntimeResult
{
    RuntimeError error;
    T val{T()};

    bool ok() const { return error.ok(); }
};

template<>
struct RuntimeResult<void>
{
    RuntimeError error;
    bool ok() const { return error.ok(); }
};



/**
 * @brief Loop执行辅助函数
 * @tparam Func 
 * @tparam Args 
 * @param loop 
 * @param timeout_ms 
 * @param func 
 * @param args 
 * @return RuntimeResult 
 */
template<typename FuncType, typename... Args>
auto InvokeOnLoopSync(kit_muduo::EventLoop *loop, 
    int64_t timeout_ms, 
    FuncType &&func, 
    Args &&...args) 
-> std::invoke_result_t<FuncType, Args...>
{
    using ReturnType = std::invoke_result_t<FuncType, Args...>;
    ReturnType runtime_result;
    runtime_result.error.set(RuntimeError::kOk);

    if(!loop ||  0 == timeout_ms)
    {
        runtime_result.error.set(RuntimeError::kInvokeInvalidArgument);
        return runtime_result;
    }

    if constexpr (!std::is_invocable_v<FuncType, Args...>) 
    {
        runtime_result.error.set(RuntimeError::kInvokeInvalidArgument);
        return runtime_result;
    }

    auto task = std::make_shared<std::packaged_task<ReturnType()>>(
        std::bind(std::forward<FuncType>(func),
        std::forward<Args>(args)...)
    );

    try
    {
        if(loop->isInLoopThread())
        {
            // 直接在事件循环线程中执行
            runtime_result = std::invoke(std::forward<FuncType>(func), std::forward<Args>(args)...);

        }
        else
        {
            std::shared_ptr<std::atomic_bool> is_timeout = std::make_shared<std::atomic_bool>(false);
            auto f = task->get_future();
            loop->queueInLoop([task, is_timeout]() {
                if(is_timeout->load())
                {
                    KIT_FMT_WARN(KIT_LOGGER("domain"), "runtime", "invoke run timeout \n");
                    return;
                }
                (*task)();
            });

            if(timeout_ms > 0)
            {
                auto status = f.wait_for(std::chrono::milliseconds(timeout_ms));
    

                if (std::future_status::ready == status) 
                {
                    runtime_result = f.get();
                } 
                else if (std::future_status::timeout == status) 
                {
                    is_timeout->store(true);
                    runtime_result.error.set(RuntimeError::kInvokeTimeout);
                } 
                else if (std::future_status::deferred == status) 
                {
                    is_timeout->store(true);
                    runtime_result.error.set(RuntimeError::kInvokeDeferred);
                }
            }
            else
            {
                // 无限期阻塞等待
                runtime_result = f.get();
            }
        }
    }
    catch(const std::exception &e)
    {
        KIT_FMT_ERROR(KIT_LOGGER("domain"), "runtime", "invoke run exception:%s \n", e.what());

        runtime_result.error.set(RuntimeError::kInvokeException);
    }
    catch(...)
    {
        runtime_result.error.set(RuntimeError::kInvokeException);
    }
    
    return runtime_result;
}



}



#endif //__KIT_RUNTIME_RESULT_H__