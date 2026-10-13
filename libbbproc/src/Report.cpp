#include "tbsbbproc/Processor.h"

#include <cstdio>
#include <ctime>
#include <cstdarg>
#include <algorithm>

namespace tbs {
namespace bb {

namespace {

std::string fmt(const char *f, ...)
#if defined(__GNUC__)
    __attribute__((format(printf, 1, 2)))
#endif
    ;

std::string fmt(const char *f, ...)
{
    char buf[1024];
    va_list ap;
    va_start(ap, f);
    std::vsnprintf(buf, sizeof(buf), f, ap);
    va_end(ap);
    return buf;
}

std::string utc(int64_t t)
{
    if (t <= 0)
        return "-";
    const std::time_t tt = static_cast<std::time_t>(t);
    std::tm tm{};
#if defined(_WIN32)
    gmtime_s(&tm, &tt);
#else
    gmtime_r(&tt, &tm);
#endif
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm);
    return buf;
}

std::string bytes(uint64_t b)
{
    if (b >= 1ull << 30)
        return fmt("%.2f GiB", b / double(1ull << 30));
    if (b >= 1ull << 20)
        return fmt("%.2f MiB", b / double(1ull << 20));
    if (b >= 1ull << 10)
        return fmt("%.1f KiB", b / 1024.0);
    return fmt("%llu B", static_cast<unsigned long long>(b));
}

} // namespace

std::string formatReport(const ProcessorSnapshot &s, bool verbose)
{
    std::string o;
    for (const auto &st : s.streams) {
        o += "==== Stream " + (st.isi < 0 ? std::string("(single)") : "ISI " + std::to_string(st.isi)) + ": "
            + streamContentName(st.content) + " ====\n";
        if (st.frames)
            o += fmt("BBFrames %llu, data %s%s\n", static_cast<unsigned long long>(st.frames), bytes(st.dataBytes).c_str(),
                     st.hasHeader ? (", MATYPE " + st.lastHeader.summary()).c_str() : "");

        if (st.content == StreamContent::TransportStream || st.content == StreamContent::TsInput) {
            const TsReport &t = st.ts;
            o += fmt("TS packets %llu, TSID %u, ONID %u, sections %llu, CRC errors %llu, UTC %s\n",
                     static_cast<unsigned long long>(st.tsPackets), t.tsid, t.onid,
                     static_cast<unsigned long long>(t.sections), static_cast<unsigned long long>(t.crcErrors),
                     t.hasTime ? utc(t.utcTime).c_str() : "-");
            if (t.epgEvents)
                o += fmt("EPG: %llu events (%llu from the EIT schedule)\n", static_cast<unsigned long long>(t.epgEvents),
                         static_cast<unsigned long long>(t.scheduleEvents));
            for (const auto &n : t.networks)
                o += fmt("Network %u%s \"%s\": %u transport streams\n", n.networkId, n.actual ? "" : " (other)",
                         n.name.c_str(), static_cast<unsigned>(n.transports.size()));
            for (const auto &p : t.programs) {
                std::string name, provider, type;
                for (const auto &sv : t.services)
                    if (sv.actual && sv.serviceId == p.programNumber) {
                        name = sv.name;
                        provider = sv.provider;
                        type = sv.typeName;
                    }
                o += fmt("  Service %5u  %-28s %-16s %-18s PMT 0x%04X PCR 0x%04X\n", p.programNumber, name.c_str(),
                         provider.c_str(), type.c_str(), p.pmtPid, p.pcrPid);
                for (const auto &es : p.streams)
                    o += fmt("      0x%04X  %-10s %s%s%s\n", es.pid, es.kind.c_str(), es.codec.c_str(),
                             es.language.empty() ? "" : " ", es.language.c_str());
            }
            if (verbose)
                for (const auto &tb : t.tables)
                    o += fmt("  Table %-20s PID 0x%04X v%-3d sections %llu CRC errors %llu\n", tb.name.c_str(), tb.pid,
                             tb.version, static_cast<unsigned long long>(tb.sections),
                             static_cast<unsigned long long>(tb.crcErrors));
        }

        if (st.gse.dataFields) {
            const GseStats &g = st.gse;
            o += fmt("GSE: data fields %llu (valid %llu), packets %llu, PDUs %llu (%s), reassembled %llu, "
                     "CRC errors %llu, length errors %llu, fragment errors %llu\n",
                     static_cast<unsigned long long>(g.dataFields), static_cast<unsigned long long>(g.validDataFields),
                     static_cast<unsigned long long>(g.packets), static_cast<unsigned long long>(g.pdus),
                     bytes(g.pduBytes).c_str(), static_cast<unsigned long long>(g.reassembled),
                     static_cast<unsigned long long>(g.crcErrors), static_cast<unsigned long long>(g.lengthErrors),
                     static_cast<unsigned long long>(g.fragmentErrors));
        }
        if (st.mpe.sections) {
            const MpeStats &m = st.mpe;
            std::string pids;
            for (uint16_t p : m.pids)
                pids += fmt(" 0x%04X", p);
            o += fmt("MPE: PIDs%s, sections %llu, datagrams %llu (LLC/SNAP %llu), CRC errors %llu, scrambled %llu\n",
                     pids.c_str(), static_cast<unsigned long long>(m.sections), static_cast<unsigned long long>(m.datagrams),
                     static_cast<unsigned long long>(m.llcSnap), static_cast<unsigned long long>(m.crcErrors),
                     static_cast<unsigned long long>(m.scrambled));
        }
        if (st.ip.pdus) {
            const IpReport &ip = st.ip;
            o += fmt("IP: IPv4 %llu, IPv6 %llu, non-IP %llu, malformed %llu, %s, flows %u\n",
                     static_cast<unsigned long long>(ip.ipv4), static_cast<unsigned long long>(ip.ipv6),
                     static_cast<unsigned long long>(ip.nonIp), static_cast<unsigned long long>(ip.malformed),
                     bytes(ip.ipBytes).c_str(), static_cast<unsigned>(ip.flowCount));
            for (const auto &p : ip.protocols)
                o += fmt("  %-12s %10llu packets %12s\n", p.name.c_str(), static_cast<unsigned long long>(p.packets),
                         bytes(p.bytes).c_str());
            const size_t n = verbose ? ip.topFlows.size() : std::min<size_t>(ip.topFlows.size(), 10);
            for (size_t i = 0; i < n; ++i) {
                const FlowStat &f = ip.topFlows[i];
                o += fmt("  %-6s %s:%u -> %s:%u  %llu packets, %s\n", f.protocolName.c_str(), f.src.c_str(), f.srcPort,
                         f.dst.c_str(), f.dstPort, static_cast<unsigned long long>(f.packets), bytes(f.bytes).c_str());
            }
        }
    }
    if (!s.pcapFile.empty())
        o += fmt("pcap: %s, %llu packets, %s%s\n", s.pcapFile.c_str(), static_cast<unsigned long long>(s.pcapPackets),
                 bytes(s.pcapBytes).c_str(), s.pcapError.empty() ? "" : (" (error: " + s.pcapError + ")").c_str());
    if (!s.tunName.empty() || !s.tunError.empty())
        o += s.tunError.empty() ? fmt("tun: %s, %llu packets, %llu errors\n", s.tunName.c_str(),
                                      static_cast<unsigned long long>(s.tunPackets), static_cast<unsigned long long>(s.tunErrors))
                                : "tun: error: " + s.tunError + "\n";
    return o;
}

} // namespace bb
} // namespace tbs
