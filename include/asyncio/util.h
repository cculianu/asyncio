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

// helper type for std::visit
template<class... Ts> struct Overloaded : Ts... { using Ts::operator()...; };
template<class... Ts> Overloaded(Ts...) -> Overloaded<Ts...>;

ASYNCIO_NS_END
