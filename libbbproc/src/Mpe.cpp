#include "tbsbbproc/Mpe.h"

#include "tbsbbproc/DvbText.h"

namespace tbs {
namespace bb {

namespace {
constexpr uint8_t kMpeTableId = 0x3E;
constexpr size_t kMaxPids = 64;
}

void MpeDecapsulator::reset()
{
    m_pids.clear();
    m_stats = MpeStats();
}

MpeStats MpeDecapsulator::stats() const
{
    MpeStats s = m_stats;
    for (const auto &p : m_pids)
        s.pids.push_back(p.first);
    return s;
}

void MpeDecapsulator::pushPackets(const uint8_t *packets, size_t count, const PduCallback &cb)
{
    for (size_t n = 0; n < count; ++n) {
        const uint8_t *p = packets + n * 188;
        if (p[0] != 0x47 || (p[1] & 0x80))
            continue;
        const uint16_t pid = static_cast<uint16_t>(((p[1] & 0x1F) << 8) | p[2]);
        if (pid < 0x0020 || pid == 0x1FFF)
            continue; // PSI/SI and null packets never carry MPE
        auto it = m_pids.find(pid);
        if (it == m_pids.end()) {
            // Detect: a section start whose table id is 0x3E
            if (!(p[1] & 0x40) || m_pids.size() >= kMaxPids)
                continue;
            const uint8_t afc = (p[3] >> 4) & 3;
            if (!(afc & 1))
                continue;
            size_t off = 4 + ((afc & 2) ? 1u + p[4] : 0u);
            if (off >= 187)
                continue;
            off += 1 + p[off]; // pointer field
            if (off >= 188 || p[off] != kMpeTableId)
                continue;
            it = m_pids.emplace(pid, SectionCollector()).first;
        }
        it->second.push(p, [&](const uint8_t *s, size_t len) { pushSection(s, len, cb); });
    }
}

void MpeDecapsulator::pushSection(const uint8_t *s, size_t len, const PduCallback &cb)
{
    if (len < 3 || s[0] != kMpeTableId)
        return;
    ++m_stats.sections;
    // Header (12 bytes) + at least a minimal datagram + CRC/checksum (4)
    if (len < 12 + 4 + 1) {
        ++m_stats.invalid;
        return;
    }
    const bool syntax = s[1] & 0x80;
    if (syntax && crc32Mpeg(s, len) != 0) {
        ++m_stats.crcErrors;
        return;
    }
    if (!(s[5] & 0x01))
        return; // not applicable yet
    if ((s[5] >> 4) & 0x03) {
        ++m_stats.scrambled;
        return;
    }
    const bool llcSnap = s[5] & 0x02;

    GsePdu pdu;
    pdu.labelType = 0;
    pdu.labelLen = 6;
    // MAC_address_1 (most significant) .. 4 at bytes 11..8, 5 and 6 at bytes 4..3
    pdu.label[0] = s[11];
    pdu.label[1] = s[10];
    pdu.label[2] = s[9];
    pdu.label[3] = s[8];
    pdu.label[4] = s[4];
    pdu.label[5] = s[3];

    const uint8_t *d = s + 12;
    size_t n = len - 12 - 4;
    if (llcSnap) {
        // LLC AA AA 03, OUI (3 bytes), EtherType
        if (n < 8 || d[0] != 0xAA || d[1] != 0xAA || d[2] != 0x03) {
            ++m_stats.invalid;
            return;
        }
        pdu.protocolType = static_cast<uint16_t>((d[6] << 8) | d[7]);
        d += 8;
        n -= 8;
        ++m_stats.llcSnap;
    } else {
        const uint8_t version = d[0] >> 4;
        if (version == 4)
            pdu.protocolType = 0x0800;
        else if (version == 6)
            pdu.protocolType = 0x86DD;
        else {
            ++m_stats.invalid;
            return;
        }
    }
    // Sections may be padded: trim to the IP datagram length
    if (pdu.protocolType == 0x0800 && n >= 20) {
        const size_t total = static_cast<size_t>((d[2] << 8) | d[3]);
        if (total >= 20 && total <= n)
            n = total;
    } else if (pdu.protocolType == 0x86DD && n >= 40) {
        const size_t total = 40u + static_cast<size_t>((d[4] << 8) | d[5]);
        if (total <= n)
            n = total;
    }
    pdu.data = d;
    pdu.len = n;
    ++m_stats.datagrams;
    if (cb)
        cb(pdu);
}

} // namespace bb
} // namespace tbs
