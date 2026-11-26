#pragma once

// Decoders for generic streams (MATYPE TS/GS = 00, 01, 10) that carry something
// other than standard GSE, and the auto detection that picks one per input stream.
//
// The variants follow "Don't Look Up: Ubiquitous Data Exposure in Open Satellite
// Communications" (UCSD) and its parser, github.com/ucsdsysnet/dontlookup, which
// found them on geostationary carriers:
//   * GSE whose GSE_Length includes the 2-byte header, and/or whose Frag_ID is a
//     6-bit ID plus a 2-bit counter
//   * MPEG-TS packets placed in a generic stream, as-is or with the sync byte
//     replaced by the CRC-8 of the previous packet (DVB-S compatible framing on a
//     stream that is not signalled as TS; e.g. Newtec modulators)
//   * IPv4 packets written straight into the data field, with no encapsulation
//   * the same, with every 16-bit word byte-swapped
//   * BBFrames with a CRC-32 appended to the data field

#include "Gse.h"
#include "export.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace tbs {
namespace bb {

enum class GenericDecoder {
    Auto = 0,         // probe all of the below on the first frames, keep the best
    Gse,              // ETSI TS 102 606-1
    GseLen2,          // GSE, GSE_Length includes the 2-byte header
    GseSplitFragId,   // GSE, 6-bit Frag ID + 2-bit counter
    GseLen2SplitFragId,
    Ts,               // 188-byte TS packets with sync byte 0x47
    CrcTs,            // TS packets whose sync byte carries the CRC-8 of the previous packet
    RawIp,            // IPv4 packets directly in the data field
    RawIpSwapped,     // the same, 16-bit words byte-swapped
    None,             // count only
};

constexpr int kGenericDecoderCount = static_cast<int>(GenericDecoder::None) + 1;

TBSBBPROC_API const char *genericDecoderName(GenericDecoder d); // human readable
TBSBBPROC_API const char *genericDecoderId(GenericDecoder d);   // "auto", "gse", "gse-len2", ...
TBSBBPROC_API bool parseGenericDecoder(const std::string &id, GenericDecoder *out);
TBSBBPROC_API bool isGseDecoder(GenericDecoder d);
TBSBBPROC_API GseDialect gseDialectFor(GenericDecoder d);

// IPv4 header sanity + header checksum (no options parsing beyond IHL)
TBSBBPROC_API bool validIpv4Header(const uint8_t *p, size_t avail);

// Rearranges a data field as some encoders do: swaps the bytes of every 16-bit
// word and drops the first two bytes (a 2-byte prefix).
TBSBBPROC_API void byteSwap16(const uint8_t *in, size_t len, std::vector<uint8_t> &out);

struct TBSBBPROC_API RawIpStats
{
    uint64_t dataFields = 0;
    uint64_t packets = 0;     // IPv4 packets with a valid header checksum
    uint64_t bytes = 0;
    uint64_t skippedBytes = 0; // bytes between packets
};

// Blind IPv4 search over the concatenated data fields (packets may span BBFrames).
class TBSBBPROC_API RawIpExtractor
{
public:
    using PduCallback = std::function<void(const GsePdu &)>;
    explicit RawIpExtractor(bool byteSwapped = false) : m_swapped(byteSwapped) {}
    void pushDataField(const uint8_t *df, size_t len, const PduCallback &cb);
    void reset();
    const RawIpStats &stats() const { return m_stats; }

private:
    bool m_swapped;
    RawIpStats m_stats;
    std::vector<uint8_t> m_buf;
    std::vector<uint8_t> m_tmp;
};

struct TBSBBPROC_API TsExtractStats
{
    uint64_t dataFields = 0;
    uint64_t packets = 0;     // packets emitted
    uint64_t verified = 0;    // with a matching sync byte / CRC-8
    uint64_t crcErrors = 0;   // CRC-TS: CRC-8 mismatch while locked
    uint64_t syncLosses = 0;
    uint64_t skippedBytes = 0;
};

// Finds 188-byte TS packets in the concatenated data fields of a generic stream.
// Plain mode locks on 0x47 every 188 bytes. CRC mode locks where byte 0 of each
// packet equals the CRC-8 (EN 302 307 polynomial) of the previous packet's
// 187 bytes; SYNCD is not trusted. Packets come out with the sync byte restored.
class TBSBBPROC_API GenericTsExtractor
{
public:
    using PacketCallback = std::function<void(const uint8_t *pkt188)>;
    explicit GenericTsExtractor(bool crcMode = false) : m_crc(crcMode) {}
    void pushDataField(const uint8_t *df, size_t len, const PacketCallback &cb);
    void reset();
    const TsExtractStats &stats() const { return m_stats; }

private:
    bool aligned(size_t pos) const;
    bool findLock(size_t &pos); // advances pos to a locked position, false: need more data
    bool m_crc;
    bool m_locked = false;
    int m_misses = 0;
    uint8_t m_prevCrc = 0;
    TsExtractStats m_stats;
    std::vector<uint8_t> m_buf;
};

struct TBSBBPROC_API DecoderScore
{
    GenericDecoder decoder = GenericDecoder::None;
    double coverage = 0.0; // share of the probed data (trailing zeros excluded) explained by valid units
    uint64_t units = 0;    // valid IP packets / TS packets found
};

// Auto detection: runs every candidate on the same data fields and ranks them.
class TBSBBPROC_API GenericDecoderProbe
{
public:
    GenericDecoderProbe();
    ~GenericDecoderProbe();
    GenericDecoderProbe(const GenericDecoderProbe &) = delete;
    GenericDecoderProbe &operator=(const GenericDecoderProbe &) = delete;

    void pushDataField(const uint8_t *df, size_t len, bool hem, uint16_t syncdBits);
    uint64_t dataFields() const;
    // Best first. Ties (within kTieMargin) keep the earlier, more standard decoder.
    std::vector<DecoderScore> scores() const;
    // The decoder to use, or GenericDecoder::None if nothing explains the data well enough
    GenericDecoder best() const;
    // True once a decoder explains nearly all of the data: no need to wait for more
    bool conclusive() const;

    static constexpr double kMinCoverage = 0.25;
    static constexpr double kTieMargin = 0.05;
    static constexpr double kConclusiveCoverage = 0.9;
    static constexpr uint64_t kConclusiveFields = 4;

private:
    struct Impl;
    std::unique_ptr<Impl> d;
};

} // namespace bb
} // namespace tbs
