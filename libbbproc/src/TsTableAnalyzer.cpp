#include "tbsbbproc/TsTableAnalyzer.h"

#include "tbsbbproc/DvbText.h"

#include <algorithm>
#include <cstdio>

namespace tbs {
namespace bb {

namespace {

constexpr uint16_t kPidPat = 0x0000;
constexpr uint16_t kPidCat = 0x0001;
constexpr uint16_t kPidNit = 0x0010;
constexpr uint16_t kPidSdt = 0x0011;
constexpr uint16_t kPidEit = 0x0012;
constexpr uint16_t kPidTdt = 0x0014;
constexpr size_t kMaxSection = 4096;
constexpr size_t kMaxScheduleEvents = 500000;

inline uint16_t u16(const uint8_t *p)
{
    return static_cast<uint16_t>((p[0] << 8) | p[1]);
}

int bcd(uint8_t b)
{
    return (b >> 4) * 10 + (b & 0x0F);
}

// MJD (16 bits) + UTC hh:mm:ss in BCD -> seconds since 1970; 0 if undefined
int64_t dvbTime(const uint8_t *p)
{
    const int mjd = u16(p);
    if (mjd == 0xFFFF || mjd == 0)
        return 0;
    return (static_cast<int64_t>(mjd) - 40587) * 86400 + bcd(p[2]) * 3600 + bcd(p[3]) * 60 + bcd(p[4]);
}

uint32_t bcdDuration(const uint8_t *p)
{
    return static_cast<uint32_t>(bcd(p[0]) * 3600 + bcd(p[1]) * 60 + bcd(p[2]));
}

std::string text(const uint8_t *p, size_t len)
{
    return dvbTextToUtf8(p, len);
}

std::string lang(const uint8_t *p)
{
    std::string s;
    for (int i = 0; i < 3; ++i)
        if (p[i] >= 0x20 && p[i] < 0x7F)
            s += static_cast<char>(p[i]);
    return s;
}

void appendLang(std::string &dst, const std::string &l)
{
    if (l.empty() || dst.find(l) != std::string::npos)
        return;
    if (!dst.empty())
        dst += ", ";
    dst += l;
}

// Calls fn(tag, data, len) for each descriptor in [p, p+len)
template <typename Fn>
void forEachDescriptor(const uint8_t *p, size_t len, Fn fn)
{
    size_t i = 0;
    while (i + 2 <= len) {
        const uint8_t tag = p[i];
        const size_t dl = p[i + 1];
        if (i + 2 + dl > len)
            break;
        fn(tag, p + i + 2, dl);
        i += 2 + dl;
    }
}

std::string satelliteDelivery(const uint8_t *d, size_t len)
{
    if (len < 11)
        return {};
    char buf[160];
    // 8 BCD digits, decimal point after the third: 011.75725 GHz
    const double freqGHz = bcd(d[0]) * 10.0 + bcd(d[1]) / 10.0 + bcd(d[2]) / 1000.0 + bcd(d[3]) / 100000.0;
    const double orbital = bcd(d[4]) * 10.0 + bcd(d[5]) / 10.0;
    const bool east = d[6] & 0x80;
    static const char *const pol[] = {"H", "V", "L", "R"};
    static const char *const ro[] = {"0.35", "0.25", "0.20", "?"};
    const bool s2 = d[6] & 0x04;
    static const char *const mod[] = {"auto", "QPSK", "8PSK", "16QAM"};
    const double srMsym = bcd(d[7]) * 10.0 + bcd(d[8]) / 10.0 + bcd(d[9]) / 1000.0 + (d[10] >> 4) / 10000.0;
    static const char *const fec[] = {"?", "1/2", "2/3", "3/4", "5/6", "7/8", "8/9", "3/5", "4/5", "9/10", "?", "?", "?", "?", "?", "none"};
    std::snprintf(buf, sizeof(buf), "%s %.3f MHz %s, %.3f MS/s, %s %s%s%s, %.1f°%c", s2 ? "DVB-S2" : "DVB-S",
                  freqGHz * 1000.0, pol[(d[6] >> 5) & 3], srMsym, mod[d[6] & 3], fec[d[10] & 0x0F],
                  s2 ? ", RO " : "", s2 ? ro[(d[6] >> 3) & 3] : "", orbital, east ? 'E' : 'W');
    return buf;
}

std::string s2Delivery(const uint8_t *d, size_t len)
{
    if (len < 1)
        return {};
    std::string out;
    size_t i = 1;
    if ((d[0] & 0x80) && len >= i + 3) {
        const uint32_t gold = ((d[i] & 0x03u) << 16) | (d[i + 1] << 8) | d[i + 2];
        out += ", PLS gold " + std::to_string(gold);
        i += 3;
    }
    if ((d[0] & 0x40) && len >= i + 1)
        out += ", ISI " + std::to_string(d[i]);
    return out;
}

} // namespace

struct TsTableAnalyzer::Collector
{
    std::vector<uint8_t> buf;
    int lastCc = -1;
    bool started = false;
};

TsTableAnalyzer::TsTableAnalyzer() = default;
TsTableAnalyzer::~TsTableAnalyzer() = default;

void TsTableAnalyzer::reset()
{
    m_packets = 0;
    m_collectors.clear();
    m_pmtPids.clear();
    m_patSeen = false;
    m_tsid = 0;
    m_patVersion = -1;
    m_programs.clear();
    m_emm.clear();
    m_networks.clear();
    m_bouquets.clear();
    m_services.clear();
    m_onid = 0;
    m_hasTime = false;
    m_utc = 0;
    m_sections = 0;
    m_crcErrors = 0;
    m_scheduleEvents = 0;
    m_tableStats.clear();
    m_tableInstances.clear();
    m_scheduleSeen.clear();
}

bool TsTableAnalyzer::wantsPid(uint16_t pid) const
{
    return pid == kPidPat || pid == kPidCat || pid == kPidNit || pid == kPidSdt || pid == kPidEit || pid == kPidTdt
        || m_pmtPids.count(pid);
}

void TsTableAnalyzer::pushPackets(const uint8_t *packets, size_t count)
{
    for (size_t n = 0; n < count; ++n) {
        const uint8_t *p = packets + n * 188;
        ++m_packets;
        if (p[0] != 0x47 || (p[1] & 0x80))
            continue; // not aligned or transport error
        const uint16_t pid = static_cast<uint16_t>(((p[1] & 0x1F) << 8) | p[2]);
        if (!wantsPid(pid))
            continue;
        const uint8_t afc = (p[3] >> 4) & 3;
        if (!(afc & 1))
            continue;
        size_t off = 4;
        if (afc & 2)
            off += 1 + p[4];
        if (off >= 188)
            continue;

        auto &c = m_collectors[pid];
        if (!c)
            c = std::make_unique<Collector>();
        const int cc = p[3] & 0x0F;
        if (c->lastCc >= 0) {
            if (cc == c->lastCc)
                continue; // duplicate
            if (cc != ((c->lastCc + 1) & 0x0F)) {
                c->buf.clear();
                c->started = false;
            }
        }
        c->lastCc = cc;

        const uint8_t *payload = p + off;
        size_t plen = 188 - off;
        auto drain = [&]() {
            while (c->buf.size() >= 3) {
                if (c->buf[0] == 0xFF) {
                    c->buf.clear();
                    c->started = false;
                    return;
                }
                const size_t total = 3 + (((c->buf[1] & 0x0F) << 8) | c->buf[2]);
                if (total > kMaxSection) {
                    c->buf.clear();
                    c->started = false;
                    return;
                }
                if (c->buf.size() < total)
                    return;
                onSection(pid, c->buf.data(), total);
                c->buf.erase(c->buf.begin(), c->buf.begin() + static_cast<std::ptrdiff_t>(total));
            }
        };

        if (p[1] & 0x40) {
            const size_t pointer = payload[0];
            if (1 + pointer > plen) {
                c->buf.clear();
                c->started = false;
                continue;
            }
            if (c->started && pointer > 0) {
                c->buf.insert(c->buf.end(), payload + 1, payload + 1 + pointer);
                drain();
            }
            c->buf.assign(payload + 1 + pointer, payload + plen);
            c->started = true;
            drain();
        } else if (c->started) {
            c->buf.insert(c->buf.end(), payload, payload + plen);
            drain();
        }
    }
}

void TsTableAnalyzer::pushSection(uint16_t pid, const uint8_t *section, size_t len)
{
    onSection(pid, section, len);
}

void TsTableAnalyzer::onSection(uint16_t pid, const uint8_t *s, size_t len)
{
    if (len < 3)
        return;
    const uint8_t tableId = s[0];
    const bool syntax = s[1] & 0x80;
    TableStat &st = m_tableStats[{tableId, pid}];
    st.tableId = tableId;
    st.pid = pid;
    st.name = tableName(tableId);
    ++st.sections;
    ++m_sections;

    if (syntax || tableId == 0x73) {
        if (len < (syntax ? 12u : 14u) || crc32Mpeg(s, len) != 0) {
            ++st.crcErrors;
            ++m_crcErrors;
            return;
        }
    }
    int version = -1;
    if (syntax) {
        version = (s[5] >> 1) & 0x1F;
        if (!(s[5] & 1))
            return; // next version, not yet applicable
        st.version = version;
        auto &inst = m_tableInstances[{tableId, pid}];
        if (inst.size() < 100000)
            inst.insert(u16(s + 3));
        st.instances = static_cast<uint32_t>(inst.size());
    }

    if (pid == kPidPat && tableId == 0x00)
        parsePat(s, len, version);
    else if (pid == kPidCat && tableId == 0x01)
        parseCat(s, len);
    else if (tableId == 0x02 && m_pmtPids.count(pid))
        parsePmt(pid, s, len, version);
    else if (pid == kPidNit && (tableId == 0x40 || tableId == 0x41))
        parseNitBat(s, len, false, version);
    else if (pid == kPidSdt && (tableId == 0x42 || tableId == 0x46))
        parseSdt(s, len);
    else if (pid == kPidSdt && tableId == 0x4A)
        parseNitBat(s, len, true, version);
    else if (pid == kPidEit && tableId >= 0x4E && tableId <= 0x6F)
        parseEit(s, len);
    else if (pid == kPidTdt && (tableId == 0x70 || tableId == 0x73))
        parseTime(s, len);
}

void TsTableAnalyzer::parsePat(const uint8_t *s, size_t len, int version)
{
    m_patSeen = true;
    m_tsid = u16(s + 3);
    m_patVersion = version;
    std::map<uint16_t, ProgramInfo> programs;
    for (size_t i = 8; i + 4 <= len - 4; i += 4) {
        const uint16_t number = u16(s + i);
        const uint16_t pid = u16(s + i + 2) & 0x1FFF;
        if (number == 0)
            continue; // network PID
        auto it = m_programs.find(number);
        // Keep the decoded PMT while the program keeps its PMT PID
        ProgramInfo prog = (it != m_programs.end() && it->second.pmtPid == pid) ? it->second : ProgramInfo();
        prog.programNumber = number;
        prog.pmtPid = pid;
        programs[number] = prog;
    }
    if (s[7] == 0) {
        m_programs.swap(programs); // single-section PAT: replaces the program list
    } else {
        for (auto &p : programs)
            m_programs[p.first] = p.second;
    }
    m_pmtPids.clear();
    for (const auto &p : m_programs)
        m_pmtPids.insert(p.second.pmtPid);
}

void TsTableAnalyzer::parseCat(const uint8_t *s, size_t len)
{
    std::vector<CaInfo> emm;
    forEachDescriptor(s + 8, len - 12, [&](uint8_t tag, const uint8_t *d, size_t dl) {
        if (tag == 0x09 && dl >= 4)
            emm.push_back({u16(d), static_cast<uint16_t>(u16(d + 2) & 0x1FFF), caSystemName(u16(d))});
    });
    m_emm = emm;
}

void TsTableAnalyzer::parsePmt(uint16_t pid, const uint8_t *s, size_t len, int version)
{
    const uint16_t number = u16(s + 3);
    ProgramInfo &prog = m_programs[number];
    prog.programNumber = number;
    prog.pmtPid = pid;
    prog.pmtSeen = true;
    prog.pmtVersion = version;
    prog.pcrPid = u16(s + 8) & 0x1FFF;
    prog.ca.clear();
    prog.streams.clear();

    const size_t end = len - 4;
    const size_t infoLen = u16(s + 10) & 0x0FFF;
    if (12 + infoLen > end)
        return;
    forEachDescriptor(s + 12, infoLen, [&](uint8_t tag, const uint8_t *d, size_t dl) {
        if (tag == 0x09 && dl >= 4)
            prog.ca.push_back({u16(d), static_cast<uint16_t>(u16(d + 2) & 0x1FFF), caSystemName(u16(d))});
    });

    size_t i = 12 + infoLen;
    while (i + 5 <= end) {
        EsInfo es;
        es.streamType = s[i];
        es.pid = u16(s + i + 1) & 0x1FFF;
        const size_t esLen = u16(s + i + 3) & 0x0FFF;
        if (i + 5 + esLen > end)
            break;

        // Classification from the stream type, refined by descriptors
        switch (es.streamType) {
        case 0x01: case 0x02: case 0x10: case 0x1B: case 0x24: case 0x33: case 0x42: case 0xD2: case 0xEA:
            es.kind = "Video";
            break;
        case 0x03: case 0x04: case 0x0F: case 0x11: case 0x1C: case 0x2D: case 0x81: case 0x87:
            es.kind = "Audio";
            break;
        case 0x05: case 0x0B: case 0x0D: case 0x86:
            es.kind = "Data";
            break;
        default:
            es.kind = "Data";
        }
        es.codec = streamTypeName(es.streamType);

        forEachDescriptor(s + i + 5, esLen, [&](uint8_t tag, const uint8_t *d, size_t dl) {
            switch (tag) {
            case 0x09:
                if (dl >= 4)
                    es.ca.push_back({u16(d), static_cast<uint16_t>(u16(d + 2) & 0x1FFF), caSystemName(u16(d))});
                break;
            case 0x0A:
                for (size_t k = 0; k + 4 <= dl; k += 4)
                    appendLang(es.language, lang(d + k));
                break;
            case 0x56:
                es.kind = "Teletext";
                es.codec = "DVB teletext";
                for (size_t k = 0; k + 5 <= dl; k += 5)
                    appendLang(es.language, lang(d + k));
                break;
            case 0x59:
                es.kind = "Subtitles";
                es.codec = "DVB subtitles";
                for (size_t k = 0; k + 8 <= dl; k += 8)
                    appendLang(es.language, lang(d + k));
                break;
            case 0x6A:
                es.kind = "Audio";
                es.codec = "AC-3";
                break;
            case 0x7A:
                es.kind = "Audio";
                es.codec = "E-AC-3";
                break;
            case 0x7B:
                es.kind = "Audio";
                es.codec = "DTS";
                break;
            case 0x7C:
                es.kind = "Audio";
                es.codec = "AAC";
                break;
            case 0x7F:
                if (dl >= 1) {
                    if (d[0] == 0x15) {
                        es.kind = "Audio";
                        es.codec = "AC-4";
                    } else if (d[0] == 0x0E) {
                        es.kind = "Audio";
                        es.codec = "DTS-HD";
                    }
                }
                break;
            case 0x05:
                if (dl >= 4) {
                    const std::string fmt(reinterpret_cast<const char *>(d), 4);
                    if (fmt == "AC-3") {
                        es.kind = "Audio";
                        es.codec = "AC-3";
                    } else if (fmt == "EAC3") {
                        es.kind = "Audio";
                        es.codec = "E-AC-3";
                    } else if (fmt == "HEVC") {
                        es.kind = "Video";
                        es.codec = "HEVC";
                    } else if (fmt == "Opus") {
                        es.kind = "Audio";
                        es.codec = "Opus";
                    } else if (fmt == "KLVA") {
                        es.kind = "Data";
                        es.codec = "KLV metadata";
                    }
                }
                break;
            case 0x66:
                es.kind = "Data";
                es.codec = "Data broadcast";
                break;
            default:
                break;
            }
        });
        prog.streams.push_back(es);
        i += 5 + esLen;
    }
}

void TsTableAnalyzer::parseNitBat(const uint8_t *s, size_t len, bool bat, int version)
{
    const uint16_t id = u16(s + 3);
    const size_t end = len - 4;
    const size_t descLen = u16(s + 8) & 0x0FFF;
    if (10 + descLen + 2 > end)
        return;
    std::string name;
    forEachDescriptor(s + 10, descLen, [&](uint8_t tag, const uint8_t *d, size_t dl) {
        if ((tag == 0x40 && !bat) || (tag == 0x47 && bat))
            name = text(d, dl);
    });

    std::vector<TransportInfo> transports;
    size_t i = 10 + descLen;
    const size_t loopLen = u16(s + i) & 0x0FFF;
    i += 2;
    const size_t loopEnd = std::min(end, i + loopLen);
    while (i + 6 <= loopEnd) {
        TransportInfo t;
        t.tsid = u16(s + i);
        t.onid = u16(s + i + 2);
        const size_t tdl = u16(s + i + 4) & 0x0FFF;
        if (i + 6 + tdl > loopEnd)
            break;
        std::string s2extra;
        forEachDescriptor(s + i + 6, tdl, [&](uint8_t tag, const uint8_t *d, size_t dl) {
            switch (tag) {
            case 0x43:
                t.delivery = satelliteDelivery(d, dl);
                break;
            case 0x79:
                s2extra = s2Delivery(d, dl);
                break;
            case 0x44:
                if (t.delivery.empty())
                    t.delivery = "DVB-C";
                break;
            case 0x5A:
                if (t.delivery.empty())
                    t.delivery = "DVB-T";
                break;
            case 0x41:
                for (size_t k = 0; k + 3 <= dl; k += 3)
                    t.serviceIds.push_back(u16(d + k));
                break;
            default:
                break;
            }
        });
        t.delivery += s2extra;
        transports.push_back(t);
        i += 6 + tdl;
    }

    auto merge = [&](std::vector<TransportInfo> &dst) {
        for (const auto &t : transports) {
            auto it = std::find_if(dst.begin(), dst.end(), [&](const TransportInfo &x) { return x.tsid == t.tsid && x.onid == t.onid; });
            if (it == dst.end())
                dst.push_back(t);
            else
                *it = t;
        }
    };
    if (bat) {
        BouquetInfo &b = m_bouquets[id];
        b.bouquetId = id;
        if (b.version != version)
            b.transports.clear();
        b.version = version;
        if (!name.empty())
            b.name = name;
        merge(b.transports);
    } else {
        const bool actual = s[0] == 0x40;
        NetworkInfo &n = m_networks[{!actual, id}];
        n.networkId = id;
        n.actual = actual;
        if (n.version != version)
            n.transports.clear();
        n.version = version;
        if (!name.empty())
            n.name = name;
        merge(n.transports);
    }
}

void TsTableAnalyzer::parseSdt(const uint8_t *s, size_t len)
{
    const bool actual = s[0] == 0x42;
    const uint16_t tsid = u16(s + 3);
    const uint16_t onid = u16(s + 8);
    if (actual)
        m_onid = onid;
    const size_t end = len - 4;
    size_t i = 11;
    while (i + 5 <= end) {
        const uint16_t sid = u16(s + i);
        const uint64_t key = (static_cast<uint64_t>(onid) << 32) | (static_cast<uint64_t>(tsid) << 16) | sid;
        ServiceInfo &svc = m_services[key];
        svc.serviceId = sid;
        svc.tsid = tsid;
        svc.onid = onid;
        svc.actual = actual;
        svc.eitSchedule = s[i + 2] & 0x02;
        svc.eitPresentFollowing = s[i + 2] & 0x01;
        svc.runningStatus = s[i + 3] >> 5;
        svc.scrambled = s[i + 3] & 0x10;
        const size_t dl = u16(s + i + 3) & 0x0FFF;
        if (i + 5 + dl > end)
            break;
        forEachDescriptor(s + i + 5, dl, [&](uint8_t tag, const uint8_t *d, size_t l) {
            if (tag != 0x48 || l < 3)
                return;
            svc.type = d[0];
            svc.typeName = serviceTypeName(d[0]);
            const size_t pl = d[1];
            if (2 + pl + 1 > l)
                return;
            svc.provider = text(d + 2, pl);
            const size_t nl = d[2 + pl];
            if (3 + pl + nl <= l)
                svc.name = text(d + 3 + pl, nl);
        });
        i += 5 + dl;
    }
}

void TsTableAnalyzer::parseEit(const uint8_t *s, size_t len)
{
    const uint8_t tableId = s[0];
    if (len < 18)
        return;
    const uint16_t sid = u16(s + 3);
    const uint16_t tsid = u16(s + 8);
    const uint16_t onid = u16(s + 10);
    const uint8_t sectionNumber = s[6];
    const size_t end = len - 4;
    const bool pf = tableId == 0x4E || tableId == 0x4F;

    size_t i = 14;
    int index = 0;
    while (i + 12 <= end) {
        EventInfo ev;
        ev.valid = true;
        ev.eventId = u16(s + i);
        ev.startUtc = dvbTime(s + i + 2);
        ev.durationSec = bcdDuration(s + i + 7);
        ev.runningStatus = s[i + 10] >> 5;
        const size_t dl = u16(s + i + 10) & 0x0FFF;
        if (i + 12 + dl > end)
            break;
        if (pf) {
            forEachDescriptor(s + i + 12, dl, [&](uint8_t tag, const uint8_t *d, size_t l) {
                if (tag != 0x4D || l < 5 || !ev.name.empty())
                    return;
                ev.language = lang(d);
                const size_t nl = d[3];
                if (4 + nl + 1 > l)
                    return;
                ev.name = text(d + 4, nl);
                const size_t tl = d[4 + nl];
                if (5 + nl + tl <= l)
                    ev.text = text(d + 5 + nl, tl);
            });
            const uint64_t key = (static_cast<uint64_t>(onid) << 32) | (static_cast<uint64_t>(tsid) << 16) | sid;
            ServiceInfo &svc = m_services[key];
            svc.serviceId = sid;
            svc.tsid = tsid;
            svc.onid = onid;
            if (svc.name.empty() && svc.typeName.empty())
                svc.actual = tableId == 0x4E;
            // Section 0 carries the present event, section 1 the following one
            if (sectionNumber + index == 0)
                svc.present = ev;
            else if (sectionNumber + index == 1)
                svc.following = ev;
        } else {
            const uint64_t key = (static_cast<uint64_t>(onid) << 48) ^ (static_cast<uint64_t>(sid) << 16) ^ ev.eventId
                ^ (static_cast<uint64_t>(tsid) << 32);
            if (m_scheduleSeen.size() < kMaxScheduleEvents && m_scheduleSeen.insert(key).second)
                ++m_scheduleEvents;
        }
        ++index;
        i += 12 + dl;
    }
}

void TsTableAnalyzer::parseTime(const uint8_t *s, size_t len)
{
    if (len < 8)
        return;
    const int64_t t = dvbTime(s + 3);
    if (t > 0) {
        m_hasTime = true;
        m_utc = t;
    }
}

void TsTableAnalyzer::rebuildPidUsage(TsReport &r) const
{
    auto &u = r.pidUsage;
    u[kPidPat] = "PAT";
    if (m_tableStats.count({0x01, kPidCat}))
        u[kPidCat] = "CAT";
    u[kPidNit] = "NIT";
    u[kPidSdt] = "SDT / BAT";
    u[kPidEit] = "EIT";
    u[kPidTdt] = "TDT / TOT";
    u[0x1FFF] = "Null packets";

    auto serviceName = [&](uint16_t program) -> std::string {
        for (const auto &s : r.services)
            if (s.actual && s.serviceId == program && s.tsid == r.tsid && !s.name.empty())
                return s.name;
        return "program " + std::to_string(program);
    };
    auto add = [&](uint16_t pid, const std::string &what) {
        auto it = u.find(pid);
        if (it == u.end())
            u[pid] = what;
        else if (it->second.find(what) == std::string::npos && it->second.size() < 200)
            it->second += " | " + what;
    };
    for (const auto &p : r.programs) {
        const std::string name = serviceName(p.programNumber);
        add(p.pmtPid, "PMT · " + name);
        bool pcrIsEs = false;
        for (const auto &es : p.streams) {
            add(es.pid, es.kind + " " + es.codec + (es.language.empty() ? "" : " (" + es.language + ")") + " · " + name);
            pcrIsEs = pcrIsEs || es.pid == p.pcrPid;
            for (const auto &ca : es.ca)
                add(ca.pid, "ECM " + ca.systemName + " · " + name);
        }
        if (!pcrIsEs && p.pcrPid != 0x1FFF)
            add(p.pcrPid, "PCR · " + name);
        for (const auto &ca : p.ca)
            add(ca.pid, "ECM " + ca.systemName + " · " + name);
    }
    for (const auto &e : r.emm)
        add(e.pid, "EMM " + e.systemName);
}

TsReport TsTableAnalyzer::report() const
{
    TsReport r;
    r.patSeen = m_patSeen;
    r.tsid = m_tsid;
    r.patVersion = m_patVersion;
    r.onid = m_onid;
    for (const auto &p : m_programs)
        r.programs.push_back(p.second);
    for (const auto &s : m_services)
        r.services.push_back(s.second);
    std::stable_sort(r.services.begin(), r.services.end(), [](const ServiceInfo &a, const ServiceInfo &b) {
        if (a.actual != b.actual)
            return a.actual;
        return std::tie(a.onid, a.tsid, a.serviceId) < std::tie(b.onid, b.tsid, b.serviceId);
    });
    for (const auto &n : m_networks)
        r.networks.push_back(n.second);
    for (const auto &b : m_bouquets)
        r.bouquets.push_back(b.second);
    r.emm = m_emm;
    r.hasTime = m_hasTime;
    r.utcTime = m_utc;
    for (const auto &t : m_tableStats)
        r.tables.push_back(t.second);
    r.sections = m_sections;
    r.crcErrors = m_crcErrors;
    r.scheduleEvents = m_scheduleEvents;
    rebuildPidUsage(r);
    return r;
}

} // namespace bb
} // namespace tbs
