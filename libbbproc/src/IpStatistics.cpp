#include "tbsbbproc/IpStatistics.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace tbs {
namespace bb {

namespace {
inline uint16_t u16(const uint8_t *p)
{
    return static_cast<uint16_t>((p[0] << 8) | p[1]);
}
} // namespace

std::string ipToString(int version, const uint8_t *a)
{
    char buf[64];
    if (version == 4) {
        std::snprintf(buf, sizeof(buf), "%u.%u.%u.%u", a[0], a[1], a[2], a[3]);
        return buf;
    }
    // RFC 5952: lower case, longest run of zero groups compressed
    uint16_t g[8];
    for (int i = 0; i < 8; ++i)
        g[i] = u16(a + 2 * i);
    int bestStart = -1, bestLen = 0;
    for (int i = 0; i < 8;) {
        if (g[i] != 0) {
            ++i;
            continue;
        }
        int j = i;
        while (j < 8 && g[j] == 0)
            ++j;
        if (j - i > bestLen && j - i >= 2) {
            bestStart = i;
            bestLen = j - i;
        }
        i = j;
    }
    std::string s;
    for (int i = 0; i < 8; ++i) {
        if (i == bestStart) {
            s += "::";
            i += bestLen - 1;
            continue;
        }
        if (!s.empty() && s.back() != ':')
            s += ':';
        std::snprintf(buf, sizeof(buf), "%x", g[i]);
        s += buf;
    }
    return s;
}

bool IpStatistics::FlowKey::operator==(const FlowKey &o) const
{
    return version == o.version && proto == o.proto && sport == o.sport && dport == o.dport
        && std::memcmp(src, o.src, 16) == 0 && std::memcmp(dst, o.dst, 16) == 0;
}

size_t IpStatistics::FlowKeyHash::operator()(const FlowKey &k) const
{
    uint64_t h = 1469598103934665603ull;
    auto mix = [&h](const uint8_t *p, size_t n) {
        for (size_t i = 0; i < n; ++i) {
            h ^= p[i];
            h *= 1099511628211ull;
        }
    };
    mix(&k.version, 1);
    mix(&k.proto, 1);
    mix(reinterpret_cast<const uint8_t *>(&k.sport), 2);
    mix(reinterpret_cast<const uint8_t *>(&k.dport), 2);
    mix(k.src, 16);
    mix(k.dst, 16);
    return static_cast<size_t>(h);
}

IpStatistics::IpStatistics(size_t maxFlows, size_t maxLabels)
    : m_maxFlows(maxFlows)
    , m_maxLabels(maxLabels)
{
}

void IpStatistics::reset()
{
    *this = IpStatistics(m_maxFlows, m_maxLabels);
}

std::string IpStatistics::protocolName(uint8_t p)
{
    switch (p) {
    case 1: return "ICMP";
    case 2: return "IGMP";
    case 4: return "IP-in-IP";
    case 6: return "TCP";
    case 17: return "UDP";
    case 41: return "IPv6-in-IP";
    case 47: return "GRE";
    case 50: return "ESP";
    case 51: return "AH";
    case 58: return "ICMPv6";
    case 89: return "OSPF";
    case 103: return "PIM";
    case 132: return "SCTP";
    default: return "IP proto " + std::to_string(p);
    }
}

std::string IpStatistics::labelToString(const GsePdu &pdu)
{
    if (pdu.labelLen == 0)
        return pdu.labelType == 2 ? "broadcast" : "none";
    std::string s;
    char buf[4];
    for (int i = 0; i < pdu.labelLen; ++i) {
        std::snprintf(buf, sizeof(buf), i ? ":%02x" : "%02x", pdu.label[i]);
        s += buf;
    }
    return s;
}

void IpStatistics::addPdu(const GsePdu &pdu, int64_t nowMs)
{
    ++m_totals.pdus;
    {
        const std::string label = labelToString(pdu);
        auto it = m_labels.find(label);
        if (it == m_labels.end() && m_labels.size() < m_maxLabels)
            it = m_labels.emplace(label, Counter()).first;
        if (it != m_labels.end()) {
            ++it->second.packets;
            it->second.bytes += pdu.len;
        }
    }

    const uint8_t *p = pdu.data;
    size_t len = pdu.len;
    uint16_t type = pdu.protocolType;
    if (pdu.bridged) {
        if (len < 14) {
            ++m_totals.malformed;
            return;
        }
        if (p[0] == 0xFF && p[1] == 0xFF && p[2] == 0xFF && p[3] == 0xFF && p[4] == 0xFF && p[5] == 0xFF)
            ++m_totals.broadcast;
        p += 14;
        len -= 14;
        if (type == 0x8100 && len >= 4) { // 802.1Q tag
            type = u16(p + 2);
            p += 4;
            len -= 4;
        }
    }
    if (type == 0x0800 || type == 0x86DD) {
        addIp(p, len, nowMs);
        return;
    }
    ++m_totals.nonIp;
    if (m_etherTypes.size() < 256 || m_etherTypes.count(type)) {
        auto &c = m_etherTypes[type];
        ++c.packets;
        c.bytes += len;
    }
}

void IpStatistics::addIp(const uint8_t *p, size_t len, int64_t nowMs)
{
    FlowKey k;
    std::memset(&k, 0, sizeof(k));
    size_t payloadOff = 0;
    bool firstFragment = true;

    if (len >= 20 && (p[0] >> 4) == 4) {
        const size_t ihl = (p[0] & 0x0F) * 4u;
        if (ihl < 20 || ihl > len) {
            ++m_totals.malformed;
            return;
        }
        k.version = 4;
        k.proto = p[9];
        std::memcpy(k.src, p + 12, 4);
        std::memcpy(k.dst, p + 16, 4);
        firstFragment = (u16(p + 6) & 0x1FFF) == 0;
        if (!firstFragment)
            ++m_totals.fragmentsIp;
        payloadOff = ihl;
        ++m_totals.ipv4;
        if (p[16] >= 224 && p[16] <= 239)
            ++m_totals.multicast;
        else if (p[16] == 255 && p[17] == 255 && p[18] == 255 && p[19] == 255)
            ++m_totals.broadcast;
    } else if (len >= 40 && (p[0] >> 4) == 6) {
        k.version = 6;
        k.proto = p[6];
        std::memcpy(k.src, p + 8, 16);
        std::memcpy(k.dst, p + 24, 16);
        payloadOff = 40;
        // Skip the common extension headers to find the transport protocol
        for (int n = 0; n < 8; ++n) {
            if ((k.proto == 0 || k.proto == 43 || k.proto == 60) && payloadOff + 8 <= len) {
                k.proto = p[payloadOff];
                payloadOff += (p[payloadOff + 1] + 1u) * 8u;
            } else if (k.proto == 44 && payloadOff + 8 <= len) {
                firstFragment = (u16(p + payloadOff + 2) & 0xFFF8) == 0;
                if (!firstFragment)
                    ++m_totals.fragmentsIp;
                k.proto = p[payloadOff];
                payloadOff += 8;
            } else {
                break;
            }
        }
        ++m_totals.ipv6;
        if (p[24] == 0xFF)
            ++m_totals.multicast;
    } else {
        ++m_totals.malformed;
        return;
    }
    m_totals.ipBytes += len;

    if (firstFragment && (k.proto == 6 || k.proto == 17 || k.proto == 132) && payloadOff + 4 <= len) {
        k.sport = u16(p + payloadOff);
        k.dport = u16(p + payloadOff + 2);
    }

    auto &pc = m_protocols[k.proto];
    ++pc.packets;
    pc.bytes += len;

    auto it = m_flows.find(k);
    if (it == m_flows.end()) {
        if (m_flows.size() >= m_maxFlows) {
            ++m_totals.flowsNotTracked;
            return;
        }
        it = m_flows.emplace(k, FlowCounters()).first;
    }
    ++it->second.packets;
    it->second.bytes += len;
    it->second.lastSeenMs = nowMs;
}

IpReport IpStatistics::report(size_t topFlows, int64_t nowMs)
{
    IpReport r = m_totals;
    const double dt = m_lastReportMs > 0 ? (nowMs - m_lastReportMs) / 1000.0 : 0.0;
    if (dt > 0) {
        r.packetsPerSec = (m_totals.pdus - m_lastPdus) / dt;
        r.mbps = (m_totals.ipBytes - m_lastIpBytes) * 8.0 / dt / 1e6;
    }

    for (const auto &p : m_protocols)
        r.protocols.push_back({protocolName(p.first), p.second.packets, p.second.bytes});
    for (const auto &e : m_etherTypes) {
        char buf[16];
        std::snprintf(buf, sizeof(buf), "0x%04X", e.first);
        std::string name = buf;
        if (e.first == 0x0806)
            name = "ARP";
        else if (e.first == 0x8100)
            name = "VLAN";
        else if (e.first == 0x8847)
            name = "MPLS";
        r.etherTypes.push_back({name, e.second.packets, e.second.bytes});
    }
    for (const auto &l : m_labels)
        r.labels.push_back({l.first, l.second.packets, l.second.bytes});
    auto byBytes = [](const auto &a, const auto &b) { return a.bytes > b.bytes; };
    std::sort(r.protocols.begin(), r.protocols.end(), byBytes);
    std::sort(r.etherTypes.begin(), r.etherTypes.end(), byBytes);
    std::sort(r.labels.begin(), r.labels.end(), byBytes);
    if (r.labels.size() > 200)
        r.labels.resize(200);

    // Top flows by bytes
    std::vector<std::pair<const FlowKey *, FlowCounters *>> all;
    all.reserve(m_flows.size());
    for (auto &f : m_flows)
        all.emplace_back(&f.first, &f.second);
    const size_t n = std::min(topFlows, all.size());
    std::partial_sort(all.begin(), all.begin() + static_cast<std::ptrdiff_t>(n), all.end(),
                      [](const auto &a, const auto &b) { return a.second->bytes > b.second->bytes; });
    for (size_t i = 0; i < n; ++i) {
        const FlowKey &k = *all[i].first;
        const FlowCounters &c = *all[i].second;
        FlowStat f;
        f.ipVersion = k.version;
        f.protocol = k.proto;
        f.protocolName = protocolName(k.proto);
        f.src = ipToString(k.version, k.src);
        f.dst = ipToString(k.version, k.dst);
        f.srcPort = k.sport;
        f.dstPort = k.dport;
        f.packets = c.packets;
        f.bytes = c.bytes;
        f.lastSeenMs = c.lastSeenMs;
        if (dt > 0)
            f.mbps = (c.bytes - c.bytesAtReport) * 8.0 / dt / 1e6;
        r.topFlows.push_back(f);
    }
    for (auto &f : m_flows)
        f.second.bytesAtReport = f.second.bytes;
    r.flowCount = m_flows.size();

    m_lastReportMs = nowMs;
    m_lastPdus = m_totals.pdus;
    m_lastIpBytes = m_totals.ipBytes;
    return r;
}

} // namespace bb
} // namespace tbs
