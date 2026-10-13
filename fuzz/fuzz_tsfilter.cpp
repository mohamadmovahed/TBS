// Service / PID filter of the TS network output (PAT/PMT following, PAT rewrite).
// The harness appends valid CRC-32s so PAT and PMT sections are accepted.
//
// Layout: program(2) flags(1) | records: selector(1) len(2) bytes
//   selector bit 7 set: one raw 188-byte packet (sync byte forced) on the selected PID
//   otherwise: a section (CRC appended) packetised on the selected PID

#include "FuzzInput.h"

#include <tbsdvb/Psi.h>
#include <tbsdvb/TsFilter.h>

#include <cstdlib>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    static const uint16_t kPids[] = {0x0000, 0x0100, 0x0200, 0x0101, 0x0102, 0x0103, 0x0011, 0x1FFF};
    FuzzInput in(data, size);
    if (in.remaining() < 3)
        return 0;
    tbs::TsFilterOptions opt;
    opt.programNumber = in.u16();
    const uint8_t flags = in.u8();
    if (flags & 1)
        opt.pids = {0x0011, 0x0101};
    opt.dropNullPackets = (flags & 2) == 0;
    tbs::TsFilter f(opt);
    auto check = [](const uint8_t *p) {
        if (p[0] != 0x47)
            std::abort();
    };
    uint8_t cc[8] = {};
    int records = 0;
    while (in.remaining() >= 4 && records < 128) {
        const uint8_t sel = in.u8();
        const uint16_t pid = kPids[sel & 7];
        std::vector<uint8_t> s = in.bytes(std::min<size_t>(in.u16() & 0x0FFF, 4092));
        if (sel & 0x80) {
            uint8_t p[188] = {};
            std::copy(s.begin(), s.begin() + static_cast<std::ptrdiff_t>(std::min<size_t>(s.size(), 188)), p);
            p[0] = 0x47;
            p[1] = static_cast<uint8_t>((p[1] & 0xE0) | (pid >> 8));
            p[2] = static_cast<uint8_t>(pid);
            f.push(p, check);
            ++records;
            continue;
        }
        if (s.size() < 3)
            break;
        const size_t len = s.size() + 4 - 3;
        s[1] = static_cast<uint8_t>((s[1] & 0xF0) | ((len >> 8) & 0x0F));
        s[2] = static_cast<uint8_t>(len);
        const uint32_t crc = tbs::crc32Mpeg(s.data(), s.size());
        for (int i = 3; i >= 0; --i)
            s.push_back(static_cast<uint8_t>(crc >> (8 * i)));
        size_t pos = 0;
        bool first = true;
        while (pos < s.size()) {
            uint8_t p[188];
            std::fill(p, p + 188, 0xFF);
            p[0] = 0x47;
            p[1] = static_cast<uint8_t>((first ? 0x40 : 0) | (pid >> 8));
            p[2] = static_cast<uint8_t>(pid);
            p[3] = static_cast<uint8_t>(0x10 | (cc[sel & 7]++ & 0x0F));
            size_t off = 4;
            if (first)
                p[off++] = 0;
            const size_t take = std::min(188 - off, s.size() - pos);
            std::copy(s.begin() + static_cast<std::ptrdiff_t>(pos), s.begin() + static_cast<std::ptrdiff_t>(pos + take),
                      p + off);
            pos += take;
            first = false;
            f.push(p, check);
        }
        ++records;
    }
    (void)f.servicePids();
    return 0;
}
