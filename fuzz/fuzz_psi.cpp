// PSI/SI section parsers. The harness appends a valid CRC-32 so sections are
// accepted, and also feeds them packetised on the matching PID.
//
// Record layout: pid selector(1) len(2) | section bytes (table_id, flags, ...)

#include "FuzzInput.h"

#include <tbsbbproc/DvbText.h>
#include <tbsbbproc/TsTableAnalyzer.h>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    static const uint16_t kPids[] = {0x0000, 0x0001, 0x0010, 0x0011, 0x0012, 0x0014, 0x0100, 0x0200};
    FuzzInput in(data, size);
    tbs::bb::TsTableAnalyzer a;
    uint8_t cc[8] = {};
    int sections = 0;
    while (in.remaining() >= 4 && sections < 64) {
        const uint8_t sel = in.u8();
        const uint16_t pid = kPids[sel & 7];
        std::vector<uint8_t> s = in.bytes(std::min<size_t>(in.u16() & 0x0FFF, 4092));
        if (s.size() < 3)
            break;
        // Make the section length field consistent and append the CRC
        const size_t len = s.size() + 4 - 3;
        s[1] = static_cast<uint8_t>((s[1] & 0xF0) | ((len >> 8) & 0x0F));
        s[2] = static_cast<uint8_t>(len);
        const uint32_t crc = tbs::bb::crc32Mpeg(s.data(), s.size());
        for (int i = 3; i >= 0; --i)
            s.push_back(static_cast<uint8_t>(crc >> (8 * i)));

        if (sel & 0x80) {
            a.pushSection(pid, s.data(), s.size());
        } else {
            // Packetise: PUSI + pointer field, 0xFF stuffing
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
                std::copy(s.begin() + static_cast<std::ptrdiff_t>(pos), s.begin() + static_cast<std::ptrdiff_t>(pos + take), p + off);
                pos += take;
                first = false;
                a.pushPackets(p, 1);
            }
        }
        ++sections;
    }
    (void)a.report();
    return 0;
}
