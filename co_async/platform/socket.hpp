#pragma once

#include <co_async/std.hpp>
#include <co_async/platform/error_handling.hpp>
#include <co_async/generic/cancel.hpp>
#include <co_async/platform/fs.hpp>
#include <co_async/platform/platform_io.hpp>
#include <co_async/utils/string_utils.hpp>
#include <co_async/awaiter/task.hpp>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <netdb.h>
#include <sys/un.h>

namespace co_async {

struct IpAddress {
    explicit IpAddress(struct in_addr const &addr) noexcept : mAddr(addr) {}

    explicit IpAddress(struct in6_addr const &addr6) noexcept : mAddr(addr6) {}

    IpAddress(char const *ip) {
        struct in_addr addr = {};
        struct in6_addr addr6 = {};
        if (throwingErrorErrno(inet_pton(AF_INET, ip, &addr))) {
            mAddr = addr;
            return;
        }
        if (throwingErrorErrno(inet_pton(AF_INET6, ip, &addr6))) {
            mAddr = addr6;
            return;
        }
        struct hostent *hent = gethostbyname(ip);
        for (int i = 0; hent->h_addr_list[i]; i++) {
            if (hent->h_addrtype == AF_INET) {
                std::memcpy(&addr, hent->h_addr_list[i], sizeof(in_addr));
                mAddr = addr;
                return;
            } else if (hent->h_addrtype == AF_INET6) {
                std::memcpy(&addr6, hent->h_addr_list[i], sizeof(in6_addr));
                mAddr = addr6;
                return;
            }
        }
        throw std::invalid_argument("invalid domain name or ip address");
    }

    std::string toString() const {
        if (mAddr.index() == 1) {
            char buf[INET6_ADDRSTRLEN + 1] = {};
            inet_ntop(AF_INET6, &std::get<1>(mAddr), buf, sizeof(buf));
            return buf;
        } else {
            char buf[INET_ADDRSTRLEN + 1] = {};
            inet_ntop(AF_INET, &std::get<0>(mAddr), buf, sizeof(buf));
            return buf;
        }
    }

    auto repr() const {
        return toString();
    }

    std::variant<struct in_addr, struct in6_addr> mAddr;
};

struct SocketAddress {
    SocketAddress() = default;

    static SocketAddress parseCommaSeperated(std::string_view host,
                                             int defaultPort) {
        auto pos = host.find(':');
        std::string hostPart(host);
        std::optional<int> port;
        if (pos != std::string_view::npos) {
            hostPart = host.substr(0, pos);
            port = from_string<int>(host.substr(pos + 1));
            if (port < 0 || port > 65535) [[unlikely]] {
                port = std::nullopt;
            }
        }
        return SocketAddress(IpAddress(hostPart.c_str()),
                             port.value_or(defaultPort));
    }

    SocketAddress(IpAddress ip, int port) {
        std::visit([&](auto const &addr) { initFromHostPort(addr, port); },
                   ip.mAddr);
    }

    union {
        struct sockaddr_in mAddrIpv4;
        struct sockaddr_in6 mAddrIpv6;
        struct sockaddr mAddr;
    };

    socklen_t mAddrLen;

    sa_family_t family() const noexcept {
        return mAddr.sa_family;
    }

    IpAddress host() const {
        if (family() == AF_INET) {
            return IpAddress(mAddrIpv4.sin_addr);
        } else if (family() == AF_INET6) {
            return IpAddress(mAddrIpv6.sin6_addr);
        } else [[unlikely]] {
            throw std::runtime_error("address family not ipv4 or ipv6");
        }
    }

    int port() const {
        if (family() == AF_INET) {
            return ntohs(mAddrIpv4.sin_port);
        } else if (family() == AF_INET6) {
            return ntohs(mAddrIpv6.sin6_port);
        } else [[unlikely]] {
            throw std::runtime_error("address family not ipv4 or ipv6");
        }
    }

    auto toString() const {
        return host().toString() + ":" + to_string(port());
    }

    auto repr() const {
        return toString();
    }

private:
    void initFromHostPort(struct in_addr const &host, int port) {
        struct sockaddr_in saddr = {};
        saddr.sin_family = AF_INET;
        std::memcpy(&saddr.sin_addr, &host, sizeof(saddr.sin_addr));
        saddr.sin_port = htons(port);
        std::memcpy(&mAddrIpv4, &saddr, sizeof(saddr));
        mAddrLen = sizeof(saddr);
    }

    void initFromHostPort(struct in6_addr const &host, int port) {
        struct sockaddr_in6 saddr = {};
        saddr.sin6_family = AF_INET6;
        std::memcpy(&saddr.sin6_addr, &host, sizeof(saddr.sin6_addr));
        saddr.sin6_port = htons(port);
        std::memcpy(&mAddrIpv6, &saddr, sizeof(saddr));
        mAddrLen = sizeof(saddr);
    }
};

struct [[nodiscard]] SocketHandle : FileHandle {
    using FileHandle::FileHandle;
};

struct [[nodiscard]] SocketListener : SocketHandle {
    using SocketHandle::SocketHandle;
};

inline SocketAddress get_socket_address(SocketHandle &sock) {
    SocketAddress sa;
    sa.mAddrLen = sizeof(sa.mAddrIpv6);
    throwingErrorErrno(
        getsockname(sock.fileNo(), (sockaddr *)&sa.mAddr, &sa.mAddrLen));
    return sa;
}

inline SocketAddress get_socket_peer_address(SocketHandle &sock) {
    SocketAddress sa;
    sa.mAddrLen = sizeof(sa.mAddrIpv6);
    throwingErrorErrno(
        getpeername(sock.fileNo(), (sockaddr *)&sa.mAddr, &sa.mAddrLen));
    return sa;
}

template <class T>
inline Expected<T> socketGetOption(SocketHandle &sock, int level, int optId) {
    T val;
    socklen_t len = sizeof(val);
    if (auto e =
            expectError(getsockopt(sock.fileNo(), level, optId, &val, &len))) {
        return Unexpected{e.error()};
    }
    return val;
}

template <class T>
inline Expected<> socketSetOption(SocketHandle &sock, int level, int opt,
                                  T const &optVal) {
    return expectError(
        setsockopt(sock.fileNo(), level, opt, &optVal, sizeof(optVal)));
}

inline Task<Expected<SocketHandle>> createSocket(int family, int type) {
    int fd = co_await expectError(
        co_await UringOp().prep_socket(family, type, 0, 0));
    SocketHandle sock(fd);
    co_return sock;
}

inline Task<Expected<SocketHandle>> socket_connect(SocketAddress const &addr) {
    SocketHandle sock =
        co_await co_await createSocket(addr.family(), SOCK_STREAM);
    co_await expectError(co_await UringOp().prep_connect(
        sock.fileNo(), (const struct sockaddr *)&addr.mAddr, addr.mAddrLen));
    co_return sock;
}

inline Task<Expected<SocketHandle>>
socket_connect(SocketAddress const &addr,
               std::chrono::steady_clock::duration timeout) {
    SocketHandle sock =
        co_await co_await createSocket(addr.family(), SOCK_STREAM);
    auto ts = durationToKernelTimespec(timeout);
    co_await expectError(co_await UringOp::link_ops(
        UringOp().prep_connect(
            sock.fileNo(), (const struct sockaddr *)&addr.mAddr, addr.mAddrLen),
        UringOp().prep_link_timeout(&ts, IORING_TIMEOUT_BOOTTIME)));
    co_return sock;
}

inline Task<Expected<SocketHandle>> socket_connect(SocketAddress const &addr,
                                                   CancelToken cancel) {
    SocketHandle sock =
        co_await co_await createSocket(addr.family(), SOCK_STREAM);
    if (cancel.is_canceled()) [[unlikely]] {
        co_return Unexpected{
            std::make_error_code(std::errc::operation_canceled)};
    }
    co_await expectError(co_await cancel.invoke<UringOpCanceller>(
        UringOp().prep_connect(sock.fileNo(),
                               (const struct sockaddr *)&addr.mAddr,
                               addr.mAddrLen)));
    co_return sock;
}

inline Task<Expected<SocketListener>> listener_bind(SocketAddress const &addr,
                                                    int backlog = SOMAXCONN) {
    SocketHandle sock =
        co_await co_await createSocket(addr.family(), SOCK_STREAM);
    co_await socketSetOption(sock, SOL_SOCKET, SO_REUSEADDR, 1);
    /* co_await socketSetOption(sock, IPPROTO_TCP, TCP_CORK, 0); */
    /* co_await socketSetOption(sock, IPPROTO_TCP, TCP_NODELAY, 1); */
    /* co_await socketSetOption(sock, SOL_SOCKET, SO_KEEPALIVE, 1); */
    SocketListener serv(sock.releaseFile());
    co_await expectError(bind(
        serv.fileNo(), (struct sockaddr const *)&addr.mAddr, addr.mAddrLen));
    co_await expectError(listen(serv.fileNo(), backlog));
    co_return serv;
}

inline Task<Expected<SocketHandle>> listener_accept(SocketListener &listener) {
    int fd = co_await expectError(
        co_await UringOp().prep_accept(listener.fileNo(), nullptr, nullptr, 0));
    SocketHandle sock(fd);
    co_return sock;
}

inline Task<Expected<SocketHandle>> listener_accept(SocketListener &listener,
                                                    CancelToken cancel) {
    int fd = co_await expectError(co_await cancel.invoke<UringOpCanceller>(
        UringOp().prep_accept(listener.fileNo(), nullptr, nullptr, 0)));
    SocketHandle sock(fd);
    co_return sock;
}

inline Task<Expected<SocketHandle>> listener_accept(SocketListener &listener,
                                                    SocketAddress &peerAddr) {
    int fd = co_await expectError(co_await UringOp().prep_accept(
        listener.fileNo(), (struct sockaddr *)&peerAddr.mAddr,
        &peerAddr.mAddrLen, 0));
    SocketHandle sock(fd);
    co_return sock;
}

inline Task<Expected<SocketHandle>> listener_accept(SocketListener &listener,
                                                    SocketAddress &peerAddr,
                                                    CancelToken cancel) {
    int fd = co_await expectError(co_await cancel.invoke<UringOpCanceller>(
        UringOp().prep_accept(listener.fileNo(),
                              (struct sockaddr *)&peerAddr.mAddr,
                              &peerAddr.mAddrLen, 0)));
    SocketHandle sock(fd);
    co_return sock;
}

inline Task<Expected<std::size_t>> socket_write(SocketHandle &sock,
                                                std::span<char const> buf) {
    co_return expectError(co_await UringOp().prep_send(sock.fileNo(), buf, 0));
}

inline Task<Expected<std::size_t>> socket_read(SocketHandle &sock,
                                               std::span<char> buf) {
    co_return expectError(co_await UringOp().prep_recv(sock.fileNo(), buf, 0));
}

inline Task<Expected<std::size_t>> socket_write(SocketHandle &sock,
                                                std::span<char const> buf,
                                                CancelToken cancel) {
    co_return expectError(co_await cancel.invoke<UringOpCanceller>(
        UringOp().prep_send(sock.fileNo(), buf, 0)));
}

inline Task<Expected<std::size_t>>
socket_read(SocketHandle &sock, std::span<char> buf, CancelToken cancel) {
    co_return expectError(co_await cancel.invoke<UringOpCanceller>(
        UringOp().prep_recv(sock.fileNo(), buf, 0)));
}

inline Task<Expected<std::size_t>>
socket_write(SocketHandle &sock, std::span<char const> buf,
             std::chrono::steady_clock::duration timeout) {
    auto ts = durationToKernelTimespec(timeout);
    co_return co_await expectError(co_await UringOp::link_ops(
        UringOp().prep_send(sock.fileNo(), buf, 0),
        UringOp().prep_link_timeout(&ts, IORING_TIMEOUT_BOOTTIME)));
}

inline Task<Expected<std::size_t>>
socket_read(SocketHandle &sock, std::span<char> buf,
            std::chrono::steady_clock::duration timeout) {
    auto ts = durationToKernelTimespec(timeout);
    co_return expectError(co_await UringOp::link_ops(
        UringOp().prep_recv(sock.fileNo(), buf, 0),
        UringOp().prep_link_timeout(&ts, IORING_TIMEOUT_BOOTTIME)));
}

inline Task<Expected<>> socket_shutdown(SocketHandle &sock,
                                        int how = SHUT_RDWR) {
    co_return expectError(co_await UringOp().prep_shutdown(sock.fileNo(), how));
}

} // namespace co_async
