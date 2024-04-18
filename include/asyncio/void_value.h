//
// Created by netcan on 2021/11/20.
//

#pragma once
#include <asyncio/asyncio_ns.h>

#include <type_traits>

ASYNCIO_NS_BEGIN

struct VoidValue { };

namespace detail {
template<typename T> struct GetTypeIfVoid: std::type_identity<T> {};
template<> struct GetTypeIfVoid<void>: std::type_identity<VoidValue> {};
} // namespace detail

template<typename T>
using GetTypeIfVoid_t = typename detail::GetTypeIfVoid<T>::type;

ASYNCIO_NS_END
