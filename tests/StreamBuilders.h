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

// ---------------------------------------------------------------------------
// GSE / IP builders
// ---------------------------------------------------------------------------

inline uint32_t crc32Mpeg(const uint8_t *p, size_t n)
{
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; ++i) {
        crc ^= static_cast<uint32_t>(p[i]) << 24;
        for (int b = 0; b < 8; ++b)
            crc = (crc & 0x80000000u) ? (crc << 1) ^ 0x04C11DB7u : (crc << 1);
    }
    return crc;
}

inline void put16(Bytes &b, uint16_t v)
{
    b.push_back(static_cast<uint8_t>(v >> 8));
    b.push_back(static_cast<uint8_t>(v));
}

inline Bytes makeIpv4(uint8_t proto, const uint8_t src[4], const uint8_t dst[4], uint16_t sport, uint16_t dport,
                      size_t payload, uint8_t seed)
{
    const size_t l4 = proto == 6 ? 20 : 8;
    const size_t total = 20 + l4 + payload;
    Bytes p;
    p.push_back(0x45);
    p.push_back(0);
    put16(p, static_cast<uint16_t>(total));
    put16(p, seed); // identification
    put16(p, 0x4000); // DF
    p.push_back(64);
    p.push_back(proto);
    put16(p, 0); // checksum, filled below
    p.insert(p.end(), src, src + 4);
    p.insert(p.end(), dst, dst + 4);
    uint32_t sum = 0;
    for (size_t i = 0; i < 20; i += 2)
        sum += (p[i] << 8) | p[i + 1];
    while (sum >> 16)
        sum = (sum & 0xFFFF) + (sum >> 16);
    p[10] = static_cast<uint8_t>(~sum >> 8);
    p[11] = static_cast<uint8_t>(~sum);
    put16(p, sport);
    put16(p, dport);
    if (proto == 6) {
        for (int i = 0; i < 8; ++i)
            p.push_back(0);
        p.push_back(0x50);
        p.push_back(0x18);
        put16(p, 0xFFFF);
        put16(p, 0);
        put16(p, 0);
    } else {
        put16(p, static_cast<uint16_t>(8 + payload));
        put16(p, 0);
    }
    for (size_t i = 0; i < payload; ++i)
        p.push_back(static_cast<uint8_t>(seed + i));
    return p;
}

inline Bytes makeIpv6Udp(const uint8_t src[16], const uint8_t dst[16], uint16_t sport, uint16_t dport, size_t payload)
{
    Bytes p = {0x60, 0, 0, 0};
    put16(p, static_cast<uint16_t>(8 + payload));
    p.push_back(17);
    p.push_back(64);
    p.insert(p.end(), src, src + 16);
    p.insert(p.end(), dst, dst + 16);
    put16(p, sport);
    put16(p, dport);
    put16(p, static_cast<uint16_t>(8 + payload));
    put16(p, 0);
    for (size_t i = 0; i < payload; ++i)
        p.push_back(static_cast<uint8_t>(i));
    return p;
}

// GSE packets (TS 102 606) for one PDU. If the encapsulated PDU exceeds maxPacket
// bytes it is fragmented (first / middle / last fragment with CRC-32).
inline std::vector<Bytes> gseEncapsulate(const Bytes &pdu, uint16_t protocolType, uint8_t labelType, const uint8_t *label,
                                         size_t maxPacket, uint8_t fragId)
{
    const size_t lb = labelType == 0 ? 6 : labelType == 1 ? 3 : 0;
    Bytes body; // Protocol_Type, Label, PDU
    put16(body, protocolType);
    if (lb)
        body.insert(body.end(), label, label + lb);
    body.insert(body.end(), pdu.begin(), pdu.end());

    auto header = [](bool s, bool e, uint8_t lt, size_t len) {
        Bytes h(2);
        h[0] = static_cast<uint8_t>((s ? 0x80 : 0) | (e ? 0x40 : 0) | ((lt & 3) << 4) | ((len >> 8) & 0x0F));
        h[1] = static_cast<uint8_t>(len);
        return h;
    };
    std::vector<Bytes> out;
    if (2 + body.size() <= maxPacket) {
        Bytes g = header(true, true, labelType, body.size());
        g.insert(g.end(), body.begin(), body.end());
        out.push_back(g);
        return out;
    }
    // Fragmented: CRC over Total_Length + body
    Bytes crcInput;
    put16(crcInput, static_cast<uint16_t>(body.size()));
    crcInput.insert(crcInput.end(), body.begin(), body.end());
    const uint32_t crc = crc32Mpeg(crcInput.data(), crcInput.size());
    Bytes tail = body;
    tail.push_back(static_cast<uint8_t>(crc >> 24));
    tail.push_back(static_cast<uint8_t>(crc >> 16));
    tail.push_back(static_cast<uint8_t>(crc >> 8));
    tail.push_back(static_cast<uint8_t>(crc));

    size_t pos = 0;
    bool first = true;
    while (pos < tail.size()) {
        const size_t overhead = 2 + 1 + (first ? 2 : 0);
        size_t take = std::min(maxPacket - overhead, tail.size() - pos);
        const bool last = pos + take == tail.size();
        // The CRC must not be split from the end of the last fragment
        if (!last && tail.size() - (pos + take) < 4)
            take = tail.size() - pos - 4;
        // LT is only meaningful in the first fragment; later ones use 11 because
        // S=0, E=0, LT=00 would read as padding
        Bytes g = header(first, last, first ? labelType : 3, overhead - 2 + take);
        g.push_back(fragId);
        if (first)
            put16(g, static_cast<uint16_t>(body.size()));
        g.insert(g.end(), tail.begin() + static_cast<std::ptrdiff_t>(pos), tail.begin() + static_cast<std::ptrdiff_t>(pos + take));
        out.push_back(g);
        pos += take;
        first = false;
    }
    return out;
}

// Packs GSE packets into BBFrames.
//   normal mode (TS/GS = 01): whole packets per data field, zero padding at the end
//   HEM (TS/GS = 10): packets run across data fields, SYNCD = first packet start
inline std::vector<Bytes> gseToBbFrames(const std::vector<Bytes> &packets, size_t dataFieldBytes, bool hem, int isi = -1)
{
    std::vector<Bytes> frames;
    const uint8_t sis = isi < 0 ? 0x20 : 0x00;
    const uint8_t matype1 = static_cast<uint8_t>((hem ? 0x80 : 0x40) | sis | 0x10);
    const uint8_t matype2 = static_cast<uint8_t>(isi < 0 ? 0 : isi);
    if (!hem) {
        Bytes df;
        for (const Bytes &g : packets) {
            if (df.size() + g.size() > dataFieldBytes) {
                df.resize(dataFieldBytes, 0);
                frames.push_back(makeBbFrame(matype1, matype2, 0, df, 0, 0, false));
                df.clear();
            }
            df.insert(df.end(), g.begin(), g.end());
        }
        if (!df.empty()) {
            df.resize(dataFieldBytes, 0);
            frames.push_back(makeBbFrame(matype1, matype2, 0, df, 0, 0, false));
        }
        return frames;
    }
    Bytes stream;
    std::vector<size_t> starts;
    for (const Bytes &g : packets) {
        starts.push_back(stream.size());
        stream.insert(stream.end(), g.begin(), g.end());
    }
    size_t si = 0;
    for (size_t off = 0; off < stream.size(); off += dataFieldBytes) {
        const size_t end = std::min(stream.size(), off + dataFieldBytes);
        Bytes df(stream.begin() + static_cast<std::ptrdiff_t>(off), stream.begin() + static_cast<std::ptrdiff_t>(end));
        while (si < starts.size() && starts[si] < off)
            ++si;
        uint16_t syncd = 0xFFFF;
        if (si < starts.size() && starts[si] < end)
            syncd = static_cast<uint16_t>((starts[si] - off) * 8);
        df.resize(dataFieldBytes, 0);
        frames.push_back(makeBbFrame(matype1, matype2, 0, df, syncd, 0, true));
    }
    return frames;
}

// ---------------------------------------------------------------------------
// PSI / SI section builders
// ---------------------------------------------------------------------------

// Long-form section: table_id, length, ext, version, section numbers, body, CRC
inline Bytes makeSection(uint8_t tableId, uint16_t ext, uint8_t version, const Bytes &body, uint8_t sectionNumber = 0,
                         uint8_t lastSection = 0)
{
    Bytes s;
    s.push_back(tableId);
    const size_t len = 5 + body.size() + 4;
    s.push_back(static_cast<uint8_t>(0xB0 | ((len >> 8) & 0x0F)));
    s.push_back(static_cast<uint8_t>(len));
    put16(s, ext);
    s.push_back(static_cast<uint8_t>(0xC1 | ((version & 0x1F) << 1)));
    s.push_back(sectionNumber);
    s.push_back(lastSection);
    s.insert(s.end(), body.begin(), body.end());
    const uint32_t crc = crc32Mpeg(s.data(), s.size());
    for (int i = 3; i >= 0; --i)
        s.push_back(static_cast<uint8_t>(crc >> (8 * i)));
    return s;
}

// Splits a section into TS packets (PUSI + pointer field, 0xFF stuffing)
inline std::vector<Bytes> sectionToTs(const Bytes &section, uint16_t pid, uint8_t &cc)
{
    std::vector<Bytes> out;
    size_t pos = 0;
    bool first = true;
    while (pos < section.size()) {
        Bytes p(188, 0xFF);
        p[0] = 0x47;
        p[1] = static_cast<uint8_t>((first ? 0x40 : 0) | ((pid >> 8) & 0x1F));
        p[2] = static_cast<uint8_t>(pid);
        p[3] = static_cast<uint8_t>(0x10 | (cc++ & 0x0F));
        size_t off = 4;
        if (first)
            p[off++] = 0; // pointer field
        const size_t take = std::min(188 - off, section.size() - pos);
        std::memcpy(p.data() + off, section.data() + pos, take);
        pos += take;
        first = false;
        out.push_back(p);
    }
    return out;
}

// MPE datagram section (EN 301 192), table 0x3E, with CRC-32
inline Bytes makeMpeSection(const uint8_t mac[6], const Bytes &datagram, bool llcSnap, uint16_t etherType = 0x0800,
                            bool scrambled = false)
{
    Bytes body;
    if (llcSnap) {
        body = {0xAA, 0xAA, 0x03, 0x00, 0x00, 0x00};
        put16(body, etherType);
    }
    body.insert(body.end(), datagram.begin(), datagram.end());
    Bytes s;
    s.push_back(0x3E);
    const size_t len = 9 + body.size() + 4;
    s.push_back(static_cast<uint8_t>(0xB0 | ((len >> 8) & 0x0F)));
    s.push_back(static_cast<uint8_t>(len));
    s.push_back(mac[5]); // MAC_address_6
    s.push_back(mac[4]); // MAC_address_5
    s.push_back(static_cast<uint8_t>(0xC1 | (scrambled ? 0x10 : 0) | (llcSnap ? 0x02 : 0)));
    s.push_back(0); // section_number
    s.push_back(0); // last_section_number
    s.push_back(mac[3]);
    s.push_back(mac[2]);
    s.push_back(mac[1]);
    s.push_back(mac[0]);
    s.insert(s.end(), body.begin(), body.end());
    const uint32_t crc = crc32Mpeg(s.data(), s.size());
    for (int i = 3; i >= 0; --i)
        s.push_back(static_cast<uint8_t>(crc >> (8 * i)));
    return s;
}

inline Bytes dvbString(const std::string &s)
{
    return Bytes(s.begin(), s.end());
}

inline void addDescriptor(Bytes &b, uint8_t tag, const Bytes &data)
{
    b.push_back(tag);
    b.push_back(static_cast<uint8_t>(data.size()));
    b.insert(b.end(), data.begin(), data.end());
}

// ---------------------------------------------------------------------------
// EIT builders
// ---------------------------------------------------------------------------

// MJD + BCD UTC time (5 bytes)
inline Bytes dvbUtc(int64_t t)
{
    auto bcd = [](int v) { return static_cast<uint8_t>(((v / 10) << 4) | (v % 10)); };
    Bytes b;
    const long secs = static_cast<long>(t % 86400);
    put16(b, static_cast<uint16_t>(40587 + t / 86400));
    b.push_back(bcd(static_cast<int>(secs / 3600)));
    b.push_back(bcd(static_cast<int>(secs / 60 % 60)));
    b.push_back(bcd(static_cast<int>(secs % 60)));
    return b;
}

struct EitEventSpec
{
    uint16_t id = 0;
    int64_t start = 0;
    uint32_t duration = 1800;
    uint8_t running = 0;
    std::string lang = "eng";
    Bytes name;                    // raw DVB strings (may start with a character table selector)
    Bytes text;
    std::vector<Bytes> extended;   // raw text pieces, one extended event descriptor each
    std::vector<std::pair<std::string, std::string>> items; // in the first extended descriptor
    std::vector<uint8_t> content;  // content nibble bytes
    uint8_t rating = 0;            // parental rating byte (age - 3)
};

inline Bytes eitEvent(const EitEventSpec &e)
{
    auto bcd = [](int v) { return static_cast<uint8_t>(((v / 10) << 4) | (v % 10)); };
    Bytes d;
    Bytes se(e.lang.begin(), e.lang.end());
    se.push_back(static_cast<uint8_t>(e.name.size()));
    se.insert(se.end(), e.name.begin(), e.name.end());
    se.push_back(static_cast<uint8_t>(e.text.size()));
    se.insert(se.end(), e.text.begin(), e.text.end());
    addDescriptor(d, 0x4D, se);
    const size_t n = e.extended.size();
    for (size_t i = 0; i < n; ++i) {
        Bytes x;
        x.push_back(static_cast<uint8_t>((i << 4) | (n - 1)));
        x.insert(x.end(), e.lang.begin(), e.lang.end());
        Bytes items;
        if (i == 0)
            for (const auto &it : e.items) {
                items.push_back(static_cast<uint8_t>(it.first.size()));
                items.insert(items.end(), it.first.begin(), it.first.end());
                items.push_back(static_cast<uint8_t>(it.second.size()));
                items.insert(items.end(), it.second.begin(), it.second.end());
            }
        x.push_back(static_cast<uint8_t>(items.size()));
        x.insert(x.end(), items.begin(), items.end());
        x.push_back(static_cast<uint8_t>(e.extended[i].size()));
        x.insert(x.end(), e.extended[i].begin(), e.extended[i].end());
        addDescriptor(d, 0x4E, x);
    }
    if (!e.content.empty()) {
        Bytes c;
        for (uint8_t v : e.content) {
            c.push_back(v);
            c.push_back(0);
        }
        addDescriptor(d, 0x54, c);
    }
    if (e.rating)
        addDescriptor(d, 0x55, {'F', 'R', 'A', e.rating});
    Bytes b;
    put16(b, e.id);
    const Bytes t = dvbUtc(e.start);
    b.insert(b.end(), t.begin(), t.end());
    b.push_back(bcd(static_cast<int>(e.duration / 3600)));
    b.push_back(bcd(static_cast<int>(e.duration / 60 % 60)));
    b.push_back(bcd(static_cast<int>(e.duration % 60)));
    put16(b, static_cast<uint16_t>((e.running << 13) | d.size()));
    b.insert(b.end(), d.begin(), d.end());
    return b;
}

// EIT section (0x4E/0x4F p/f, 0x50..0x6F schedule) with the given events
inline Bytes makeEitSection(uint8_t tableId, uint16_t sid, uint16_t tsid, uint16_t onid, uint8_t version,
                            uint8_t section, uint8_t lastSection, const std::vector<Bytes> &events)
{
    Bytes body;
    put16(body, tsid);
    put16(body, onid);
    body.push_back(section); // segment_last_section_number
    body.push_back(tableId);
    for (const Bytes &e : events)
        body.insert(body.end(), e.begin(), e.end());
    return makeSection(tableId, sid, version, body, section, lastSection);
}

} // namespace builders
