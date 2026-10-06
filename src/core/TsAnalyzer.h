#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <utility>
#include <vector>

// Re-aligns an arbitrary byte stream on 188-byte TS packets and keeps per-PID statistics.
class TsAnalyzer
{
public:
    static constexpr size_t kPacketSize = 188;
    static constexpr uint8_t kSyncByte = 0x47;

    // Called with runs of consecutive, aligned packets.
    using PacketCallback = std::function<void(const uint8_t *packets, size_t count)>;

    TsAnalyzer();

    void push(const uint8_t *data, size_t len, const PacketCallback &cb);
    void reset();

    uint64_t packets() const { return m_packets; }
    uint64_t syncLosses() const { return m_syncLosses; }
    uint64_t ccErrors() const { return m_ccErrors; }
    uint64_t teiErrors() const { return m_teiErrors; }
    uint64_t pidCount(uint16_t pid) const { return m_pidCounts[pid & 0x1FFF]; }
    std::vector<std::pair<uint16_t, uint64_t>> pidSnapshot() const;

    static uint16_t pidOf(const uint8_t *pkt) { return static_cast<uint16_t>(((pkt[1] & 0x1F) << 8) | pkt[2]); }

private:
    void inspect(const uint8_t *pkt);

    std::vector<uint8_t> m_buf;
    bool m_synced = false;
    uint64_t m_packets = 0;
    uint64_t m_syncLosses = 0;
    uint64_t m_ccErrors = 0;
    uint64_t m_teiErrors = 0;
    std::array<uint64_t, 8192> m_pidCounts{};
    std::array<uint8_t, 8192> m_lastCc{};   // 0xFF = not seen yet
};
