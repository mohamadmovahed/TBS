#pragma once

#include "Gse.h"
#include "export.h"

#include <cstdint>
#include <string>

namespace tbs {
namespace bb {

struct TBSBBPROC_API TunOptions
{
    bool enabled = false;
    std::string name = "tbs0";
    // false: TUN, IPv4/IPv6 packets only
    // true:  TAP, Ethernet frames (GSE label / MPE MAC as destination, bridged frames as received)
    bool tap = false;
    bool bringUp = true; // set the interface up (needs CAP_NET_ADMIN)
};

// Injects decapsulated packets into a Linux TUN/TAP network interface, so other
// programs (routing, multicast receivers, tcpdump) can use the satellite data.
//
// Without root, create a persistent interface owned by your user once:
//   sudo ip tuntap add dev tbs0 mode tun user $USER && sudo ip link set tbs0 up
class TBSBBPROC_API TunWriter
{
public:
    explicit TunWriter(const TunOptions &opt);
    ~TunWriter();
    TunWriter(const TunWriter &) = delete;
    TunWriter &operator=(const TunWriter &) = delete;

    static bool supported(); // Linux only

    bool open(std::string *error, std::string *warning = nullptr);
    void close();
    bool isOpen() const { return m_fd >= 0; }
    bool write(const GsePdu &pdu);

    const std::string &name() const { return m_name; }
    uint64_t packets() const { return m_packets; }
    uint64_t errors() const { return m_errors; }

private:
    TunOptions m_opt;
    std::string m_name;
    int m_fd = -1;
    uint64_t m_packets = 0;
    uint64_t m_errors = 0;
};

} // namespace bb
} // namespace tbs
