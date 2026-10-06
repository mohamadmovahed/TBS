#pragma once

#include "export.h"

#include <cstddef>
#include <cstdint>
#include <string>

namespace tbs {

// DVB-S2 / S2X baseband header (EN 302 307-1, 5.1.6).
struct TBSDVB_API BbHeader
{
    static constexpr size_t kSize = 10;

    enum class StreamType { GenericPacketized = 0, GenericContinuous = 1, GseHem = 2, Transport = 3 };
    enum class Mode { Normal, HighEfficiency, Invalid };

    uint8_t matype1 = 0;
    uint8_t matype2 = 0;
    uint16_t upl = 0;   // user packet length in bits
    uint16_t dfl = 0;   // data field length in bits
    uint8_t sync = 0;
    uint16_t syncd = 0; // distance in bits to the first user packet in the data field
    uint8_t crc = 0;
    Mode mode = Mode::Invalid; // from the CRC-8 (XOR 1 in HEM)

    StreamType streamType() const { return static_cast<StreamType>(matype1 >> 6); }
    bool singleInputStream() const { return (matype1 & 0x20) != 0; }
    bool ccm() const { return (matype1 & 0x10) != 0; }
    bool issyi() const { return (matype1 & 0x08) != 0; }
    bool npd() const { return (matype1 & 0x04) != 0; }
    int rolloffBits() const { return matype1 & 0x03; }
    int isi() const { return singleInputStream() ? -1 : matype2; }
    bool valid() const { return mode != Mode::Invalid; }
    size_t dataFieldBytes() const { return (dfl + 7u) / 8u; }

    // Parses 10 bytes. Returns false if the CRC matches neither NM nor HEM.
    bool parse(const uint8_t *p);

    std::string streamTypeName() const;
    std::string rolloffName() const;
    std::string modeName() const;
    std::string summary() const;

    static uint8_t crc8(const uint8_t *p, size_t len);
};

} // namespace tbs
