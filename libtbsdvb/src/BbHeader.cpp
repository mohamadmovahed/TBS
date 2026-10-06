#include "tbsdvb/BbHeader.h"

namespace tbs {

uint8_t BbHeader::crc8(const uint8_t *p, size_t len)
{
    // g(X) = X^8 + X^7 + X^6 + X^4 + X^2 + 1, MSB first, initial value 0
    uint8_t crc = 0;
    for (size_t i = 0; i < len; ++i) {
        crc ^= p[i];
        for (int b = 0; b < 8; ++b)
            crc = (crc & 0x80) ? static_cast<uint8_t>((crc << 1) ^ 0xD5) : static_cast<uint8_t>(crc << 1);
    }
    return crc;
}

bool BbHeader::parse(const uint8_t *p)
{
    matype1 = p[0];
    matype2 = p[1];
    upl = static_cast<uint16_t>((p[2] << 8) | p[3]);
    dfl = static_cast<uint16_t>((p[4] << 8) | p[5]);
    sync = p[6];
    syncd = static_cast<uint16_t>((p[7] << 8) | p[8]);
    crc = p[9];
    const uint8_t calc = crc8(p, 9);
    if (calc == crc)
        mode = Mode::Normal;
    else if ((calc ^ 0x01) == crc)
        mode = Mode::HighEfficiency;
    else
        mode = Mode::Invalid;
    return mode != Mode::Invalid;
}

std::string BbHeader::streamTypeName() const
{
    switch (streamType()) {
    case StreamType::GenericPacketized:
        return "Generic packetized";
    case StreamType::GenericContinuous:
        return "Generic continuous";
    case StreamType::GseHem:
        return "GSE-HEM";
    case StreamType::Transport:
        return "Transport stream";
    }
    return {};
}

std::string BbHeader::rolloffName() const
{
    static const char *const names[] = {"0.35", "0.25", "0.20", "reserved/S2X"};
    return names[rolloffBits()];
}

std::string BbHeader::modeName() const
{
    switch (mode) {
    case Mode::Normal:
        return "NM";
    case Mode::HighEfficiency:
        return "HEM";
    case Mode::Invalid:
        break;
    }
    return "CRC error";
}

std::string BbHeader::summary() const
{
    std::string s = streamTypeName();
    s += singleInputStream() ? ", SIS" : ", MIS ISI " + std::to_string(matype2);
    s += ccm() ? ", CCM" : ", ACM/VCM";
    if (issyi())
        s += ", ISSY";
    if (npd())
        s += ", NPD";
    s += ", RO " + rolloffName() + ", " + modeName();
    return s;
}

} // namespace tbs
