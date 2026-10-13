#pragma once

#include "export.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace tbs {
namespace bb {

// Reassembles MPEG-2 sections of one PID from TS packets (PUSI + pointer field,
// several sections per packet, 0xFF stuffing, continuity checks).
class TBSBBPROC_API SectionCollector
{
public:
    static constexpr size_t kMaxSection = 4096;

    // pkt: aligned 188-byte packet of this PID. fn(section, len) is called for each
    // complete section (table_id .. end); no CRC check is done here.
    template <typename Fn>
    void push(const uint8_t *pkt, Fn &&fn)
    {
        if (pkt[1] & 0x80)
            return; // transport error
        const uint8_t afc = (pkt[3] >> 4) & 3;
        if (!(afc & 1))
            return;
        size_t off = 4;
        if (afc & 2)
            off += 1 + pkt[4];
        if (off >= 188)
            return;

        const int cc = pkt[3] & 0x0F;
        if (m_lastCc >= 0) {
            if (cc == m_lastCc)
                return; // duplicate packet
            if (cc != ((m_lastCc + 1) & 0x0F))
                restart();
        }
        m_lastCc = cc;

        const uint8_t *payload = pkt + off;
        const size_t plen = 188 - off;
        if (pkt[1] & 0x40) {
            const size_t pointer = payload[0];
            if (1 + pointer > plen) {
                restart();
                return;
            }
            if (m_started && pointer > 0) {
                m_buf.insert(m_buf.end(), payload + 1, payload + 1 + pointer);
                drain(fn);
            }
            m_buf.assign(payload + 1 + pointer, payload + plen);
            m_started = true;
            drain(fn);
        } else if (m_started) {
            m_buf.insert(m_buf.end(), payload, payload + plen);
            drain(fn);
        }
    }

    void reset()
    {
        restart();
        m_lastCc = -1;
    }

private:
    void restart()
    {
        m_buf.clear();
        m_started = false;
    }

    template <typename Fn>
    void drain(Fn &fn)
    {
        while (m_buf.size() >= 3) {
            if (m_buf[0] == 0xFF) { // stuffing: rest of the packet is unused
                restart();
                return;
            }
            const size_t total = 3 + (((m_buf[1] & 0x0F) << 8) | m_buf[2]);
            if (total > kMaxSection) {
                restart();
                return;
            }
            if (m_buf.size() < total)
                return;
            fn(m_buf.data(), total);
            m_buf.erase(m_buf.begin(), m_buf.begin() + static_cast<std::ptrdiff_t>(total));
        }
    }

    std::vector<uint8_t> m_buf;
    int m_lastCc = -1;
    bool m_started = false;
};

} // namespace bb
} // namespace tbs
