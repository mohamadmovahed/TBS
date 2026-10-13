// Generates test input for the receiver's "File replay" device and for tbs-bbproc.
//
//   tbs-make-test-stream --out FILE [--in input.ts] [--add-si] [--dfl BYTES] [--hem] [--packets N]
//       Wraps a transport stream (input file, or a synthetic one) into DVB-S2 BBFrames.
//       --add-si inserts NIT, EIT present/following and a two-day schedule, TDT, CAT and BAT tables.
//   tbs-make-test-stream --out FILE --gse [--hem] [--pdus N]
//       Generic stream with GSE-encapsulated IP traffic (UDP multicast, TCP, IPv6, ARP).
//   tbs-make-test-stream --out FILE --mpe [--in input.ts] [--pdus N]
//       Adds MPE IP traffic (PID 0x0500) to the transport stream before wrapping it.
//   tbs-make-test-stream --out FILE --mis --in input.ts
//       Multistream carrier: the TS on ISI 1 and GSE IP traffic on ISI 2.
//
// The output is STiD135 pseudo-TS (PID 0x010E), i.e. what a TBS 6903x delivers with
// "options stid135 bbframe=1". Add --raw to write raw BBFrames instead.

#include "../tests/StreamBuilders.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>
#include <iostream>
#include <string>

using builders::Bytes;
using tbs::BbHeader;

namespace {

// --- SI tables added with --add-si (match the program/TS ids of the input) ---
std::vector<Bytes> siPackets(uint16_t tsid, uint16_t onid, uint16_t program, std::time_t now, uint8_t (&cc)[8])
{
    using namespace builders;
    std::vector<Bytes> pk;
    auto add = [&](const Bytes &sec, uint16_t pid, int ccIndex) {
        for (auto &p : sectionToTs(sec, pid, cc[ccIndex]))
            pk.push_back(p);
    };
    auto bcd = [](int v) { return static_cast<uint8_t>(((v / 10) << 4) | (v % 10)); };
    auto mjdTime = [&](std::time_t t, Bytes &b) {
        const long secs = static_cast<long>(t % 86400);
        put16(b, static_cast<uint16_t>(40587 + t / 86400));
        b.push_back(bcd(static_cast<int>(secs / 3600)));
        b.push_back(bcd(static_cast<int>(secs / 60 % 60)));
        b.push_back(bcd(static_cast<int>(secs % 60)));
    };

    // NIT actual: network name + satellite delivery for this TS + service list
    Bytes nit, nd;
    addDescriptor(nd, 0x40, dvbString("TBS Demo Network"));
    put16(nit, static_cast<uint16_t>(0xF000 | nd.size()));
    nit.insert(nit.end(), nd.begin(), nd.end());
    Bytes ts;
    put16(ts, tsid);
    put16(ts, onid);
    Bytes td;
    // 11494.000 MHz, 13.0E, H, DVB-S2 8PSK, RO 0.20, 27.5 MS/s, 3/4
    addDescriptor(td, 0x43, {0x01, 0x14, 0x94, 0x00, 0x01, 0x30, 0x80 | 0x10 | 0x04 | 0x02, 0x02, 0x75, 0x00, 0x03});
    addDescriptor(td, 0x41, {static_cast<uint8_t>(program >> 8), static_cast<uint8_t>(program), 0x19});
    put16(ts, static_cast<uint16_t>(0xF000 | td.size()));
    ts.insert(ts.end(), td.begin(), td.end());
    // Other transports of the network (for the NIT scan): 11053 H, 11817 H, 12187 V at 13.0E
    auto other = [&](uint16_t id, const Bytes &sat) {
        put16(ts, id);
        put16(ts, onid);
        Bytes od;
        addDescriptor(od, 0x43, sat);
        put16(ts, static_cast<uint16_t>(0xF000 | od.size()));
        ts.insert(ts.end(), od.begin(), od.end());
    };
    other(static_cast<uint16_t>(tsid + 1), {0x01, 0x10, 0x53, 0x00, 0x01, 0x30, 0x80 | 0x10 | 0x04 | 0x02, 0x02, 0x75, 0x00, 0x02});
    other(static_cast<uint16_t>(tsid + 2), {0x01, 0x18, 0x17, 0x00, 0x01, 0x30, 0x80 | 0x10 | 0x04 | 0x02, 0x02, 0x75, 0x00, 0x03});
    other(static_cast<uint16_t>(tsid + 3), {0x01, 0x21, 0x87, 0x00, 0x01, 0x30, 0x80 | 0x20 | 0x04 | 0x01, 0x02, 0x75, 0x00, 0x03});
    put16(nit, static_cast<uint16_t>(0xF000 | ts.size()));
    nit.insert(nit.end(), ts.begin(), ts.end());
    add(makeSection(0x40, 0x0100, 1, nit), 0x0010, 0);

    // BAT: bouquet with a Persian (ISO 8859-6) name
    Bytes bat, bd;
    addDescriptor(bd, 0x47, {0x02, 0xC8, 0xD3, 0xCA, 0xE7}); // "بستك"-style sample text
    put16(bat, static_cast<uint16_t>(0xF000 | bd.size()));
    bat.insert(bat.end(), bd.begin(), bd.end());
    put16(bat, 0xF000);
    add(makeSection(0x4A, 0x0042, 1, bat), 0x0011, 1);

    const std::time_t slot = now - now % 1800;

    // EIT schedule actual: two days of programmes with extended text, genre and rating
    {
        struct Show
        {
            const char *name;
            const char *text;
            uint8_t genre;
            uint32_t duration;
            uint8_t rating;
        };
        static const Show shows[] = {
            {"Morning Briefing", "News, weather and traffic", 0x21, 3600, 0},
            {"Wild Coasts", "Seabirds of the northern cliffs", 0x91, 2700, 0},
            {"Final Whistle", "Football highlights and analysis", 0x43, 5400, 0},
            {"The Last Signal", "A radio engineer uncovers a conspiracy", 0x11, 6300, 9},
            {"Kitchen Lab", "Bread from four ingredients", 0xA5, 1800, 0},
            {"Late Jazz", "Live from the club", 0x64, 3600, 0},
        };
        std::vector<Bytes> events;
        std::vector<builders::EitEventSpec> specs;
        std::time_t t = slot - 2 * 3600;
        uint16_t id = 2000;
        size_t k = 0;
        while (t < slot + 2 * 86400) {
            builders::EitEventSpec e;
            e.id = id++;
            e.start = t;
            if (k % 7 == 6) {
                // Arabic script (ISO 8859-6, selector 0x02), extended text split over two descriptors
                e.lang = "per";
                e.name = {0x02, 0xC7, 0xCE, 0xC8, 0xC7, 0xD1};                  // news
                e.text = {0x02, 0xC7, 0xCE, 0xC8, 0xC7, 0xD1, 0x20, 0xE4, 0xCA}; // short text
                e.extended = {{0x02, 0xC7, 0xCE, 0xC8, 0xC7, 0xD1, 0x20}, {0x02, 0xE4, 0xCA, 0xC7}};
                e.content = {0x20};
                e.duration = 1800;
            } else {
                const Show &sh = shows[k % 6];
                e.name = dvbString(sh.name);
                e.text = dvbString(sh.text);
                e.duration = sh.duration;
                e.content = {sh.genre};
                e.rating = sh.rating;
                if (sh.genre == 0x11) {
                    e.extended = {dvbString("When a numbers station goes silent, Mara follows the last "),
                                  dvbString("transmission across three countries.")};
                    e.items = {{"Director", "A. Rahimi"}, {"Year", "2024"}};
                }
            }
            events.push_back(builders::eitEvent(e));
            specs.push_back(e);
            t += e.duration;
            ++k;
        }
        const size_t perSection = 4;
        const size_t sections = (events.size() + perSection - 1) / perSection;
        for (size_t sn = 0; sn < sections; ++sn) {
            std::vector<Bytes> part(events.begin() + static_cast<std::ptrdiff_t>(sn * perSection),
                                    events.begin() + static_cast<std::ptrdiff_t>(std::min(events.size(), (sn + 1) * perSection)));
            add(builders::makeEitSection(0x50, program, tsid, onid, 1, static_cast<uint8_t>(sn),
                                         static_cast<uint8_t>(sections - 1), part),
                0x0012, 2);
        }
        // EIT p/f actual: the programme running now and the next one, from the same schedule
        for (size_t i = 0; i + 1 < specs.size(); ++i) {
            if (specs[i].start <= now && now < specs[i].start + specs[i].duration) {
                builders::EitEventSpec present = specs[i], following = specs[i + 1];
                present.running = 4;
                add(builders::makeEitSection(0x4E, program, tsid, onid, 1, 0, 1, {builders::eitEvent(present)}), 0x0012, 2);
                add(builders::makeEitSection(0x4E, program, tsid, onid, 1, 1, 1, {builders::eitEvent(following)}), 0x0012, 2);
                break;
            }
        }
    }

    // CAT: an EMM PID
    Bytes cat;
    addDescriptor(cat, 0x09, {0x06, 0x04, 0xE7, 0x00});
    add(makeSection(0x01, 0xFFFF, 1, cat), 0x0001, 3);

    // TDT
    Bytes tdt = {0x70, 0x70, 0x05};
    mjdTime(now, tdt);
    add(tdt, 0x0014, 4);
    return pk;
}

std::vector<Bytes> withSi(const std::vector<Bytes> &in, double secondsSpan)
{
    // Program / TS ids from the input's PAT
    uint16_t tsid = 1, program = 1, onid = 0xFF01;
    for (const Bytes &p : in) {
        if (((p[1] & 0x1F) << 8 | p[2]) == 0 && (p[1] & 0x40) && p[4] == 0 && p[5] == 0x00) {
            tsid = static_cast<uint16_t>((p[8] << 8) | p[9]);
            program = static_cast<uint16_t>((p[13] << 8) | p[14]);
            break;
        }
    }
    std::vector<Bytes> out;
    uint8_t cc[8] = {};
    const std::time_t t0 = std::time(nullptr);
    const size_t every = 2000;
    for (size_t i = 0; i < in.size(); ++i) {
        if (i % every == 0) {
            const std::time_t now = t0 + static_cast<std::time_t>(secondsSpan * i / std::max<size_t>(1, in.size()));
            for (auto &p : siPackets(tsid, onid, program, now, cc))
                out.push_back(p);
        }
        out.push_back(in[i]);
    }
    return out;
}

// --- GSE traffic ---
std::vector<Bytes> gseTraffic(size_t pdus)
{
    std::vector<Bytes> g;
    const uint8_t mcastMac[6] = {0x01, 0x00, 0x5e, 0x01, 0x01, 0x01};
    const uint8_t hostMac[6] = {0x02, 0x11, 0x22, 0x33, 0x44, 0x55};
    const uint8_t src[4] = {10, 20, 0, 1};
    const uint8_t mcast[4] = {239, 1, 1, 1};
    const uint8_t mcast2[4] = {239, 1, 1, 2};
    const uint8_t host[4] = {192, 168, 10, 50};
    const uint8_t dns[4] = {8, 8, 8, 8};
    uint8_t s6[16] = {0x20, 0x01, 0x0d, 0xb8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x10};
    uint8_t d6[16] = {0xff, 0x0e, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x42};
    uint8_t frag = 0;
    for (size_t i = 0; i < pdus; ++i) {
        const int kind = static_cast<int>(i % 20);
        Bytes ip;
        uint16_t type = 0x0800;
        uint8_t lt = 0;
        const uint8_t *label = mcastMac;
        if (kind < 9) {
            ip = builders::makeIpv4(17, src, mcast, 5000, 5000, 1316, static_cast<uint8_t>(i)); // MPEG-TS over UDP
        } else if (kind < 12) {
            ip = builders::makeIpv4(17, src, mcast2, 5004, 5004, 1200, static_cast<uint8_t>(i));
        } else if (kind < 17) {
            ip = builders::makeIpv4(6, src, host, 443, 51544, 1440, static_cast<uint8_t>(i)); // download
            label = hostMac;
            lt = kind % 2 ? 3 : 0;
        } else if (kind == 17) {
            ip = builders::makeIpv4(17, dns, host, 53, 40000, 120, static_cast<uint8_t>(i));
            label = hostMac;
        } else if (kind == 18) {
            ip = builders::makeIpv6Udp(s6, d6, 6000, 6000, 900);
            type = 0x86DD;
            lt = 2; // broadcast label
        } else {
            // ARP request (non-IP)
            ip = {0x00, 0x01, 0x08, 0x00, 0x06, 0x04, 0x00, 0x01, 0x02, 0x11, 0x22, 0x33, 0x44, 0x55,
                  10, 20, 0, 1, 0, 0, 0, 0, 0, 0, 192, 168, 10, 50};
            type = 0x0806;
            lt = 2;
        }
        // Large PDUs are fragmented so the stream exercises reassembly
        for (auto &p : builders::gseEncapsulate(ip, type, lt, label, 900, frag++))
            g.push_back(p);
    }
    return g;
}

// --- MPE traffic interleaved into a TS ---
std::vector<Bytes> withMpe(const std::vector<Bytes> &in, size_t datagrams)
{
    const uint8_t mcastMac[6] = {0x01, 0x00, 0x5e, 0x02, 0x02, 0x02};
    const uint8_t hostMac[6] = {0x02, 0x00, 0xc0, 0xa8, 0x14, 0x07};
    const uint8_t src[4] = {172, 16, 0, 1};
    const uint8_t grp[4] = {239, 2, 2, 2};
    const uint8_t host[4] = {192, 168, 20, 7};
    std::vector<Bytes> mpe;
    uint8_t cc = 0;
    for (size_t i = 0; i < datagrams; ++i) {
        const bool tcp = i % 4 == 3;
        Bytes ip = builders::makeIpv4(tcp ? 6 : 17, src, tcp ? host : grp, tcp ? 80 : 7000, tcp ? 40100 : 7000,
                                      tcp ? 1400 : 1000, static_cast<uint8_t>(i));
        for (auto &p : builders::sectionToTs(builders::makeMpeSection(tcp ? hostMac : mcastMac, ip, i % 8 == 0), 0x0500, cc))
            mpe.push_back(p);
    }
    std::vector<Bytes> out;
    const size_t total = in.size() + mpe.size();
    size_t a = 0, b = 0;
    for (size_t i = 0; i < total; ++i) {
        // spread the MPE packets evenly through the input
        if (b < mpe.size() && (a >= in.size() || b * in.size() <= a * mpe.size()))
            out.push_back(mpe[b++]);
        else
            out.push_back(in[a++]);
    }
    return out;
}

std::vector<Bytes> readTs(const std::string &in)
{
    std::vector<Bytes> packets;
    std::ifstream f(in, std::ios::binary);
    if (!f) {
        std::cerr << "cannot open " << in << "\n";
        std::exit(1);
    }
    Bytes p(188);
    while (f.read(reinterpret_cast<char *>(p.data()), 188)) {
        if (p[0] != 0x47) {
            std::cerr << "input is not aligned 188-byte TS\n";
            std::exit(1);
        }
        packets.push_back(p);
    }
    return packets;
}

} // namespace

int main(int argc, char **argv)
{
    std::string in;
    std::string out;
    size_t dfl = 7274 - 10;
    bool hem = false;
    bool gse = false;
    bool mis = false;
    bool addSi = false;
    bool mpe = false;
    bool raw = false;
    size_t synthPackets = 20000;
    size_t pdus = 20000;

    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto next = [&]() -> std::string {
            if (i + 1 >= argc) {
                std::cerr << "missing value for " << a << "\n";
                std::exit(2);
            }
            return argv[++i];
        };
        if (a == "--in")
            in = next();
        else if (a == "--out")
            out = next();
        else if (a == "--dfl")
            dfl = std::stoul(next());
        else if (a == "--hem")
            hem = true;
        else if (a == "--gse")
            gse = true;
        else if (a == "--mis")
            mis = true;
        else if (a == "--add-si")
            addSi = true;
        else if (a == "--mpe")
            mpe = true;
        else if (a == "--raw")
            raw = true;
        else if (a == "--packets")
            synthPackets = std::stoul(next());
        else if (a == "--pdus")
            pdus = std::stoul(next());
        else {
            out.clear();
            break;
        }
    }
    if (out.empty() || dfl < 16 || dfl > 7264) {
        std::cerr << "usage: " << argv[0]
                  << " --out FILE [--in input.ts] [--add-si] [--mpe] [--gse] [--mis] [--hem] [--dfl 16..7264] [--packets N]"
                     " [--pdus N] [--raw]\n";
        return 2;
    }

    std::vector<Bytes> frames;
    auto tsFrames = [&]() {
        std::vector<Bytes> packets;
        if (!in.empty()) {
            packets = readTs(in);
        } else {
            for (size_t i = 0; i < synthPackets; ++i)
                packets.push_back(builders::makeTsPacket(0x0100, static_cast<uint8_t>(i), static_cast<uint8_t>(i * 7)));
        }
        if (addSi || mis)
            packets = withSi(packets, 20.0);
        if (mpe)
            packets = withMpe(packets, pdus);
        std::cout << packets.size() << " TS packets";
        return builders::tsToBbFrames(packets, dfl, hem);
    };

    if (mis) {
        auto ts = tsFrames();
        auto g = builders::gseToBbFrames(gseTraffic(pdus), dfl, false, 2);
        for (auto &f : ts) {
            f[0] = static_cast<uint8_t>(f[0] & ~0x20); // MIS
            f[1] = 1;                                  // ISI 1
            f[9] = static_cast<uint8_t>(BbHeader::crc8(f.data(), 9) ^ (hem ? 1 : 0));
        }
        // Interleave in proportion to the frame counts
        size_t gi = 0;
        for (size_t i = 0; i < ts.size(); ++i) {
            frames.push_back(ts[i]);
            const size_t want = (i + 1) * g.size() / ts.size();
            while (gi < want)
                frames.push_back(g[gi++]);
        }
        while (gi < g.size())
            frames.push_back(g[gi++]);
        std::cout << " (ISI 1) + " << pdus << " GSE PDUs (ISI 2)";
    } else if (gse) {
        frames = builders::gseToBbFrames(gseTraffic(pdus), dfl, hem);
        std::cout << pdus << " GSE PDUs";
    } else {
        frames = tsFrames();
    }

    std::ofstream o(out, std::ios::binary);
    if (!o) {
        std::cerr << "cannot create " << out << "\n";
        return 1;
    }
    uint8_t cc = 0;
    size_t tsOut = 0;
    for (const Bytes &fr : frames) {
        if (raw) {
            o.write(reinterpret_cast<const char *>(fr.data()), static_cast<std::streamsize>(fr.size()));
            continue;
        }
        for (const Bytes &p : builders::bbFrameToPseudoTs(fr, cc)) {
            o.write(reinterpret_cast<const char *>(p.data()), 188);
            ++tsOut;
        }
    }
    std::cout << " -> " << frames.size() << " BBFrames (" << (hem ? "HEM" : "NM") << ", DFL " << dfl * 8 << " bits)";
    if (!raw)
        std::cout << " -> " << tsOut << " pseudo-TS packets";
    std::cout << "\n";
    return 0;
}
