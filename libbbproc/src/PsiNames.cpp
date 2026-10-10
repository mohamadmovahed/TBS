#include "tbsbbproc/PsiTypes.h"

#include <cstdio>

namespace tbs {
namespace bb {

namespace {
std::string hex(unsigned v, int digits)
{
    char buf[16];
    std::snprintf(buf, sizeof(buf), "0x%0*X", digits, v);
    return buf;
}
} // namespace

std::string streamTypeName(uint8_t t)
{
    switch (t) {
    case 0x01: return "MPEG-1 video";
    case 0x02: return "MPEG-2 video";
    case 0x03: return "MPEG-1 audio";
    case 0x04: return "MPEG-2 audio";
    case 0x05: return "Private sections";
    case 0x06: return "Private PES";
    case 0x0B: return "DSM-CC";
    case 0x0D: return "DSM-CC sections";
    case 0x0F: return "AAC (ADTS)";
    case 0x10: return "MPEG-4 video";
    case 0x11: return "AAC (LATM)";
    case 0x15: return "Metadata";
    case 0x1B: return "H.264";
    case 0x1C: return "MPEG-4 audio";
    case 0x24: return "HEVC";
    case 0x2D: return "MPEG-H 3D audio";
    case 0x33: return "VVC";
    case 0x42: return "AVS2 video";
    case 0x81: return "AC-3 (ATSC)";
    case 0x86: return "SCTE-35";
    case 0x87: return "E-AC-3 (ATSC)";
    case 0xD2: return "AVS2 video";
    case 0xEA: return "VC-1";
    default: return "Type " + hex(t, 2);
    }
}

std::string serviceTypeName(uint8_t t)
{
    switch (t) {
    case 0x01: return "TV";
    case 0x02: return "Radio";
    case 0x03: return "Teletext";
    case 0x04: return "NVOD reference";
    case 0x05: return "NVOD time-shifted";
    case 0x06: return "Mosaic";
    case 0x07: return "FM radio";
    case 0x0A: return "Radio (advanced codec)";
    case 0x0C: return "Data broadcast";
    case 0x10: return "DVB MHP";
    case 0x11: return "TV HD (MPEG-2)";
    case 0x16: return "TV SD (H.264)";
    case 0x17: return "NVOD time-shifted (H.264 SD)";
    case 0x18: return "NVOD reference (H.264 SD)";
    case 0x19: return "TV HD (H.264)";
    case 0x1C: return "TV 3D (H.264)";
    case 0x1F: return "TV (HEVC)";
    case 0x20: return "TV UHD (HEVC)";
    default: return "Service type " + hex(t, 2);
    }
}

std::string caSystemName(uint16_t id)
{
    switch (id >> 8) {
    case 0x01: return "Seca/Mediaguard";
    case 0x05: return "Viaccess";
    case 0x06: return "Irdeto";
    case 0x09: return "Videoguard (NDS)";
    case 0x0B: return "Conax";
    case 0x0D: return "Cryptoworks";
    case 0x0E: return "PowerVu";
    case 0x10: return "Tandberg";
    case 0x17: return "BetaCrypt";
    case 0x18: return "Nagravision";
    case 0x22: return "Codicrypt";
    case 0x26: return "BISS";
    case 0x4A: return id >= 0x4AE0 && id <= 0x4AE1 ? "DRE-Crypt" : "CA " + hex(id, 4);
    case 0x56: return "Verimatrix";
    default: return "CA " + hex(id, 4);
    }
}

std::string tableName(uint8_t id)
{
    switch (id) {
    case 0x00: return "PAT";
    case 0x01: return "CAT";
    case 0x02: return "PMT";
    case 0x40: return "NIT actual";
    case 0x41: return "NIT other";
    case 0x42: return "SDT actual";
    case 0x46: return "SDT other";
    case 0x4A: return "BAT";
    case 0x4E: return "EIT p/f actual";
    case 0x4F: return "EIT p/f other";
    case 0x70: return "TDT";
    case 0x73: return "TOT";
    default:
        if (id >= 0x50 && id <= 0x5F)
            return "EIT schedule actual";
        if (id >= 0x60 && id <= 0x6F)
            return "EIT schedule other";
        return "Table " + hex(id, 2);
    }
}

std::string runningStatusName(uint8_t s)
{
    static const char *const names[] = {"undefined", "not running", "starts soon", "pausing", "running", "off-air", "", ""};
    return names[s & 7];
}

} // namespace bb
} // namespace tbs
