//
// Created by netcan on 2021/11/30.
// Refactored by calin.culianu@gmail.com 4/18/2024
//

#pragma once
#include <asyncio/asyncio_ns.h>
#include <asyncio/concept/bytebuf.h>
#include <asyncio/event_loop.h>
#include <asyncio/noncopyable.h>
#include <asyncio/selector/event.h>
#include <asyncio/task.h>
#include <asyncio/util.h>

#include <fmt/format.h>

#include <cstddef> // std::byte
#include <stdexcept>
#include <span>
#include <variant>
#include <vector>

#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>

ASYNCIO_NS_BEGIN

namespace socket {
    // Redesign python method `socket.setblocking(bool)`:
    // https://github.com/python/cpython/blob/928752ce4c23f47d3175dd47ecacf08d86a99c9d/Modules/socketmodule.c#L683
    // https://stackoverflow.com/a/1549344/14070318
    bool set_blocking(int fd, bool blocking);

    extern const int NonBlockFlag; // aka SOCK_NONBLOCK
} // namespace socket


// fwd decls
const void *get_in_addr(const sockaddr *sa);
uint16_t get_in_port(const sockaddr *sa);


struct Stream : NonCopyable {
    using Buffer = std::vector<char>; // Default buffer for read() if nothing specified
    Stream(int fd);
    Stream(int fd, const sockaddr_storage& sockinfo);
    Stream(Stream&& other);
    ~Stream();

    void close();

    void shutdown();

    template <concepts::MutableByteBuf BUF = Buffer>
    Task<BUF> read(ssize_t sz = -1, bool fill_buffer = false) {
        if (sz < 0) { co_return co_await read_until_eof<BUF>(); }

        BUF result(size_t(sz), typename BUF::value_type{});
        {
            auto span_result = co_await read_in_place(Spanify(result), fill_buffer);
            result.resize(span_result.size());  // trim to what we actually read; NOTE: span_result possibly invalidated
        }
        co_return result;
    }

    // Reads data in-place into buffer, returns a (sub)span of buffer representing the valid data actually read.
    // If `fill_buffer` is true, will keep reading until buffer.size() bytes have been read, otherwise will return
    // to caller after a single read() of the underlying fd. Even with `fill_buffer`, may return a short buffer if EOF.
    // If an empty buffer is returned, this also indicates EOF.
    template <typename T>
    requires (std::has_unique_object_representations_v<T> && !std::is_const_v<T> && sizeof(T) == 1)
    Task<std::span<T>> read_in_place(std::span<T> buffer, bool fill_buffer = false) {
        std::span bytebuf = std::as_writable_bytes(buffer);
        size_t nread = 0;
        while ( ! bytebuf.empty()) {
            co_await read_awaiter_;
            ssize_t const sz = ::read(read_fd_, bytebuf.data(), bytebuf.size());
            if (sz < 0) [[unlikely]] {
                throw std::system_error(std::make_error_code(static_cast<std::errc>(errno)));
            } else if (size_t(sz) > bytebuf.size()) [[unlikely]] {
                throw std::runtime_error(fmt::format("Unexpected size returned from read(): {} > {}", sz, bytebuf.size()));
            }
            nread += sz;
            if (!fill_buffer || sz == 0) break;
            bytebuf = bytebuf.last(bytebuf.size() - size_t(sz));
        }
        co_return buffer.first(nread / sizeof(T)); // return a view into the bytes we actually read
    }

    template<concepts::ByteBuf BUF>
    Task<> write(const BUF& buf) {
        std::span bytes2write = std::as_bytes(Spanify(buf));
        while (! bytes2write.empty()) {
            co_await write_awaiter_;
            ssize_t sz = ::write(write_fd_, bytes2write.data(), bytes2write.size());
            if (sz < 0) [[unlikely]] {
                throw std::system_error(std::make_error_code(static_cast<std::errc>(errno)));
            } else if (sz == 0) [[unlikely]] {
                throw std::system_error(std::error_code{}, "write() returned 0 bytes; EOF");
            } else if (size_t(sz) > bytes2write.size()) [[unlikely]] {
                throw std::runtime_error(fmt::format("Unexpected size returned from write(): {} > {}", sz, bytes2write.size()));
            }
            // advance read pos
            bytes2write = bytes2write.last(bytes2write.size() - size_t(sz));
        }
        co_return;
    }

    // Local address if `peer==false`, remote address if `peer==true`
    const sockaddr_storage &
    get_sock_info(bool peer = false) const { return peer ? peer_sock_info_ : sock_info_; }

    // Returns the address of either the locally bound socket if `peer == false`, or the remote peer if `peer == true`.
    // Throws if `ss_family` is not `AF_INET` or `AF_INET6`, otherwise returns a valid variant.
    std::variant<sockaddr_in, sockaddr_in6>
    get_sockaddr(bool peer = false) const;

    uint16_t get_port(bool peer = false) const;

private:
    template <concepts::MutableByteBuf BUF = Buffer>
    Task<BUF> read_until_eof() {
        BUF result;
        ssize_t current_read = 0;
        size_t total_read = 0;
        do {
            result.resize(total_read + chunk_size);
            co_await read_awaiter_;
            current_read = ::read(read_fd_, result.data() + total_read, chunk_size);
            if (current_read < 0) {
                throw std::system_error(std::make_error_code(static_cast<std::errc>(errno)));
            }
            if (size_t(current_read) < chunk_size) { result.resize(total_read + current_read); }
            total_read += current_read;
        } while (current_read > 0);
        co_return result;
    }

    int read_fd_{-1};
    int write_fd_{-1};
    bool is_shut_down = false;
    Event read_ev_ { .fd = read_fd_, .flags = Event::Flags::EVENT_READ };
    Event write_ev_ { .fd = write_fd_, .flags = Event::Flags::EVENT_WRITE };
    EventLoop::WaitEventAwaiter read_awaiter_ { get_event_loop().wait_event(read_ev_) };
    EventLoop::WaitEventAwaiter write_awaiter_ { get_event_loop().wait_event(write_ev_) };
    sockaddr_storage sock_info_{}, peer_sock_info_{};
    static constexpr size_t chunk_size = 4096;
};

// Returns a type-erased pointer either of type `in_addr *` or `in6_addr *`. Throws if `sa->sa_family` is neither
// `AF_INET` nor `AF_INET6`. Don't use this function, since it is not type safe. Use `Stream::get_sockaddr` above instead
// which is more type-safe.
const void *get_in_addr(const sockaddr *sa);

// Returns the port in host byte order, or throws if sa->sa_family is not AF_INET or AF_INET6.
uint16_t get_in_port(const sockaddr *sa);

ASYNCIO_NS_END
