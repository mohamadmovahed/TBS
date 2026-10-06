#include "tbsdvb/BbFrameToTs.h"

#include <algorithm>
#include <cstring>

namespace tbs {

void BbFrameToTs::reset()
{
    m_partial.clear();
    m_synced = false;
    m_upBytes = 0;
    m_packets = 0;
}

void BbFrameToTs::emitPacket(const uint8_t *up, bool hem, const PacketCallback &cb)
{
    uint8_t pkt[188];
    pkt[0] = 0x47;
    if (hem)
        std::memcpy(pkt + 1, up, 187);     // sync byte removed in HEM
    else
        std::memcpy(pkt + 1, up + 1, 187); // first byte is the CRC-8 of the previous UP
    ++m_packets;
    if (cb)
        cb(pkt);
}

void BbFrameToTs::pushFrame(const uint8_t *frame, size_t len, const BbHeader &hdr, const PacketCallback &cb)
{
    if (hdr.streamType() != BbHeader::StreamType::Transport || !hdr.valid() || len < BbHeader::kSize)
        return;

    const bool hem = hdr.mode == BbHeader::Mode::HighEfficiency;
    const size_t upBytes = hem ? 187u + (hdr.npd() ? 1u : 0u) : hdr.upl / 8u;
    if (upBytes < 187 || (!hem && upBytes < 188))
        return;
    if (upBytes != m_upBytes) {
        m_partial.clear();
        m_synced = false;
        m_upBytes = upBytes;
    }

    const uint8_t *data = frame + BbHeader::kSize;
    const size_t n = std::min(len - BbHeader::kSize, hdr.dataFieldBytes());

    if (hdr.syncd == 0xFFFF) {
        // No user packet starts in this data field
        if (m_synced)
            m_partial.insert(m_partial.end(), data, data + n);
        if (m_partial.size() > upBytes) {
            m_partial.clear();
            m_synced = false;
        }
        return;
    }

    const size_t syncdBytes = hdr.syncd / 8u;
    if (syncdBytes > n) {
        m_partial.clear();
        m_synced = false;
        return;
    }

    if (m_synced && !m_partial.empty()) {
        m_partial.insert(m_partial.end(), data, data + syncdBytes);
        if (m_partial.size() == upBytes)
            emitPacket(m_partial.data(), hem, cb);
    }
    m_partial.clear();
    m_synced = true;

    size_t pos = syncdBytes;
    while (pos + upBytes <= n) {
        emitPacket(data + pos, hem, cb);
        pos += upBytes;
    }
    m_partial.assign(data + pos, data + n);
}

} // namespace tbs
