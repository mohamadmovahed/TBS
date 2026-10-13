#include "tbsbbproc/DvbText.h"

#include <tbsdvb/Psi.h>

namespace tbs {
namespace bb {

namespace {

void appendUtf8(std::string &out, uint32_t cp)
{
    if (cp < 0x80) {
        out += static_cast<char>(cp);
    } else if (cp < 0x800) {
        out += static_cast<char>(0xC0 | (cp >> 6));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        out += static_cast<char>(0xE0 | (cp >> 12));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (cp >> 18));
        out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    }
}

// Code point for a byte >= 0xA0 in ISO 8859-n; 0 = undefined
uint32_t iso8859(int part, uint8_t c)
{
    if (c < 0xA0)
        return c;
    switch (part) {
    case 5: // Cyrillic
        if (c == 0xA0)
            return 0xA0;
        if (c == 0xAD)
            return 0xAD;
        if (c == 0xF0)
            return 0x2116;
        if (c == 0xFD)
            return 0xA7;
        return 0x0360 + c; // 0xA1 -> U+0401 ... 0xFF -> U+045F
    case 6: // Arabic (also used for Persian services)
        if (c == 0xA0 || c == 0xA4 || c == 0xAD)
            return c;
        if (c == 0xAC)
            return 0x060C;
        if (c == 0xBB)
            return 0x061B;
        if (c == 0xBF)
            return 0x061F;
        if (c >= 0xC1 && c <= 0xDA)
            return 0x0560 + c; // U+0621..U+063A
        if (c >= 0xE0 && c <= 0xF2)
            return 0x0560 + c; // U+0640..U+0652
        return 0;
    case 7: // Greek
        if (c >= 0xB4 && c != 0xB7 && c != 0xBB && c != 0xBD)
            return 0x02D0 + c; // U+0384..U+03CE
        if (c == 0xA1)
            return 0x2018;
        if (c == 0xA2)
            return 0x2019;
        return c;
    case 8: // Hebrew
        if (c >= 0xE0 && c <= 0xFA)
            return 0x04F0 + c; // U+05D0..U+05EA
        if (c == 0xAA)
            return 0xD7;
        if (c == 0xBA)
            return 0xF7;
        if (c < 0xBF)
            return c;
        return 0;
    case 9: // Turkish: Latin-1 with six replacements
        switch (c) {
        case 0xD0: return 0x011E;
        case 0xDD: return 0x0130;
        case 0xDE: return 0x015E;
        case 0xF0: return 0x011F;
        case 0xFD: return 0x0131;
        case 0xFE: return 0x015F;
        default: return c;
        }
    case 15: // Latin-9
        switch (c) {
        case 0xA4: return 0x20AC;
        case 0xA6: return 0x0160;
        case 0xA8: return 0x0161;
        case 0xB4: return 0x017D;
        case 0xB8: return 0x017E;
        case 0xBC: return 0x0152;
        case 0xBD: return 0x0153;
        case 0xBE: return 0x0178;
        default: return c;
        }
    default: // 1 and anything not tabulated: Latin-1
        return c;
    }
}

} // namespace

std::string dvbTextToUtf8(const uint8_t *p, size_t len)
{
    std::string out;
    if (!p || !len)
        return out;

    enum class Cs { Iso6937, Iso8859, Ucs2, Utf8 } cs = Cs::Iso6937;
    int part = 1;
    size_t i = 0;
    const uint8_t first = p[0];
    if (first >= 0x01 && first <= 0x0B) {
        cs = Cs::Iso8859;
        part = first + 4; // 0x01 = 8859-5 ... 0x0B = 8859-15
        i = 1;
    } else if (first == 0x10 && len >= 3) {
        cs = Cs::Iso8859;
        part = (p[1] << 8) | p[2];
        i = 3;
    } else if (first == 0x11 || first == 0x14) {
        cs = Cs::Ucs2;
        i = 1;
    } else if (first == 0x15) {
        cs = Cs::Utf8;
        i = 1;
    } else if (first == 0x1F && len >= 2) {
        i = 2; // encoding_type_id: not supported, show the bytes as Latin-1
    } else if (first < 0x20) {
        i = 1; // reserved selector
    }

    if (cs == Cs::Utf8) {
        for (; i < len; ++i) {
            const uint8_t c = p[i];
            if (c == 0x8A)
                out += '\n';
            else if (c >= 0x80 && c <= 0x9F && (i == 0 || p[i - 1] < 0xC0))
                continue; // DVB control code outside a multi-byte sequence
            else
                out += static_cast<char>(c);
        }
        return out;
    }
    if (cs == Cs::Ucs2) {
        for (; i + 1 < len; i += 2) {
            const uint32_t cp = (p[i] << 8) | p[i + 1];
            if (cp == 0xE08A)
                out += '\n';
            else if (cp >= 0xE080 && cp <= 0xE09F)
                continue;
            else if (cp >= 0x20)
                appendUtf8(out, cp);
        }
        return out;
    }

    for (; i < len; ++i) {
        const uint8_t c = p[i];
        if (c == 0x8A) {
            out += '\n';
            continue;
        }
        if (c < 0x20 || (c >= 0x7F && c <= 0x9F))
            continue; // control and DVB emphasis codes
        if (c < 0x7F) {
            out += static_cast<char>(c);
            continue;
        }
        if (cs == Cs::Iso8859) {
            if (const uint32_t cp = iso8859(part, c))
                appendUtf8(out, cp);
            continue;
        }
        // ISO 6937: 0xC1..0xCF are non-spacing diacritics placed before the letter.
        // Approximation: drop the diacritic, keep the base letter.
        if (c >= 0xC1 && c <= 0xCF)
            continue;
        appendUtf8(out, c);
    }
    return out;
}

uint32_t crc32Mpeg(const uint8_t *p, size_t len, uint32_t crc)
{
    return tbs::crc32Mpeg(p, len, crc);
}

} // namespace bb
} // namespace tbs
