#include "TsAnalyzer.h"

TsAnalyzer::TsAnalyzer()
{
    reset();
}

void TsAnalyzer::reset()
{
    m_buf.clear();
    m_synced = false;
    m_packets = m_syncLosses = m_ccErrors = m_teiErrors = 0;
    m_pidCounts.fill(0);
    m_lastCc.fill(0xFF);
}

void TsAnalyzer::push(const uint8_t *data, size_t len, const PacketCallback &cb)
{
    m_buf.insert(m_buf.end(), data, data + len);

    const uint8_t *buf = m_buf.data();
    const size_t size = m_buf.size();
    size_t pos = 0;
    size_t runStart = 0;
    size_t runCount = 0;

    auto flushRun = [&]() {
        if (runCount && cb)
            cb(buf + runStart, runCount);
        runCount = 0;
    };

    while (pos + kPacketSize <= size) {
        const bool here = buf[pos] == kSyncByte;
        const bool nextKnown = pos + kPacketSize < size;
        const bool next = nextKnown && buf[pos + kPacketSize] == kSyncByte;

        // While synced a single sync byte is enough; to (re)acquire sync we
        // want to see the following packet's sync byte as well.
        if (here && (m_synced ? (next || !nextKnown) : next)) {
            m_synced = true;
            if (runCount == 0)
                runStart = pos;
            inspect(buf + pos);
            ++runCount;
            pos += kPacketSize;
            continue;
        }
        if (here && !m_synced && !nextKnown)
            break; // wait for more data to confirm sync

        flushRun();
        if (m_synced) {
            m_synced = false;
            ++m_syncLosses;
        }
        ++pos;
    }
    flushRun();
    m_buf.erase(m_buf.begin(), m_buf.begin() + static_cast<std::ptrdiff_t>(pos));
}

void TsAnalyzer::inspect(const uint8_t *pkt)
{
    ++m_packets;
    if (pkt[1] & 0x80)
        ++m_teiErrors;
    const uint16_t pid = pidOf(pkt);
    ++m_pidCounts[pid];
    if (pid == 0x1FFF)
        return;

    const uint8_t afc = (pkt[3] >> 4) & 0x3;
    const uint8_t cc = pkt[3] & 0x0F;
    if (!(afc & 0x1))
        return; // no payload, CC does not increment

    const bool discontinuity = (afc & 0x2) && pkt[4] > 0 && (pkt[5] & 0x80);
    const uint8_t last = m_lastCc[pid];
    if (last != 0xFF && !discontinuity && cc != last && cc != ((last + 1) & 0x0F))
        ++m_ccErrors;
    m_lastCc[pid] = cc;
}

std::vector<std::pair<uint16_t, uint64_t>> TsAnalyzer::pidSnapshot() const
{
    std::vector<std::pair<uint16_t, uint64_t>> out;
    for (uint16_t pid = 0; pid < 8192; ++pid) {
        if (m_pidCounts[pid])
            out.emplace_back(pid, m_pidCounts[pid]);
    }
    return out;
}
