#pragma once

#include "BbHeader.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

// Recovers MPEG-TS packets from BBFrames whose MATYPE says "Transport stream"
// (normal mode and high efficiency mode). Used for the video preview while
// capturing BBFrames. ISSY fields and deleted null packets are skipped.
class BbFrameToTs
{
public:
    using PacketCallback = std::function<void(const uint8_t *pkt188)>;

    void pushFrame(const uint8_t *frame, size_t len, const BbHeader &hdr, const PacketCallback &cb);
    void reset();

    uint64_t packets() const { return m_packets; }

private:
    void emitPacket(const uint8_t *up, bool hem, const PacketCallback &cb);

    std::vector<uint8_t> m_partial;
    bool m_synced = false;
    size_t m_upBytes = 0;
    uint64_t m_packets = 0;
};
