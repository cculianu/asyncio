//
// Created by netcan on 2021/10/24.
//

#pragma once

#if defined(__APPLE__)
#include <asyncio/selector/kqueue_selector.h>
namespace ASYNCIO_NS {
using Selector = KQueueSelector;
}
#elif defined(__linux)
#include <asyncio/selector/epoll_selector.h>
namespace ASYNCIO_NS {
using Selector = EpollSelector;
}
#endif
