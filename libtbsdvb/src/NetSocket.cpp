#include "NetSocket.h"

#include <cstring>
#include <mutex>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <cerrno>
#include <fcntl.h>
#include <net/if.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace tbs {
namespace net {

namespace {

#ifdef _WIN32
using SockLen = int;
int lastSocketError() { return WSAGetLastError(); }
void closeSocket(intptr_t fd) { ::closesocket(static_cast<SOCKET>(fd)); }
#else
using SockLen = socklen_t;
int lastSocketError() { return errno; }
void closeSocket(intptr_t fd) { ::close(static_cast<int>(fd)); }
#endif

template <typename T>
bool setOpt(intptr_t fd, int level, int name, const T &v)
{
#ifdef _WIN32
    return ::setsockopt(static_cast<SOCKET>(fd), level, name, reinterpret_cast<const char *>(&v), sizeof(v)) == 0;
#else
    return ::setsockopt(static_cast<int>(fd), level, name, &v, sizeof(v)) == 0;
#endif
}

unsigned interfaceIndex(const std::string &name)
{
#ifdef _WIN32
    (void)name;
    return 0;
#else
    return ::if_nametoindex(name.c_str());
#endif
}

} // namespace

std::string errorText(int code)
{
#ifdef _WIN32
    char buf[256] = {0};
    FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, static_cast<DWORD>(code), 0, buf,
                   sizeof(buf), nullptr);
    std::string s(buf);
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == '.'))
        s.pop_back();
    return s.empty() ? "error " + std::to_string(code) : s;
#else
    return std::strerror(code);
#endif
}

bool startup(std::string *error)
{
#ifdef _WIN32
    static std::once_flag once;
    static int result = 0;
    std::call_once(once, [] {
        WSADATA wsa;
        result = WSAStartup(MAKEWORD(2, 2), &wsa);
    });
    if (result != 0) {
        if (error)
            *error = "WSAStartup failed: " + errorText(result);
        return false;
    }
#else
    (void)error;
#endif
    return true;
}

UdpSender::~UdpSender()
{
    close();
}

bool UdpSender::isOpen() const
{
    return m_fd >= 0;
}

void UdpSender::close()
{
    if (m_fd >= 0)
        closeSocket(m_fd);
    m_fd = -1;
}

bool UdpSender::open(const std::string &host, uint16_t port, const std::string &iface, int ttl, std::string *error)
{
    close();
    auto fail = [&](const std::string &what) {
        if (error)
            *error = what;
        close();
        return false;
    };
    if (!startup(error))
        return false;

    addrinfo hints;
    std::memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;
    addrinfo *res = nullptr;
    const std::string service = std::to_string(port);
    const int rc = ::getaddrinfo(host.c_str(), service.c_str(), &hints, &res);
    if (rc != 0 || !res)
        return fail("cannot resolve \"" + host + "\": " + std::string(gai_strerror(rc)));
    sockaddr_storage addr;
    std::memset(&addr, 0, sizeof(addr));
    std::memcpy(&addr, res->ai_addr, res->ai_addrlen);
    const SockLen addrLen = static_cast<SockLen>(res->ai_addrlen);
    const int family = res->ai_family;
    ::freeaddrinfo(res);

#ifdef _WIN32
    const SOCKET s = ::socket(family, SOCK_DGRAM, IPPROTO_UDP);
    if (s == INVALID_SOCKET)
        return fail("socket: " + errorText(lastSocketError()));
    m_fd = static_cast<intptr_t>(s);
#else
#ifdef SOCK_CLOEXEC
    const int s = ::socket(family, SOCK_DGRAM | SOCK_CLOEXEC, IPPROTO_UDP);
#else
    const int s = ::socket(family, SOCK_DGRAM, IPPROTO_UDP);
#endif
    if (s < 0)
        return fail("socket: " + errorText(lastSocketError()));
#ifndef SOCK_CLOEXEC
    ::fcntl(s, F_SETFD, FD_CLOEXEC);
#endif
    m_fd = s;
#endif

    // A larger send buffer absorbs the bursts of the capture chunks
    const int sndbuf = 4 * 1024 * 1024;
    setOpt(m_fd, SOL_SOCKET, SO_SNDBUF, sndbuf);
    const int one = 1;
    if (family == AF_INET) {
        setOpt(m_fd, SOL_SOCKET, SO_BROADCAST, one);
        const auto *a4 = reinterpret_cast<const sockaddr_in *>(&addr);
        const bool multicast = (ntohl(a4->sin_addr.s_addr) >> 28) == 0xE;
        if (multicast) {
#ifdef _WIN32
            const DWORD t = static_cast<DWORD>(ttl);
#else
            const unsigned char t = static_cast<unsigned char>(ttl);
#endif
            if (!setOpt(m_fd, IPPROTO_IP, IP_MULTICAST_TTL, t))
                return fail("IP_MULTICAST_TTL: " + errorText(lastSocketError()));
            if (!iface.empty()) {
                in_addr local;
                if (::inet_pton(AF_INET, iface.c_str(), &local) == 1) {
                    if (!setOpt(m_fd, IPPROTO_IP, IP_MULTICAST_IF, local))
                        return fail("IP_MULTICAST_IF " + iface + ": " + errorText(lastSocketError()));
                } else {
#ifdef __linux__
                    ip_mreqn mreq;
                    std::memset(&mreq, 0, sizeof(mreq));
                    mreq.imr_ifindex = static_cast<int>(interfaceIndex(iface));
                    if (mreq.imr_ifindex == 0)
                        return fail("unknown network interface \"" + iface + "\"");
                    if (!setOpt(m_fd, IPPROTO_IP, IP_MULTICAST_IF, mreq))
                        return fail("IP_MULTICAST_IF " + iface + ": " + errorText(lastSocketError()));
#else
                    return fail("give the multicast interface as its IPv4 address, not \"" + iface + "\"");
#endif
                }
            }
        } else if (ttl > 0) {
            const int t = ttl;
            setOpt(m_fd, IPPROTO_IP, IP_TTL, t);
        }
    } else if (family == AF_INET6) {
        const int hops = ttl;
        setOpt(m_fd, IPPROTO_IPV6, IPV6_MULTICAST_HOPS, hops);
        if (!iface.empty()) {
            unsigned idx = interfaceIndex(iface);
            if (idx == 0) {
                char *end = nullptr;
                idx = static_cast<unsigned>(std::strtoul(iface.c_str(), &end, 10));
                if (*end != '\0')
                    idx = 0;
            }
            if (idx == 0 || !setOpt(m_fd, IPPROTO_IPV6, IPV6_MULTICAST_IF, idx))
                return fail("IPV6_MULTICAST_IF \"" + iface + "\" (interface name or index)");
        }
    }

#ifdef _WIN32
    if (::connect(static_cast<SOCKET>(m_fd), reinterpret_cast<const sockaddr *>(&addr), addrLen) != 0)
#else
    if (::connect(static_cast<int>(m_fd), reinterpret_cast<const sockaddr *>(&addr), addrLen) != 0)
#endif
        return fail("connect " + host + ":" + service + ": " + errorText(lastSocketError()));

#ifdef _WIN32
    u_long nb = 1;
    ::ioctlsocket(static_cast<SOCKET>(m_fd), FIONBIO, &nb);
#else
    ::fcntl(static_cast<int>(m_fd), F_SETFL, ::fcntl(static_cast<int>(m_fd), F_GETFL) | O_NONBLOCK);
#endif
    return true;
}

bool UdpSender::send(const uint8_t *data, size_t len)
{
    if (m_fd < 0)
        return false;
    // A port-unreachable ICMP for an earlier datagram is reported (and cleared) by the next
    // send, which then sends nothing: UDP has no receiver to wait for, so try once more
    for (int attempt = 0; attempt < 2; ++attempt) {
#ifdef _WIN32
        const int n = ::send(static_cast<SOCKET>(m_fd), reinterpret_cast<const char *>(data), static_cast<int>(len), 0);
#elif defined(MSG_NOSIGNAL)
        const ssize_t n = ::send(static_cast<int>(m_fd), data, len, MSG_NOSIGNAL);
#else
        const ssize_t n = ::send(static_cast<int>(m_fd), data, len, 0);
#endif
        if (n >= 0 && static_cast<size_t>(n) == len)
            return true;
        m_lastErrno = lastSocketError();
#ifdef _WIN32
        const bool icmp = m_lastErrno == WSAECONNRESET || m_lastErrno == WSAECONNREFUSED;
#else
        const bool icmp = m_lastErrno == ECONNREFUSED;
#endif
        if (!icmp)
            return false;
    }
    return true; // only ICMP feedback: nobody listens on the port, which is not a send error
}

std::string UdpSender::lastError() const
{
    return m_lastErrno ? errorText(m_lastErrno) : std::string();
}

} // namespace net
} // namespace tbs
