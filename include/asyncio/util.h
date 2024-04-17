// Created by Calin Culianu on 4/17/2024
#pragma once

#include <asyncio/asyncio_ns.h>

#include <span>

ASYNCIO_NS_BEGIN

template<typename T>
requires requires (T t) {
    { t.data() };
    { t.size() };
}
auto Spanify(T && t) { return std::span(t.data(), t.size()); }

ASYNCIO_NS_END
