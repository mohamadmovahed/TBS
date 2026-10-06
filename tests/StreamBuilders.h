#pragma once

// Helpers that synthesise the data formats the receiver consumes. Shared by the
// unit tests and the tbs-make-test-stream tool.

#include "tbsdvb/BbHeader.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <vector>

namespace builders {

using tbs::BbHeader;
using Bytes = std::vector<uint8_t>;

inline Bytes makeTsPacket(uint16_t pid, uint8_t cc, uint8_t fill, bool pusi = false)
{
    Bytes p(188, fill);
    p[0] = 0x47;
    p[1] = static_cast<uint8_t>((pusi ? 0x40 : 0x00) | ((pid >> 8) & 0x1F));
    p[2] = static_cast<uint8_t>(pid & 0xFF);
    p[3] = static_cast<uint8_t>(0x10 | (cc & 0x0F));
    return p;
}

// Builds a BBHeader + data field. In HEM the CRC is XORed with 1.
inline Bytes makeBbFrame(uint8_t matype1, uint8_t matype2, uint16_t uplBits, const Bytes &dataField,
                         uint16_t syncdBits, uint8_t sync = 0x47, bool hem = false)
{
    Bytes f(BbHeader::kSize);
    const uint16_t dflBits = static_cast<uint16_t>(dataField.size() * 8);
    f[0] = matype1;
    f[1] = matype2;
    f[2] = static_cast<uint8_t>(uplBits >> 8);
    f[3] = static_cast<uint8_t>(uplBits);
    f[4] = static_cast<uint8_t>(dflBits >> 8);
    f[5] = static_cast<uint8_t>(dflBits);
    f[6] = sync;
    f[7] = static_cast<uint8_t>(syncdBits >> 8);
    f[8] = static_cast<uint8_t>(syncdBits);
    f[9] = static_cast<uint8_t>(BbHeader::crc8(f.data(), 9) ^ (hem ? 1 : 0));
    f.insert(f.end(), dataField.begin(), dataField.end());
    return f;
}

// Slices a TS packet sequence into BBFrames (TS/GS = 11, SIS, CCM).
// Normal mode: sync byte replaced by the CRC-8 of the previous packet.
// HEM: sync byte removed.
inline std::vector<Bytes> tsToBbFrames(const std::vector<Bytes> &packets, size_t dataFieldBytes, bool hem = false)
{
    Bytes stream;
    std::vector<size_t> upStarts;
    uint8_t prevCrc = 0;
    for (const Bytes &p : packets) {
        upStarts.push_back(stream.size());
        if (hem) {
            stream.insert(stream.end(), p.begin() + 1, p.end());
        } else {
            stream.push_back(prevCrc);
            stream.insert(stream.end(), p.begin() + 1, p.end());
            prevCrc = BbHeader::crc8(p.data() + 1, 187);
        }
    }

    std::vector<Bytes> frames;
    const uint8_t matype1 = 0xF0; // TS, SIS, CCM, no ISSY, no NPD, RO 0.35
    size_t upIdx = 0;
    for (size_t off = 0; off < stream.size(); off += dataFieldBytes) {
        const size_t end = std::min(stream.size(), off + dataFieldBytes);
        Bytes df(stream.begin() + static_cast<std::ptrdiff_t>(off), stream.begin() + static_cast<std::ptrdiff_t>(end));
        while (upIdx < upStarts.size() && upStarts[upIdx] < off)
            ++upIdx;
        uint16_t syncd = 0xFFFF;
        if (upIdx < upStarts.size() && upStarts[upIdx] < end)
            syncd = static_cast<uint16_t>((upStarts[upIdx] - off) * 8);
        frames.push_back(makeBbFrame(matype1, 0, hem ? 0 : 188 * 8, df, syncd, 0x47, hem));
    }
    return frames;
}

// Wraps one BBFrame into the STiD135 pseudo-TS format (PID 0x010E).
inline std::vector<Bytes> bbFrameToPseudoTs(const Bytes &frame, uint8_t &cc, uint16_t pid = 0x010E)
{
    std::vector<Bytes> out;
    size_t pos = 0;
    uint8_t counter = 0;
    while (pos < frame.size()) {
        Bytes p(188, 0xFF);
        p[0] = 0x47;
        p[1] = static_cast<uint8_t>(0x40 | ((pid >> 8) & 0x1F));
        p[2] = static_cast<uint8_t>(pid & 0xFF);
        p[3] = static_cast<uint8_t>(0x10 | (cc++ & 0x0F));
        p[4] = 0x00;
        p[5] = 0x80;
        p[6] = 0x00;
        const size_t take = std::min<size_t>(179, frame.size() - pos);
        p[7] = static_cast<uint8_t>(take + 1);
        p[8] = counter == 0 ? 0xB8 : counter;
        std::memcpy(p.data() + 9, frame.data() + pos, take);
        pos += take;
        ++counter;
        out.push_back(std::move(p));
    }
    return out;
}

} // namespace builders
