#pragma once

#include "Gse.h"
#include "export.h"

#include <cstdint>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

namespace tbs {
namespace bb {

enum class PcapFormat {
    Pcap = 0,   // classic libpcap (all streams in one capture)
    PcapNg = 1, // pcapng with one named interface per stream (ISI, MPE)
};

struct TBSBBPROC_API PcapOptions
{
    PcapFormat format = PcapFormat::Pcap;
    bool enabled = false;
    std::string path;            // empty = <home>/gse.pcap
    bool timestampSuffix = true;
    // true: Ethernet frames (the GSE 6-byte label becomes the destination MAC, so it
    //       stays visible in Wireshark; bridged frames are written as received)
    // false: raw IP (LINKTYPE_RAW), only IPv4/IPv6
    bool ethernetFraming = true;
    bool ipOnly = true;          // skip non-IP PDUs (ARP, ...) in Ethernet mode
};

// Classic libpcap file writer for GSE PDUs.
class TBSBBPROC_API PcapWriter
{
public:
    explicit PcapWriter(const PcapOptions &opt);
    ~PcapWriter();
    PcapWriter(const PcapWriter &) = delete;
    PcapWriter &operator=(const PcapWriter &) = delete;

    bool open(std::string *error);
    void close();
    // Pushes buffered packets to disk so the file can be opened while capturing
    void flush();
    bool isOpen() const { return m_file != nullptr; }

    // Returns true if the PDU was written. interfaceKey/interfaceName select the
    // pcapng interface (created on first use); ignored for classic pcap.
    bool write(const GsePdu &pdu, int64_t unixMicros, int interfaceKey = 0, const std::string &interfaceName = std::string());

    const std::string &fileName() const { return m_path; }
    uint64_t packets() const { return m_packets; }
    uint64_t bytes() const { return m_bytes; }
    const std::string &lastError() const { return m_error; }

    static std::string resolvePath(const PcapOptions &opt);

private:
    void writeRecord(const uint8_t *a, size_t alen, const uint8_t *b, size_t blen, int64_t unixMicros, uint32_t iface);
    uint32_t interfaceId(int key, const std::string &name);
    bool put(const void *p, size_t n);

    PcapOptions m_opt;
    std::FILE *m_file = nullptr;
    std::vector<char> m_buffer;
    std::string m_path;
    std::string m_error;
    uint64_t m_packets = 0;
    uint64_t m_bytes = 0;
    std::map<int, uint32_t> m_interfaces; // pcapng: stream key -> interface id
};

} // namespace bb
} // namespace tbs
