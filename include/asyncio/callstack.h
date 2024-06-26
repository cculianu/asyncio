//
// Created by netcan on 2021/11/25.
//

#pragma once
#include <asyncio/asyncio_ns.h>
#include <asyncio/noncopyable.h>
#include <coroutine>
ASYNCIO_NS_BEGIN
namespace detail {
struct CallStackAwaiter {
    constexpr bool await_ready() noexcept { return false; }
    constexpr void await_resume() const noexcept {}
    template<typename Promise>
    bool await_suspend(std::coroutine_handle<Promise> caller) const noexcept {
        caller.promise().dump_backtrace();
        return false;
    }
};
} // namespace detail

[[nodiscard]] inline auto dump_callstack() -> detail::CallStackAwaiter { return {}; }

ASYNCIO_NS_END

