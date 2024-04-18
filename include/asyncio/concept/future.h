//
// Created by netcan on 2021/09/08.
//

#pragma once
#include <asyncio/asyncio_ns.h>
#include <asyncio/concept/awaitable.h>
#include <asyncio/handle.h>
#include <concepts>
#include <type_traits>

ASYNCIO_NS_BEGIN
namespace concepts {
template<typename Fut>
concept Future = Awaitable<Fut> && requires(Fut fut) {
    requires !std::default_initializable<Fut>;
    requires std::move_constructible<Fut>;
    typename std::remove_cvref_t<Fut>::promise_type;
    fut.get_result();
};
} // namespace concepts
ASYNCIO_NS_END
