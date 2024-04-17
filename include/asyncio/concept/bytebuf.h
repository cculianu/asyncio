//
// Created by Calin Culianu on 2024/04/17.
//
#pragma once
#include <asyncio/asyncio_ns.h>

#include <cstddef>
#include <concepts>

ASYNCIO_NS_BEGIN
namespace concepts {
template<typename B>
concept ByteBuf = requires (B b) {
    sizeof(typename B::value_type) == 1u;
    { b.data() } -> std::convertible_to<typename B::value_type const *>;
    { b.size() } -> std::convertible_to<size_t>;
};
template<typename B>
concept MutableByteBuf = ByteBuf<B> && requires (B b) {
    { b.data() } -> std::convertible_to<typename B::value_type *>;
};
}
ASYNCIO_NS_END
