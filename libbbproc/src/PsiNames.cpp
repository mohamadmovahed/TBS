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

std::string contentGenreName(uint8_t n)
{
    switch (n) {
    case 0x10: return "Movie / drama";
    case 0x11: return "Detective / thriller";
    case 0x12: return "Adventure / western / war";
    case 0x13: return "Science fiction / fantasy / horror";
    case 0x14: return "Comedy";
    case 0x15: return "Soap / melodrama / folklore";
    case 0x16: return "Romance";
    case 0x17: return "Serious / classical / religious / historical drama";
    case 0x18: return "Adult movie / drama";
    case 0x20: return "News / current affairs";
    case 0x21: return "News / weather report";
    case 0x22: return "News magazine";
    case 0x23: return "Documentary";
    case 0x24: return "Discussion / interview / debate";
    case 0x30: return "Show / game show";
    case 0x31: return "Game show / quiz / contest";
    case 0x32: return "Variety show";
    case 0x33: return "Talk show";
    case 0x40: return "Sports";
    case 0x41: return "Special sports event";
    case 0x42: return "Sports magazine";
    case 0x43: return "Football / soccer";
    case 0x44: return "Tennis / squash";
    case 0x45: return "Team sports";
    case 0x46: return "Athletics";
    case 0x47: return "Motor sport";
    case 0x48: return "Water sport";
    case 0x49: return "Winter sports";
    case 0x4A: return "Equestrian";
    case 0x4B: return "Martial sports";
    case 0x50: return "Children's / youth";
    case 0x51: return "Pre-school children's";
    case 0x52: return "Entertainment 6 to 14";
    case 0x53: return "Entertainment 10 to 16";
    case 0x54: return "Informational / educational / school";
    case 0x55: return "Cartoons / puppets";
    case 0x60: return "Music / ballet / dance";
    case 0x61: return "Rock / pop";
    case 0x62: return "Serious / classical music";
    case 0x63: return "Folk / traditional music";
    case 0x64: return "Jazz";
    case 0x65: return "Musical / opera";
    case 0x66: return "Ballet";
    case 0x70: return "Arts / culture";
    case 0x71: return "Performing arts";
    case 0x72: return "Fine arts";
    case 0x73: return "Religion";
    case 0x74: return "Popular culture / traditional arts";
    case 0x75: return "Literature";
    case 0x76: return "Film / cinema";
    case 0x77: return "Experimental film / video";
    case 0x78: return "Broadcasting / press";
    case 0x79: return "New media";
    case 0x7A: return "Arts / culture magazine";
    case 0x7B: return "Fashion";
    case 0x80: return "Social / political / economics";
    case 0x81: return "Magazine / report / documentary";
    case 0x82: return "Economics / social advisory";
    case 0x83: return "Remarkable people";
    case 0x90: return "Education / science / factual";
    case 0x91: return "Nature / animals / environment";
    case 0x92: return "Technology / natural sciences";
    case 0x93: return "Medicine / physiology / psychology";
    case 0x94: return "Foreign countries / expeditions";
    case 0x95: return "Social / spiritual sciences";
    case 0x96: return "Further education";
    case 0x97: return "Languages";
    case 0xA0: return "Leisure / hobbies";
    case 0xA1: return "Tourism / travel";
    case 0xA2: return "Handicraft";
    case 0xA3: return "Motoring";
    case 0xA4: return "Fitness and health";
    case 0xA5: return "Cooking";
    case 0xA6: return "Advertisement / shopping";
    case 0xA7: return "Gardening";
    case 0xB0: return "Original language";
    case 0xB1: return "Black and white";
    case 0xB2: return "Unpublished";
    case 0xB3: return "Live broadcast";
    default: break;
    }
    // Unlisted level 2: fall back to the level 1 group
    static const char *const level1[16] = {"",
                                           "Movie / drama",
                                           "News / current affairs",
                                           "Show / game show",
                                           "Sports",
                                           "Children's / youth",
                                           "Music / ballet / dance",
                                           "Arts / culture",
                                           "Social / political / economics",
                                           "Education / science / factual",
                                           "Leisure / hobbies",
                                           "Special characteristics",
                                           "",
                                           "",
                                           "",
                                           "User defined"};
    return level1[n >> 4];
}

std::string runningStatusName(uint8_t s)
{
    static const char *const names[] = {"undefined", "not running", "starts soon", "pausing", "running", "off-air", "", ""};
    return names[s & 7];
}

} // namespace bb
} // namespace tbs
