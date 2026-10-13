#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace tbs {
namespace net {

// Initialises the socket library (Winsock) once; no-op elsewhere.
bool startup(std::string *error);

// Connected UDP socket for sending: unicast, broadcast or multicast, IPv4 or IPv6.
class UdpSender
{
public:
    UdpSender() = default;
    ~UdpSender();
    UdpSender(const UdpSender &) = delete;
    UdpSender &operator=(const UdpSender &) = delete;

    // iface: multicast output interface, a local IPv4 address or an interface name
    bool open(const std::string &host, uint16_t port, const std::string &iface, int ttl, std::string *error);
    void close();
    bool isOpen() const;
    // false if the datagram could not be sent (buffer full, network unreachable)
    bool send(const uint8_t *data, size_t len);
    std::string lastError() const;

private:
    intptr_t m_fd = -1;
    int m_lastErrno = 0;
};

// Text for errno / WSAGetLastError()
std::string errorText(int code);

} // namespace net
} // namespace tbs
