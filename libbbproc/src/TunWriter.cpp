#include "tbsbbproc/TunWriter.h"

#include <cerrno>
#include <cstring>

#if defined(__linux__)
#include <fcntl.h>
#include <linux/if_tun.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <unistd.h>
#endif

namespace tbs {
namespace bb {

TunWriter::TunWriter(const TunOptions &opt)
    : m_opt(opt)
{
}

TunWriter::~TunWriter()
{
    close();
}

bool TunWriter::supported()
{
#if defined(__linux__)
    return true;
#else
    return false;
#endif
}

bool TunWriter::open(std::string *error, std::string *warning)
{
    close();
#if defined(__linux__)
    m_fd = ::open("/dev/net/tun", O_RDWR | O_NONBLOCK | O_CLOEXEC);
    if (m_fd < 0) {
        if (error)
            *error = std::string("Cannot open /dev/net/tun: ") + std::strerror(errno);
        return false;
    }
    ifreq ifr;
    std::memset(&ifr, 0, sizeof(ifr));
    ifr.ifr_flags = static_cast<short>((m_opt.tap ? IFF_TAP : IFF_TUN) | IFF_NO_PI);
    std::strncpy(ifr.ifr_name, m_opt.name.c_str(), IFNAMSIZ - 1);
    if (::ioctl(m_fd, TUNSETIFF, &ifr) < 0) {
        const int e = errno;
        ::close(m_fd);
        m_fd = -1;
        if (error) {
            *error = "Cannot create " + std::string(m_opt.tap ? "TAP" : "TUN") + " interface '" + m_opt.name + "': " + std::strerror(e);
            if (e == EPERM || e == EACCES || e == EBUSY)
                *error += ". Run as root, or create it once for your user: sudo ip tuntap add dev " + m_opt.name + " mode "
                    + (m_opt.tap ? "tap" : "tun") + " user $USER && sudo ip link set " + m_opt.name + " up";
            else if (e == EINVAL)
                *error += " (an interface with this name exists with another mode?)";
        }
        return false;
    }
    m_name = ifr.ifr_name;

    if (m_opt.bringUp) {
        const int s = ::socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
        bool up = false;
        if (s >= 0) {
            ifreq fl;
            std::memset(&fl, 0, sizeof(fl));
            std::strncpy(fl.ifr_name, m_name.c_str(), IFNAMSIZ - 1);
            if (::ioctl(s, SIOCGIFFLAGS, &fl) == 0) {
                up = fl.ifr_flags & IFF_UP;
                if (!up) {
                    fl.ifr_flags = static_cast<short>(fl.ifr_flags | IFF_UP | IFF_RUNNING);
                    up = ::ioctl(s, SIOCSIFFLAGS, &fl) == 0;
                }
            }
            ::close(s);
        }
        if (!up && warning)
            *warning = "Interface " + m_name + " is down and could not be brought up (needs CAP_NET_ADMIN): sudo ip link set "
                + m_name + " up";
    }
    m_packets = m_errors = 0;
    return true;
#else
    (void)warning;
    if (error)
        *error = "TUN/TAP output is only available on Linux";
    return false;
#endif
}

void TunWriter::close()
{
#if defined(__linux__)
    if (m_fd >= 0) {
        ::close(m_fd);
        m_fd = -1;
    }
#endif
}

bool TunWriter::write(const GsePdu &pdu)
{
#if defined(__linux__)
    if (m_fd < 0)
        return false;
    const bool ip = pdu.protocolType == 0x0800 || pdu.protocolType == 0x86DD;
    ssize_t n;
    if (!m_opt.tap) {
        if (!ip)
            return false;
        if (pdu.bridged) {
            if (pdu.len < 14)
                return false;
            n = ::write(m_fd, pdu.data + 14, pdu.len - 14);
        } else {
            n = ::write(m_fd, pdu.data, pdu.len);
        }
    } else if (pdu.bridged) {
        n = ::write(m_fd, pdu.data, pdu.len);
    } else {
        uint8_t eth[14];
        ethernetHeaderFor(pdu, eth);
        iovec iov[2] = {{eth, sizeof(eth)}, {const_cast<uint8_t *>(pdu.data), pdu.len}};
        n = ::writev(m_fd, iov, 2);
    }
    if (n < 0) {
        ++m_errors; // EIO while the interface is down, EAGAIN when the queue is full
        return false;
    }
    ++m_packets;
    return true;
#else
    (void)pdu;
    return false;
#endif
}

} // namespace bb
} // namespace tbs
