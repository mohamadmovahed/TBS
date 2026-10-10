#include "tbsbbproc/Gse.h"

#include "tbsbbproc/DvbText.h"

#include <cstring>

namespace tbs {
namespace bb {

namespace {
inline uint16_t u16(const uint8_t *p)
{
    return static_cast<uint16_t>((p[0] << 8) | p[1]);
}
inline uint32_t u32(const uint8_t *p)
{
    return (static_cast<uint32_t>(p[0]) << 24) | (p[1] << 16) | (p[2] << 8) | p[3];
}
size_t labelBytes(uint8_t lt)
{
    return lt == 0 ? 6 : lt == 1 ? 3 : 0;
}
} // namespace

void GseDecapsulator::reset()
{
    m_stats = GseStats();
    for (auto &f : m_frags)
        f = Fragment();
    m_field = 0;
    m_haveLastLabel = false;
    m_lastLabelLen = 0;
    m_carry.clear();
    m_hemSynced = false;
}

void GseDecapsulator::pushDataField(const uint8_t *df, size_t len, bool hem, uint16_t syncdBits, const PduCallback &cb)
{
    ++m_field;
    ++m_stats.dataFields;
    bool ok = true;

    if (!hem) {
        ok = parsePackets(df, len, false, nullptr, cb);
    } else if (syncdBits == 0xFFFF) {
        // No packet starts here: the whole field continues the carried packet
        if (m_hemSynced) {
            m_carry.insert(m_carry.end(), df, df + len);
            if (m_carry.size() > 65536 + 4) {
                m_carry.clear();
                m_hemSynced = false;
                ok = false;
                ++m_stats.lengthErrors;
            }
        }
    } else {
        const size_t start = syncdBits / 8u;
        if (start > len) {
            ok = false;
            ++m_stats.lengthErrors;
        } else {
            if (m_hemSynced && !m_carry.empty()) {
                m_carry.insert(m_carry.end(), df, df + start);
                ok = parsePackets(m_carry.data(), m_carry.size(), false, nullptr, cb) && ok;
            }
            m_carry.clear();
            m_hemSynced = true;
            ok = parsePackets(df + start, len - start, true, &m_carry, cb) && ok;
        }
    }
    if (ok)
        ++m_stats.validDataFields;
    else
        ++m_stats.invalidDataFields;
}

bool GseDecapsulator::parsePackets(const uint8_t *p, size_t len, bool allowTail, std::vector<uint8_t> *tail, const PduCallback &cb)
{
    size_t pos = 0;
    bool ok = true;
    while (pos < len) {
        if ((p[pos] & 0xF0) == 0) {
            // S = E = 0 and LT = 00: the rest of the data field is padding
            m_stats.paddingBytes += len - pos;
            return ok;
        }
        if (pos + 2 > len) {
            if (allowTail && tail) {
                tail->assign(p + pos, p + len);
                return ok;
            }
            ++m_stats.lengthErrors;
            return false;
        }
        const size_t glen = ((p[pos] & 0x0F) << 8) | p[pos + 1];
        if (pos + 2 + glen > len) {
            if (allowTail && tail) {
                tail->assign(p + pos, p + len);
                return ok;
            }
            ++m_stats.lengthErrors;
            return false;
        }
        ok = handlePacket(p + pos, 2 + glen, cb) && ok;
        pos += 2 + glen;
    }
    return ok;
}

bool GseDecapsulator::handlePacket(const uint8_t *p, size_t len, const PduCallback &cb)
{
    ++m_stats.packets;
    const bool s = p[0] & 0x80;
    const bool e = p[0] & 0x40;
    const uint8_t lt = (p[0] >> 4) & 0x03;
    const uint8_t *q = p + 2;
    const uint8_t *end = p + len;

    if (s && e) {
        // Complete PDU: Protocol_Type, Label, PDU
        if (end - q < 2) {
            ++m_stats.lengthErrors;
            return false;
        }
        const uint16_t type = u16(q);
        q += 2;
        uint8_t label[6] = {};
        const size_t lb = labelBytes(lt);
        if (static_cast<size_t>(end - q) < lb) {
            ++m_stats.lengthErrors;
            return false;
        }
        std::memcpy(label, q, lb);
        q += lb;
        ++m_stats.completePdus;
        deliver(type, lt, label, q, static_cast<size_t>(end - q), cb);
        return true;
    }

    ++m_stats.fragments;
    if (end - q < 1) {
        ++m_stats.lengthErrors;
        return false;
    }
    const uint8_t fragId = q[0];
    Fragment &f = m_frags[fragId];

    if (s) {
        // First fragment: Frag_ID, Total_Length, Protocol_Type, Label, PDU part
        if (end - q < 3) {
            ++m_stats.lengthErrors;
            return false;
        }
        if (f.active)
            ++m_stats.fragmentErrors; // previous PDU with this ID never completed
        f.active = true;
        f.labelType = lt;
        f.startField = m_field;
        f.data.assign(q + 1, end);
        return true;
    }

    // Middle (E=0) or last (E=1) fragment
    if (!f.active) {
        ++m_stats.fragmentErrors;
        return true; // not a framing error: the start was simply missed
    }
    if (m_field - f.startField > kFragmentTimeout) {
        ++m_stats.fragmentErrors;
        f.active = false;
        return true;
    }
    const uint8_t *dataEnd = e ? end - 4 : end;
    if (dataEnd < q + 1) {
        ++m_stats.lengthErrors;
        f.active = false;
        return false;
    }
    f.data.insert(f.data.end(), q + 1, dataEnd);
    if (f.data.size() > 65536) {
        ++m_stats.fragmentErrors;
        f.active = false;
        return true;
    }
    if (!e)
        return true;

    f.active = false;
    const uint32_t crc = u32(dataEnd);
    if (crc32Mpeg(f.data.data(), f.data.size()) != crc) {
        ++m_stats.crcErrors;
        return true;
    }
    if (f.data.size() < 4 || u16(f.data.data()) + 2u != f.data.size()) {
        ++m_stats.lengthErrors;
        return true;
    }
    const uint8_t *r = f.data.data() + 2;
    const uint8_t *rend = f.data.data() + f.data.size();
    const uint16_t type = u16(r);
    r += 2;
    uint8_t label[6] = {};
    const size_t lb = labelBytes(f.labelType);
    if (static_cast<size_t>(rend - r) < lb) {
        ++m_stats.lengthErrors;
        return true;
    }
    std::memcpy(label, r, lb);
    r += lb;
    ++m_stats.reassembled;
    deliver(type, f.labelType, label, r, static_cast<size_t>(rend - r), cb);
    return true;
}

void GseDecapsulator::deliver(uint16_t type, uint8_t labelType, const uint8_t *label, const uint8_t *pdu, size_t len,
                              const PduCallback &cb)
{
    GsePdu out;
    out.labelType = labelType;
    if (labelType == 3) {
        // Label re-use: same label as the previous PDU
        if (m_haveLastLabel) {
            std::memcpy(out.label, m_lastLabel, m_lastLabelLen);
            out.labelLen = m_lastLabelLen;
        }
    } else {
        out.labelLen = static_cast<uint8_t>(labelBytes(labelType));
        std::memcpy(out.label, label, out.labelLen);
        std::memcpy(m_lastLabel, label, out.labelLen);
        m_lastLabelLen = out.labelLen;
        m_haveLastLabel = true;
    }
    ++m_stats.labelTypes[labelType & 3];

    // Extension headers (ULE / RFC 4326 type field): values below 1536
    int guard = 0;
    while (type < 0x0600) {
        if (++guard > 16) {
            ++m_stats.unsupportedExtensions;
            return;
        }
        const unsigned hlen = (type >> 8) & 0x07;
        const unsigned htype = type & 0xFF;
        if (hlen == 0) {
            // Mandatory extension header: terminates the chain
            if (htype == 0x00) {
                ++m_stats.testPdus;
                return;
            }
            if (htype == 0x01 && len >= 14) {
                out.bridged = true;
                type = u16(pdu + 12); // EtherType of the bridged frame
                break;
            }
            ++m_stats.unsupportedExtensions;
            return;
        }
        // Optional extension header: 2*H-LEN bytes, the last two are the next type
        const size_t extLen = 2u * hlen;
        if (len < extLen) {
            ++m_stats.lengthErrors;
            return;
        }
        type = u16(pdu + extLen - 2);
        pdu += extLen;
        len -= extLen;
    }

    out.protocolType = type;
    out.data = pdu;
    out.len = len;
    ++m_stats.pdus;
    m_stats.pduBytes += len;
    if (m_stats.protocolTypes.size() < 256 || m_stats.protocolTypes.count(type))
        ++m_stats.protocolTypes[type];
    if (cb)
        cb(out);
}

} // namespace bb
} // namespace tbs
