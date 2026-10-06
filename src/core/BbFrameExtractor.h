#pragma once

#include "BbHeader.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

// Rebuilds BBFrames from the pseudo transport stream emitted by STiD135 based
// cards (TBS 6903x/6909x with "options stid135 bbframe=1", Digital Devices).
//
// Each BBFrame is carried in private sections on PID 0x010E (0x0118 on the TBS
// Windows driver):
//   first packet : 47 41 0E 1x 00 80 00 L B8 <BBHeader> <169 data bytes>
//   next packets : 47 41 0E 1x 00 80 00 L N  <179 data bytes>, N = 1, 2, ...
//   last packet  : 47 41 0E 1x 00 80 00 L N  <L-1 data bytes> <FF padding>
class BbFrameExtractor
{
public:
    static constexpr uint16_t kPid = 0x010E;
    static constexpr uint16_t kPidWindows = 0x0118;
    static constexpr uint8_t kStartMarker = 0xB8;
    static constexpr uint8_t kFullSectionLength = 180;

    // frame points to BBHeader + data field (trimmed to DFL)
    using FrameCallback = std::function<void(const uint8_t *frame, size_t len, const BbHeader &hdr)>;

    static bool isBbFramePid(uint16_t pid) { return pid == kPid || pid == kPidWindows; }

    // Feed one aligned 188-byte TS packet. Packets on other PIDs are ignored.
    void pushPacket(const uint8_t *pkt, const FrameCallback &cb);
    void reset();

    uint64_t carrierPackets() const { return m_carrierPackets; }
    uint64_t frames() const { return m_frames; }
    uint64_t payloadBytes() const { return m_payloadBytes; }
    uint64_t crcErrors() const { return m_crcErrors; }
    uint64_t sequenceErrors() const { return m_sequenceErrors; }
    uint64_t lengthErrors() const { return m_lengthErrors; }

private:
    enum class State { Idle, Collecting, Draining };

    // Emits the frame if it is complete; returns true if a frame was emitted.
    bool tryEmit(const FrameCallback &cb, bool final);

    State m_state = State::Idle;
    std::vector<uint8_t> m_frame;
    uint8_t m_nextCounter = 0;

    uint64_t m_carrierPackets = 0;
    uint64_t m_frames = 0;
    uint64_t m_payloadBytes = 0;
    uint64_t m_crcErrors = 0;
    uint64_t m_sequenceErrors = 0;
    uint64_t m_lengthErrors = 0;
};
