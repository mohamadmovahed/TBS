#include "tbsdvb/TsFilter.h"

#include <algorithm>
#include <cstring>

namespace tbs {

namespace {

// Long-form section with a valid CRC, current_next set
bool validSection(const uint8_t *s, size_t len, uint8_t tableId)
{
    return len >= 12 && s[0] == tableId && (s[1] & 0x80) && (s[5] & 0x01) && crc32Mpeg(s, len) == 0;
}

} // namespace

TsFilter::TsFilter(const TsFilterOptions &opt)
    : m_opt(opt)
{
    for (uint16_t p : m_opt.pids)
        if (p < 8192)
            m_extra.set(p);
    reset();
}

void TsFilter::reset()
{
    m_patCollector.reset();
    m_pmtCollector.reset();
    m_pmtPid = -1;
    m_pmtSeen = false;
    m_tsid = -1;
    m_serviceSet.clear();
    m_pat.clear();
    m_pass = m_extra;
    if (m_opt.programNumber <= 0 && m_opt.pids.empty()) {
        m_pass.set();
        if (m_opt.dropNullPackets)
            m_pass.reset(0x1FFF);
    }
}

std::vector<uint16_t> TsFilter::servicePids() const
{
    return m_serviceSet;
}

void TsFilter::push(const uint8_t *pkt, const PacketCallback &out)
{
    if (pkt[0] != 0x47)
        return;
    ++m_in;
    const uint16_t pid = static_cast<uint16_t>(((pkt[1] & 0x1F) << 8) | pkt[2]);

    if (m_opt.programNumber > 0) {
        if (pid == 0) {
            m_patCollector.push(pkt, [this, &out](const uint8_t *s, size_t len) {
                onPat(s, len);
                // One rewritten PAT per original PAT section keeps the original repetition rate
                if (!m_pat.empty())
                    emitPat(out);
            });
            return;
        }
        if (static_cast<int>(pid) == m_pmtPid)
            m_pmtCollector.push(pkt, [this](const uint8_t *s, size_t len) { onPmt(s, len); });
    }
    if (m_pass.test(pid)) {
        ++m_out;
        out(pkt);
    }
}

void TsFilter::onPat(const uint8_t *s, size_t len)
{
    if (!validSection(s, len, 0x00))
        return;
    const int tsid = (s[3] << 8) | s[4];
    int pmtPid = -1;
    for (size_t i = 8; i + 4 <= len - 4; i += 4) {
        const int program = (s[i] << 8) | s[i + 1];
        if (program == m_opt.programNumber) {
            pmtPid = ((s[i + 2] & 0x1F) << 8) | s[i + 3];
            break;
        }
    }
    // PAT sections can be split over several section numbers; only a match updates the PMT PID
    if (pmtPid < 0) {
        if (s[6] == 0 && s[7] == 0 && m_pmtPid >= 0) { // single-section PAT without the service: it is gone
            m_pass = m_extra;
            m_pmtPid = -1;
            m_pmtSeen = false;
            m_serviceSet.clear();
            m_pat.clear();
        }
        return;
    }
    if (pmtPid != m_pmtPid || tsid != m_tsid) {
        m_pmtPid = pmtPid;
        m_tsid = tsid;
        m_pmtSeen = false;
        m_pmtCollector.reset();
        m_pass = m_extra;
        m_pass.set(static_cast<size_t>(pmtPid));
        m_serviceSet = {static_cast<uint16_t>(pmtPid)};
        if (!m_pat.empty())
            m_patVersion = static_cast<uint8_t>((m_patVersion + 1) & 0x1F);
        buildPat();
    }
}

void TsFilter::onPmt(const uint8_t *s, size_t len)
{
    if (!validSection(s, len, 0x02) || ((s[3] << 8) | s[4]) != m_opt.programNumber || len < 16)
        return;
    std::vector<uint16_t> pids = {static_cast<uint16_t>(m_pmtPid)};
    auto addCaPids = [&pids](const uint8_t *d, size_t dlen) {
        size_t i = 0;
        while (i + 2 <= dlen) {
            const uint8_t tag = d[i];
            const size_t l = d[i + 1];
            if (i + 2 + l > dlen)
                break;
            if (tag == 0x09 && l >= 4)
                pids.push_back(static_cast<uint16_t>(((d[i + 4] & 0x1F) << 8) | d[i + 5]));
            i += 2 + l;
        }
    };
    const size_t end = len - 4;
    const uint16_t pcr = static_cast<uint16_t>(((s[8] & 0x1F) << 8) | s[9]);
    if (pcr != 0x1FFF)
        pids.push_back(pcr);
    const size_t progInfo = static_cast<size_t>(((s[10] & 0x0F) << 8) | s[11]);
    if (12 + progInfo > end)
        return;
    addCaPids(s + 12, progInfo);
    size_t i = 12 + progInfo;
    while (i + 5 <= end) {
        pids.push_back(static_cast<uint16_t>(((s[i + 1] & 0x1F) << 8) | s[i + 2]));
        const size_t esInfo = static_cast<size_t>(((s[i + 3] & 0x0F) << 8) | s[i + 4]);
        if (i + 5 + esInfo > end)
            break;
        addCaPids(s + i + 5, esInfo);
        i += 5 + esInfo;
    }
    std::sort(pids.begin(), pids.end());
    pids.erase(std::unique(pids.begin(), pids.end()), pids.end());
    m_pmtSeen = true;
    if (pids == m_serviceSet)
        return;
    m_serviceSet = pids;
    m_pass = m_extra;
    for (uint16_t p : pids)
        m_pass.set(p);
}

void TsFilter::buildPat()
{
    // table_id .. last_section_number, one program, CRC
    m_pat = {0x00, 0xB0, 13, static_cast<uint8_t>(m_tsid >> 8), static_cast<uint8_t>(m_tsid),
             static_cast<uint8_t>(0xC1 | (m_patVersion << 1)), 0x00, 0x00,
             static_cast<uint8_t>(m_opt.programNumber >> 8), static_cast<uint8_t>(m_opt.programNumber),
             static_cast<uint8_t>(0xE0 | (m_pmtPid >> 8)), static_cast<uint8_t>(m_pmtPid)};
    const uint32_t crc = crc32Mpeg(m_pat.data(), m_pat.size());
    for (int sh = 24; sh >= 0; sh -= 8)
        m_pat.push_back(static_cast<uint8_t>(crc >> sh));
}

void TsFilter::emitPat(const PacketCallback &out)
{
    uint8_t pkt[188];
    std::memset(pkt, 0xFF, sizeof(pkt));
    pkt[0] = 0x47;
    pkt[1] = 0x40; // PUSI, PID 0
    pkt[2] = 0x00;
    pkt[3] = static_cast<uint8_t>(0x10 | m_patCc);
    m_patCc = static_cast<uint8_t>((m_patCc + 1) & 0x0F);
    pkt[4] = 0x00; // pointer field
    std::memcpy(pkt + 5, m_pat.data(), m_pat.size());
    ++m_out;
    out(pkt);
}

} // namespace tbs
