#pragma once

#include "Gse.h"
#include "export.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace tbs {
namespace bb {

struct TBSBBPROC_API ProtocolStat
{
    std::string name;
    uint64_t packets = 0;
    uint64_t bytes = 0;
};

struct TBSBBPROC_API FlowStat
{
    int ipVersion = 4;
    uint8_t protocol = 0;
    std::string protocolName;
    std::string src;
    std::string dst;
    uint16_t srcPort = 0; // 0 when the protocol has no ports
    uint16_t dstPort = 0;
    uint64_t packets = 0;
    uint64_t bytes = 0;
    double mbps = 0.0;    // since the previous report
    int64_t lastSeenMs = 0;
};

struct TBSBBPROC_API LabelStat
{
    std::string label; // MAC (6 byte), 3 byte label, "broadcast"
    uint64_t pdus = 0;
    uint64_t bytes = 0;
};

struct TBSBBPROC_API IpReport
{
    uint64_t pdus = 0;
    uint64_t ipv4 = 0;
    uint64_t ipv6 = 0;
    uint64_t nonIp = 0;
    uint64_t malformed = 0;
    uint64_t ipBytes = 0;
    uint64_t multicast = 0;
    uint64_t broadcast = 0;
    uint64_t fragmentsIp = 0;      // IP fragments (not first)
    double packetsPerSec = 0.0;    // since the previous report
    double mbps = 0.0;
    std::vector<ProtocolStat> protocols;  // UDP, TCP, ICMP, ... sorted by bytes
    std::vector<ProtocolStat> etherTypes; // non-IP PDUs by EtherType
    std::vector<FlowStat> topFlows;       // sorted by bytes
    size_t flowCount = 0;
    uint64_t flowsNotTracked = 0;         // packets of flows beyond the table limit
    std::vector<LabelStat> labels;        // sorted by bytes
};

// Per-stream IP statistics from GSE PDUs.
class TBSBBPROC_API IpStatistics
{
public:
    explicit IpStatistics(size_t maxFlows = 20000, size_t maxLabels = 1024);

    void addPdu(const GsePdu &pdu, int64_t nowMs);
    // topN flows and rates relative to the previous call
    IpReport report(size_t topFlows, int64_t nowMs);
    void reset();

    static std::string labelToString(const GsePdu &pdu);
    static std::string protocolName(uint8_t proto);

private:
    struct FlowKey
    {
        uint8_t version;
        uint8_t proto;
        uint16_t sport;
        uint16_t dport;
        uint8_t src[16];
        uint8_t dst[16];
        bool operator==(const FlowKey &o) const;
    };
    struct FlowKeyHash
    {
        size_t operator()(const FlowKey &k) const;
    };
    struct FlowCounters
    {
        uint64_t packets = 0;
        uint64_t bytes = 0;
        uint64_t bytesAtReport = 0;
        int64_t lastSeenMs = 0;
    };
    struct Counter
    {
        uint64_t packets = 0;
        uint64_t bytes = 0;
    };

    void addIp(const uint8_t *p, size_t len, int64_t nowMs);

    size_t m_maxFlows;
    size_t m_maxLabels;
    IpReport m_totals; // scalar counters only
    std::unordered_map<FlowKey, FlowCounters, FlowKeyHash> m_flows;
    std::unordered_map<uint8_t, Counter> m_protocols;
    std::unordered_map<uint16_t, Counter> m_etherTypes;
    std::unordered_map<std::string, Counter> m_labels;
    int64_t m_lastReportMs = 0;
    uint64_t m_lastPdus = 0;
    uint64_t m_lastIpBytes = 0;
};

TBSBBPROC_API std::string ipToString(int version, const uint8_t *addr);

} // namespace bb
} // namespace tbs
