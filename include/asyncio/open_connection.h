//
// Created by netcan on 2021/11/29.
//

#pragma once
#include <asyncio/asyncio_ns.h>
#include <asyncio/stream.h>
#include <asyncio/task.h>

#include <cstdint>
#include <string_view>

ASYNCIO_NS_BEGIN

Task<Stream> open_connection(std::string_view ip, uint16_t port);

ASYNCIO_NS_END
