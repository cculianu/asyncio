//
// Created by calin.culianu@gmail.com 4/18/2024
//
#include <asyncio/stream.h>

#include <utility>

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

    const int NonBlockFlag = SOCK_NONBLOCK;
} // namespace socket


Stream::Stream(int fd)
    : read_fd_(fd), write_fd_(fd)
{
    if (read_fd_ >= 0) {
        socklen_t addrlen = sizeof(sock_info_);
        getsockname(read_fd_, reinterpret_cast<sockaddr*>(reinterpret_cast<std::byte*>(&sock_info_)), &addrlen);
        addrlen = sizeof(peer_sock_info_);
        getpeername(read_fd_, reinterpret_cast<sockaddr*>(reinterpret_cast<std::byte*>(&peer_sock_info_)), &addrlen);
    }
}

Stream::Stream(int fd, const sockaddr_storage& sockinfo): read_fd_(fd), write_fd_(fd), sock_info_(sockinfo) { }

Stream::Stream(Stream&& other)
    : read_fd_{std::exchange(other.read_fd_, -1) },
      write_fd_{std::exchange(other.write_fd_, -1) },
      is_shut_down{std::exchange(other.is_shut_down, false)},
      read_ev_{ std::exchange(other.read_ev_, {}) },
      write_ev_{ std::exchange(other.write_ev_, {}) },
      read_awaiter_{ std::move(other.read_awaiter_) },
      write_awaiter_{ std::move(other.write_awaiter_) },
      sock_info_{ other.sock_info_ },
      peer_sock_info_{ other.peer_sock_info_ }
{}

Stream::~Stream() { close(); }

void Stream::close()
{
    read_awaiter_.destroy();
    write_awaiter_.destroy();
    if (read_fd_ >= 0) { ::close(read_fd_); }
    if (write_fd_ >= 0 && write_fd_ != read_fd_) { ::close(write_fd_); }
    read_fd_ = write_fd_ = -1;
}

void Stream::shutdown()
{
    if (!is_shut_down) {
        is_shut_down = true;
        read_awaiter_.destroy();
        write_awaiter_.destroy();
        if (read_fd_ > -1) ::shutdown(read_fd_, SHUT_RDWR);
        if (read_fd_ != write_fd_ && write_fd_ > -1) ::shutdown(write_fd_, SHUT_RDWR);
    }
}

// Returns the address of either the locally bound socket if `peer == false`, or the remote peer if `peer == true`.
// Throws if `ss_family` is not `AF_INET` or `AF_INET6`, otherwise returns a valid variant.
std::variant<sockaddr_in, sockaddr_in6>
Stream::get_sockaddr(bool peer) const
{
    const sockaddr_storage &ss = get_sock_info(peer);
    std::variant<sockaddr_in, sockaddr_in6> ret;
    auto *bytes = reinterpret_cast<const std::byte *>(&ss); // Prevent C++ UB, signal compiler about aliasing of `ss`.
    if (ss.ss_family == AF_INET) {
        auto &sin = ret.emplace<sockaddr_in>();
        auto *bytes2 = reinterpret_cast<std::byte *>(&sin); // Prevent C++ UB, signal compiler about aliasing of `sin`.
        std::memcpy(bytes2, bytes, sizeof(sockaddr_in));
    } else if (ss.ss_family == AF_INET6) {
        auto &sin6 = ret.emplace<sockaddr_in6>();
        auto *bytes2 = reinterpret_cast<std::byte *>(&sin6); // Prevent C++ UB, signal compiler about aliasing of `sin6`.
        std::memcpy(bytes2, bytes, sizeof(sockaddr_in6));
    } else [[unlikely]] {
        throw std::runtime_error(fmt::format("{}: got unknown ss_family: {}", __func__, ss.ss_family));
    }
    return ret;
}

uint16_t Stream::get_port(bool peer) const
{
    const auto &ss = get_sock_info(peer);
    // Prevent C++ UB, signal compiler about aliasing of `sock_info_`.
    return get_in_port(reinterpret_cast<const sockaddr *>(reinterpret_cast<const std::byte *>(&ss)));
}


// Returns a type-erased pointer either of type `in_addr *` or `in6_addr *`. Throws if `sa->sa_family` is neither
// `AF_INET` nor `AF_INET6`. Don't use this function, since it is not type safe. Use `Stream::get_sockaddr` above instead
// which is more type-safe.
inline const void *get_in_addr(const sockaddr *sa) {
    auto *bytes = reinterpret_cast<const std::byte *>(sa); // Prevent C++ UB, signal compiler about aliasing of `sa`.
    if (sa->sa_family == AF_INET) {
        return &reinterpret_cast<const sockaddr_in*>(bytes)->sin_addr;
    }
    if (sa->sa_family != AF_INET6) [[unlikely]] throw std::runtime_error(fmt::format("{}: got unknown sa_family: {}", __func__, sa->sa_family));
    return &reinterpret_cast<const sockaddr_in6*>(bytes)->sin6_addr;
}

// Returns the port in host byte order, or throws if sa->sa_family is not AF_INET or AF_INET6.
inline uint16_t get_in_port(const sockaddr *sa) {
    uint16_t port;
    auto *bytes = reinterpret_cast<const std::byte *>(sa); // Prevent C++ UB, signal compiler about aliasing of `sa`.
    static_assert(std::is_same_v<uint16_t, in_port_t>);
    if (sa->sa_family == AF_INET) {
        std::memcpy(&port, &reinterpret_cast<const sockaddr_in*>(bytes)->sin_port, sizeof(port));
    } else { // AF_INET6
        if (sa->sa_family != AF_INET6) [[unlikely]] throw std::runtime_error(fmt::format("{}: got unknown sa_family: {}", __func__, sa->sa_family));
        std::memcpy(&port, &reinterpret_cast<const sockaddr_in6*>(bytes)->sin6_port, sizeof(port));
    }
    return ntohs(port);
}

ASYNCIO_NS_END
