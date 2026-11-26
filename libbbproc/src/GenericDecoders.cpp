#include "tbsbbproc/GenericDecoders.h"

#include <algorithm>
#include <array>
#include <cstring>

namespace tbs {
namespace bb {

namespace {

inline uint16_t u16(const uint8_t *p)
{
    return static_cast<uint16_t>((p[0] << 8) | p[1]);
}

// CRC-8 of EN 302 307 (g(X) = X^8 + X^7 + X^6 + X^4 + X^2 + 1), table driven
struct Crc8Table
{
    std::array<uint8_t, 256> t{};
    // CRC of byte v followed by 187 zero bytes: removes v from a 187-byte sliding window
    std::array<uint8_t, 256> out{};
    Crc8Table()
    {
        for (int i = 0; i < 256; ++i) {
            uint8_t c = static_cast<uint8_t>(i);
            for (int b = 0; b < 8; ++b)
                c = (c & 0x80) ? static_cast<uint8_t>((c << 1) ^ 0xD5) : static_cast<uint8_t>(c << 1);
            t[static_cast<size_t>(i)] = c;
        }
        for (int v = 0; v < 256; ++v) {
            uint8_t c = t[static_cast<size_t>(v)];
            for (int k = 0; k < 187; ++k)
                c = t[c];
            out[static_cast<size_t>(v)] = c;
        }
    }
};

const Crc8Table &crcTable()
{
    static const Crc8Table table;
    return table;
}

uint8_t crc8(const uint8_t *p, size_t n)
{
    const Crc8Table &table = crcTable();
    uint8_t c = 0;
    for (size_t i = 0; i < n; ++i)
        c = table.t[c ^ p[i]];
    return c;
}

size_t trailingZeros(const uint8_t *p, size_t n)
{
    size_t z = 0;
    while (z < n && p[n - 1 - z] == 0)
        ++z;
    return z;
}

// Bytes of a PDU that look like valid network layer data (used for scoring only)
size_t plausiblePduBytes(const GsePdu &p)
{
    if (p.bridged)
        return p.len >= 14 ? p.len : 0;
    switch (p.protocolType) {
    case 0x0800:
        return validIpv4Header(p.data, p.len) && u16(p.data + 2) <= p.len ? p.len : 0;
    case 0x86DD:
        return p.len >= 40 && (p.data[0] >> 4) == 6 && 40u + u16(p.data + 4) == p.len ? p.len : 0;
    case 0x0806:
        return p.len >= 28 && u16(p.data) == 1 ? p.len : 0;
    default:
        return 0;
    }
}

} // namespace

const char *genericDecoderName(GenericDecoder d)
{
    switch (d) {
    case GenericDecoder::Auto:
        return "Auto detect";
    case GenericDecoder::Gse:
        return "GSE (TS 102 606)";
    case GenericDecoder::GseLen2:
        return "GSE, length includes header (non-standard)";
    case GenericDecoder::GseSplitFragId:
        return "GSE, 6-bit Frag ID + counter (non-standard)";
    case GenericDecoder::GseLen2SplitFragId:
        return "GSE, length includes header, 6-bit Frag ID (non-standard)";
    case GenericDecoder::Ts:
        return "TS packets in a generic stream";
    case GenericDecoder::CrcTs:
        return "TS with CRC-8 in the sync byte, in a generic stream";
    case GenericDecoder::RawIp:
        return "Raw IPv4 in the data field";
    case GenericDecoder::RawIpSwapped:
        return "Raw IPv4, 16-bit byte-swapped";
    case GenericDecoder::None:
        return "None";
    }
    return "";
}

const char *genericDecoderId(GenericDecoder d)
{
    switch (d) {
    case GenericDecoder::Auto:
        return "auto";
    case GenericDecoder::Gse:
        return "gse";
    case GenericDecoder::GseLen2:
        return "gse-len2";
    case GenericDecoder::GseSplitFragId:
        return "gse-split";
    case GenericDecoder::GseLen2SplitFragId:
        return "gse-len2-split";
    case GenericDecoder::Ts:
        return "ts";
    case GenericDecoder::CrcTs:
        return "crc-ts";
    case GenericDecoder::RawIp:
        return "ip";
    case GenericDecoder::RawIpSwapped:
        return "ip-swapped";
    case GenericDecoder::None:
        return "none";
    }
    return "";
}

bool parseGenericDecoder(const std::string &id, GenericDecoder *out)
{
    for (int i = 0; i < kGenericDecoderCount; ++i) {
        const auto d = static_cast<GenericDecoder>(i);
        if (id == genericDecoderId(d)) {
            if (out)
                *out = d;
            return true;
        }
    }
    return false;
}

bool isGseDecoder(GenericDecoder d)
{
    return d == GenericDecoder::Gse || d == GenericDecoder::GseLen2 || d == GenericDecoder::GseSplitFragId
        || d == GenericDecoder::GseLen2SplitFragId;
}

GseDialect gseDialectFor(GenericDecoder d)
{
    GseDialect g;
    g.lengthIncludesHeader = d == GenericDecoder::GseLen2 || d == GenericDecoder::GseLen2SplitFragId;
    g.splitFragId = d == GenericDecoder::GseSplitFragId || d == GenericDecoder::GseLen2SplitFragId;
    return g;
}

bool validIpv4Header(const uint8_t *p, size_t avail)
{
    if (avail < 20 || (p[0] >> 4) != 4)
        return false;
    const size_t ihl = (p[0] & 0x0F) * 4u;
    if (ihl < 20 || ihl > avail)
        return false;
    if (u16(p + 2) < ihl || (p[6] & 0x80) || p[8] == 0)
        return false;
    uint32_t sum = 0;
    for (size_t i = 0; i < ihl; i += 2)
        sum += u16(p + i);
    while (sum >> 16)
        sum = (sum & 0xFFFF) + (sum >> 16);
    return sum == 0xFFFF;
}

void byteSwap16(const uint8_t *in, size_t len, std::vector<uint8_t> &out)
{
    out.clear();
    if (len <= 2)
        return;
    out.reserve(len - 2);
    // Swap pairs over the whole field, then drop the first two (swapped) bytes
    for (size_t i = 2; i < len; ++i) {
        const size_t pairStart = i & ~static_cast<size_t>(1);
        const size_t src = (i & 1) ? pairStart : pairStart + 1;
        out.push_back(src < len ? in[src] : in[i]);
    }
}

// ---------------------------------------------------------------------------
// Raw IP

void RawIpExtractor::reset()
{
    m_stats = RawIpStats();
    m_buf.clear();
}

void RawIpExtractor::pushDataField(const uint8_t *df, size_t len, const PduCallback &cb)
{
    ++m_stats.dataFields;
    if (m_swapped) {
        byteSwap16(df, len, m_tmp);
        m_buf.insert(m_buf.end(), m_tmp.begin(), m_tmp.end());
    } else {
        m_buf.insert(m_buf.end(), df, df + len);
    }

    size_t pos = 0;
    while (pos + 20 <= m_buf.size()) {
        const uint8_t *p = m_buf.data() + pos;
        if ((p[0] >> 4) == 4 && validIpv4Header(p, m_buf.size() - pos)) {
            const size_t total = u16(p + 2);
            if (pos + total > m_buf.size())
                break; // rest of the packet is in the next data field
            GsePdu pdu;
            pdu.protocolType = 0x0800;
            pdu.labelType = 2;
            pdu.data = p;
            pdu.len = total;
            ++m_stats.packets;
            m_stats.bytes += total;
            if (cb)
                cb(pdu);
            pos += total;
            continue;
        }
        ++pos;
        ++m_stats.skippedBytes;
    }
    m_buf.erase(m_buf.begin(), m_buf.begin() + static_cast<std::ptrdiff_t>(pos));
}

// ---------------------------------------------------------------------------
// TS in a generic stream

void GenericTsExtractor::reset()
{
    m_stats = TsExtractStats();
    m_buf.clear();
    m_locked = false;
    m_misses = 0;
}

bool GenericTsExtractor::aligned(size_t pos) const
{
    const uint8_t *b = m_buf.data() + pos;
    if (!m_crc)
        return b[0] == 0x47 && b[188] == 0x47 && b[376] == 0x47;
    return b[188] == crc8(b + 1, 187) && b[376] == crc8(b + 189, 187);
}

bool GenericTsExtractor::findLock(size_t &pos)
{
    const size_t n = m_buf.size();
    if (pos + 377 > n)
        return false;
    if (!m_crc) {
        for (; pos + 377 <= n; ++pos, ++m_stats.skippedBytes)
            if (aligned(pos))
                return true;
        return false;
    }
    // Rolling CRC-8 over the 187 bytes after pos: O(1) per candidate offset
    const Crc8Table &tab = crcTable();
    const uint8_t *b = m_buf.data();
    uint8_t c = crc8(b + pos + 1, 187);
    while (true) {
        if (b[pos + 188] == c && b[pos + 376] == crc8(b + pos + 189, 187))
            return true;
        const bool more = pos + 1 + 377 <= n;
        if (more)
            c = static_cast<uint8_t>(tab.t[c ^ b[pos + 188]] ^ tab.out[b[pos + 1]]);
        ++pos;
        ++m_stats.skippedBytes;
        if (!more)
            return false;
    }
}

void GenericTsExtractor::pushDataField(const uint8_t *df, size_t len, const PacketCallback &cb)
{
    ++m_stats.dataFields;
    m_buf.insert(m_buf.end(), df, df + len);

    size_t pos = 0;
    uint8_t pkt[188];
    bool first = false;
    while (true) {
        if (!m_locked) {
            if (!findLock(pos))
                break;
            m_locked = true;
            m_misses = 0;
            first = true;
        }
        if (pos + 188 > m_buf.size())
            break;
        const uint8_t *up = m_buf.data() + pos;
        // CRC mode: byte 0 is the CRC-8 of the previous packet. The first packet
        // after locking was verified by the alignment check itself.
        bool ok = first;
        if (!first)
            ok = m_crc ? up[0] == m_prevCrc : up[0] == 0x47;
        first = false;
        if (m_crc)
            m_prevCrc = crc8(up + 1, 187);
        if (ok) {
            m_misses = 0;
            ++m_stats.verified;
        } else {
            if (m_crc)
                ++m_stats.crcErrors;
            if (++m_misses >= 3) {
                m_locked = false;
                ++m_stats.syncLosses;
                ++pos;
                continue;
            }
        }
        pkt[0] = 0x47;
        std::memcpy(pkt + 1, up + 1, 187);
        ++m_stats.packets;
        if (cb)
            cb(pkt);
        pos += 188;
    }
    m_buf.erase(m_buf.begin(), m_buf.begin() + static_cast<std::ptrdiff_t>(pos));
}

// ---------------------------------------------------------------------------
// Auto detection

struct GenericDecoderProbe::Impl
{
    static constexpr int kGse = 4;
    GseDecapsulator gse[kGse];
    uint64_t gseBytes[kGse] = {};
    uint64_t gseUnits[kGse] = {};
    GenericTsExtractor ts{false};
    GenericTsExtractor crcTs{true};
    RawIpExtractor ip{false};
    RawIpExtractor ipSwapped{true};
    uint64_t fields = 0;
    uint64_t effectiveBytes = 0;
};

GenericDecoderProbe::GenericDecoderProbe()
    : d(std::make_unique<Impl>())
{
    const GenericDecoder g[Impl::kGse] = {GenericDecoder::Gse, GenericDecoder::GseLen2, GenericDecoder::GseSplitFragId,
                                          GenericDecoder::GseLen2SplitFragId};
    for (int i = 0; i < Impl::kGse; ++i)
        d->gse[i].setDialect(gseDialectFor(g[i]));
}

GenericDecoderProbe::~GenericDecoderProbe() = default;

uint64_t GenericDecoderProbe::dataFields() const
{
    return d->fields;
}

void GenericDecoderProbe::pushDataField(const uint8_t *df, size_t len, bool hem, uint16_t syncdBits)
{
    ++d->fields;
    d->effectiveBytes += len - trailingZeros(df, len);
    for (int i = 0; i < Impl::kGse; ++i) {
        d->gse[i].pushDataField(df, len, hem, syncdBits, [this, i](const GsePdu &pdu) {
            const size_t n = plausiblePduBytes(pdu);
            if (n) {
                d->gseBytes[i] += n;
                ++d->gseUnits[i];
            }
        });
    }
    d->ts.pushDataField(df, len, nullptr);
    d->crcTs.pushDataField(df, len, nullptr);
    d->ip.pushDataField(df, len, nullptr);
    d->ipSwapped.pushDataField(df, len, nullptr);
}

std::vector<DecoderScore> GenericDecoderProbe::scores() const
{
    std::vector<DecoderScore> v;
    const double denom = d->effectiveBytes ? static_cast<double>(d->effectiveBytes) : 1.0;
    auto add = [&](GenericDecoder dec, uint64_t bytes, uint64_t units) {
        DecoderScore s;
        s.decoder = dec;
        s.coverage = std::min(1.0, bytes / denom);
        s.units = units;
        v.push_back(s);
    };
    const GenericDecoder g[Impl::kGse] = {GenericDecoder::Gse, GenericDecoder::GseLen2, GenericDecoder::GseSplitFragId,
                                          GenericDecoder::GseLen2SplitFragId};
    for (int i = 0; i < Impl::kGse; ++i)
        add(g[i], d->gseBytes[i], d->gseUnits[i]);
    add(GenericDecoder::Ts, d->ts.stats().verified * 188, d->ts.stats().verified);
    add(GenericDecoder::CrcTs, d->crcTs.stats().verified * 188, d->crcTs.stats().verified);
    add(GenericDecoder::RawIp, d->ip.stats().bytes, d->ip.stats().packets);
    add(GenericDecoder::RawIpSwapped, d->ipSwapped.stats().bytes, d->ipSwapped.stats().packets);
    std::stable_sort(v.begin(), v.end(), [](const DecoderScore &a, const DecoderScore &b) { return a.coverage > b.coverage; });
    return v;
}

namespace {
// scores() is sorted by coverage; the tie rule looks at them in candidate (enum) order
DecoderScore pickBest(std::vector<DecoderScore> v)
{
    std::sort(v.begin(), v.end(),
              [](const DecoderScore &a, const DecoderScore &b) { return static_cast<int>(a.decoder) < static_cast<int>(b.decoder); });
    DecoderScore best;
    bool have = false;
    for (const DecoderScore &s : v)
        if (!have || s.coverage > best.coverage + GenericDecoderProbe::kTieMargin) {
            best = s;
            have = true;
        }
    return best;
}
} // namespace

GenericDecoder GenericDecoderProbe::best() const
{
    const DecoderScore b = pickBest(scores());
    return b.coverage < kMinCoverage ? GenericDecoder::None : b.decoder;
}

bool GenericDecoderProbe::conclusive() const
{
    return d->fields >= kConclusiveFields && pickBest(scores()).coverage >= kConclusiveCoverage;
}

} // namespace bb
} // namespace tbs
