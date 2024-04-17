//
// Created by netcan on 2021/11/30.
//

#ifndef ASYNCIO_STREAM_H
#define ASYNCIO_STREAM_H
#include <asyncio/asyncio_ns.h>
#include <asyncio/concept/bytebuf.h>
#include <asyncio/event_loop.h>
#include <asyncio/noncopyable.h>
#include <asyncio/selector/event.h>
#include <asyncio/task.h>
#include <asyncio/util.h>

#include <span>
#include <utility>
#include <vector>

#include <fcntl.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <unistd.h>

#ifndef SOCK_NONBLOCK /* If Protocol not supported */
    #define SOCK_NONBLOCK 0
#endif

ASYNCIO_NS_BEGIN
namespace socket {
    // Redesign python method `socket.setblocking(bool)`:
    // https://github.com/python/cpython/blob/928752ce4c23f47d3175dd47ecacf08d86a99c9d/Modules/socketmodule.c#L683
    // https://stackoverflow.com/a/1549344/14070318
    bool set_blocking(int fd, bool blocking) {
        if (fd < 0)
            return false;
        if constexpr (SOCK_NONBLOCK != 0) {
            return true;
        } else {
        #if defined(_WIN32)
            unsigned long block = !blocking;
            return !ioctlsocket(fd, FIONBIO, &block);
        #elif __has_include(<sys/ioctl.h>) && defined(FIONBIO)
            unsigned int block = !blocking;
            return !ioctl(fd, FIONBIO, &block);
        #else
            int delay_flag, new_delay_flag;
            delay_flag = fcntl(fd, F_GETFL, 0);
            if (delay_flag == -1)
                return false;
            new_delay_flag = blocking ? (delay_flag & ~O_NONBLOCK) : (delay_flag | O_NONBLOCK);
            if (new_delay_flag != delay_flag)
                return !fcntl(fd, F_SETFL, new_delay_flag);
            else
                return false;
        #endif
        }
    }
}

struct Stream: NonCopyable {
    using Buffer = std::vector<char>; // Default buffer for read() if nothing specified
    Stream(int fd): read_fd_(fd), write_fd_(fd) {
        if (read_fd_ >= 0) {
            socklen_t addrlen = sizeof(sock_info_);
            getsockname(read_fd_, reinterpret_cast<sockaddr*>(&sock_info_), &addrlen);
        }
    }
    Stream(int fd, const sockaddr_storage& sockinfo): read_fd_(fd), write_fd_(fd), sock_info_(sockinfo) { }
    Stream(Stream&& other): read_fd_{std::exchange(other.read_fd_, -1) },
                            write_fd_{std::exchange(other.write_fd_, -1) },
                            read_ev_{ std::exchange(other.read_ev_, {}) },
                            write_ev_{ std::exchange(other.write_ev_, {}) },
                            read_awaiter_{ std::move(other.read_awaiter_) },
                            write_awaiter_{ std::move(other.write_awaiter_) },
                            sock_info_{ other.sock_info_ } { }
    ~Stream() { close(); }

    void close() {
        read_awaiter_.destroy();
        write_awaiter_.destroy();
        if (read_fd_ >= 0) { ::close(read_fd_); }
        if (write_fd_ >= 0 && write_fd_ != read_fd_) { ::close(write_fd_); }
        read_fd_ = -1;
        write_fd_ = -1;
    }

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
    const sockaddr_storage& get_sock_info() const {
        return sock_info_;
    }

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
    Event read_ev_ { .fd = read_fd_, .flags = Event::Flags::EVENT_READ };
    Event write_ev_ { .fd = write_fd_, .flags = Event::Flags::EVENT_WRITE };
    EventLoop::WaitEventAwaiter read_awaiter_ { get_event_loop().wait_event(read_ev_) };
    EventLoop::WaitEventAwaiter write_awaiter_ { get_event_loop().wait_event(write_ev_) };
    sockaddr_storage sock_info_{};
    constexpr static size_t chunk_size = 4096;
};


inline const void *get_in_addr(const sockaddr *sa) {
    if (sa->sa_family == AF_INET) {
        return &reinterpret_cast<const sockaddr_in*>(sa)->sin_addr;
    }
    return &reinterpret_cast<const sockaddr_in6*>(sa)->sin6_addr;
}

inline uint16_t get_in_port(const sockaddr *sa) {
    if (sa->sa_family == AF_INET) {
        return ntohs(reinterpret_cast<const sockaddr_in*>(sa)->sin_port);
    }

    return ntohs(reinterpret_cast<const sockaddr_in6*>(sa)->sin6_port);
}

ASYNCIO_NS_END
#endif // ASYNCIO_STREAM_H
