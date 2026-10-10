#pragma once

#include "Gse.h"
#include "export.h"

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace tbs {
namespace bb {

struct TBSBBPROC_API PcapOptions
{
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

    // Returns true if the PDU was written
    bool write(const GsePdu &pdu, int64_t unixMicros);

    const std::string &fileName() const { return m_path; }
    uint64_t packets() const { return m_packets; }
    uint64_t bytes() const { return m_bytes; }
    const std::string &lastError() const { return m_error; }

    static std::string resolvePath(const PcapOptions &opt);

private:
    void writeRecord(const uint8_t *a, size_t alen, const uint8_t *b, size_t blen, int64_t unixMicros);

    PcapOptions m_opt;
    std::FILE *m_file = nullptr;
    std::vector<char> m_buffer;
    std::string m_path;
    std::string m_error;
    uint64_t m_packets = 0;
    uint64_t m_bytes = 0;
};

} // namespace bb
} // namespace tbs
