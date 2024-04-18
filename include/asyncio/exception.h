//
// Created by netcan on 2021/11/21.
//

#pragma once
#include <asyncio/asyncio_ns.h>
#include <exception>
ASYNCIO_NS_BEGIN
struct TimeoutError : std::exception {
    [[nodiscard]] const char* what() const noexcept override { return "TimeoutError"; }
};

struct NoResultError : std::exception {
    [[nodiscard]] const char* what() const noexcept override { return "result is unset"; }
};

struct InvalidFuture : std::exception {
    [[nodiscard]] const char* what() const noexcept override { return "future is invalid"; }
};

ASYNCIO_NS_END
