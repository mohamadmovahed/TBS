// Tests for the tbsbbproc library: DVB text, PSI/SI tables, GSE, IP statistics,
// pcap output, stream content detection and the capture file reader.

#include "StreamBuilders.h"

#include "tbsbbproc/CaptureFile.h"
#include "tbsbbproc/DvbText.h"
#include "tbsbbproc/Gse.h"
#include "tbsbbproc/Processor.h"
#include "tbsbbproc/TsTableAnalyzer.h"

#include <cstdio>
#include <cstring>
#include <string>

using builders::Bytes;
using namespace tbs::bb;

static int g_failures = 0;

#define CHECK(cond)                                                                       \
    do {                                                                                  \
        if (!(cond)) {                                                                    \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
            ++g_failures;                                                                 \
        }                                                                                 \
    } while (0)

#define CHECK_EQ(a, b)                                                                                       \
    do {                                                                                                     \
        const auto va = (a);                                                                                 \
        const auto vb = (b);                                                                                 \
        if (!(va == vb)) {                                                                                   \
            std::fprintf(stderr, "%s:%d: CHECK_EQ failed: %s (%lld) != %s (%lld)\n", __FILE__, __LINE__, #a, \
                         static_cast<long long>(va), #b, static_cast<long long>(vb));                        \
            ++g_failures;                                                                                    \
        }                                                                                                    \
    } while (0)

#define CHECK_STR(a, b)                                                                                                   \
    do {                                                                                                                  \
        const std::string va = (a);                                                                                       \
        const std::string vb = (b);                                                                                       \
        if (va != vb) {                                                                                                   \
            std::fprintf(stderr, "%s:%d: CHECK_STR failed: \"%s\" != \"%s\"\n", __FILE__, __LINE__, va.c_str(), vb.c_str()); \
            ++g_failures;                                                                                                 \
        }                                                                                                                 \
    } while (0)

static const uint8_t kSrc[4] = {10, 0, 0, 1};
static const uint8_t kDst[4] = {239, 1, 2, 3};
static const uint8_t kDst2[4] = {192, 168, 1, 20};
static const uint8_t kMac[6] = {0x01, 0x00, 0x5e, 0x01, 0x02, 0x03};

// ---------------------------------------------------------------------------

static void testDvbText()
{
    const uint8_t ascii[] = {'B', 'B', 'C', 0x86, ' ', 'O', 'n', 'e', 0x87};
    CHECK_STR(dvbTextToUtf8(ascii, sizeof(ascii)), "BBC One");
    // ISO 8859-6 (selector 0x02): Arabic / Persian letters
    const uint8_t arabic[] = {0x02, 0xC7, 0xE4, 0xD9};
    CHECK_STR(dvbTextToUtf8(arabic, sizeof(arabic)), "\xD8\xA7\xD9\x84\xD8\xB9"); // ا ل ع
    const uint8_t utf8[] = {0x15, 0xD8, 0xA7, 'x'};
    CHECK_STR(dvbTextToUtf8(utf8, sizeof(utf8)), "\xD8\xA7x");
    const uint8_t ucs2[] = {0x11, 0x00, 'A', 0x06, 0x27};
    CHECK_STR(dvbTextToUtf8(ucs2, sizeof(ucs2)), "A\xD8\xA7");
    const uint8_t latin[] = {0x10, 0x00, 0x01, 'C', 'a', 'f', 0xE9};
    CHECK_STR(dvbTextToUtf8(latin, sizeof(latin)), "Caf\xC3\xA9");
    const uint8_t newline[] = {'a', 0x8A, 'b'};
    CHECK_STR(dvbTextToUtf8(newline, sizeof(newline)), "a\nb");
    // CRC-32/MPEG-2 check value
    CHECK_EQ(crc32Mpeg(reinterpret_cast<const uint8_t *>("123456789"), 9), 0x0376E6E7u);
}

// ---------------------------------------------------------------------------
// PSI / SI

static std::vector<Bytes> buildSiPackets()
{
    using namespace builders;
    std::vector<Bytes> pk;
    uint8_t ccPat = 0, ccPmt = 0, ccSdt = 0, ccNit = 0, ccEit = 0, ccTdt = 0, ccCat = 0;
    auto add = [&](const Bytes &sec, uint16_t pid, uint8_t &cc) {
        for (auto &p : sectionToTs(sec, pid, cc))
            pk.push_back(p);
    };

    // PAT: program 0 -> NIT, programs 10 and 20
    Bytes pat;
    put16(pat, 0);
    put16(pat, 0xE010);
    put16(pat, 10);
    put16(pat, 0xE000 | 0x0100);
    put16(pat, 20);
    put16(pat, 0xE000 | 0x0200);
    add(makeSection(0x00, 0x0457, 3, pat), 0x0000, ccPat);

    // PMT program 10: H.264 video, AC-3 audio (fas), DVB subtitles (eng), Viaccess ECM
    Bytes pmt;
    put16(pmt, 0xE000 | 0x0101); // PCR
    Bytes pinfo;
    addDescriptor(pinfo, 0x09, {0x05, 0x00, 0xE5, 0x00}); // Viaccess, ECM PID 0x0500
    put16(pmt, static_cast<uint16_t>(0xF000 | pinfo.size()));
    pmt.insert(pmt.end(), pinfo.begin(), pinfo.end());
    pmt.push_back(0x1B);
    put16(pmt, 0xE101);
    put16(pmt, 0xF000);
    Bytes a;
    addDescriptor(a, 0x6A, {0x00});
    addDescriptor(a, 0x0A, {'f', 'a', 's', 0x00});
    pmt.push_back(0x06);
    put16(pmt, 0xE102);
    put16(pmt, static_cast<uint16_t>(0xF000 | a.size()));
    pmt.insert(pmt.end(), a.begin(), a.end());
    Bytes sub;
    addDescriptor(sub, 0x59, {'e', 'n', 'g', 0x10, 0x00, 0x01, 0x00, 0x01});
    pmt.push_back(0x06);
    put16(pmt, 0xE103);
    put16(pmt, static_cast<uint16_t>(0xF000 | sub.size()));
    pmt.insert(pmt.end(), sub.begin(), sub.end());
    add(makeSection(0x02, 10, 1, pmt), 0x0100, ccPmt);

    // SDT actual: service 10 "IRIB" in ISO 8859-6, provider "P"; service 20 radio
    Bytes sdt;
    put16(sdt, 0x0001); // ONID
    sdt.push_back(0xFF);
    auto service = [&](uint16_t sid, uint8_t type, const Bytes &provider, const Bytes &name, bool scrambled) {
        put16(sdt, sid);
        sdt.push_back(0xFC | 0x03); // EIT schedule + p/f
        Bytes d;
        Bytes sd = {type, static_cast<uint8_t>(provider.size())};
        sd.insert(sd.end(), provider.begin(), provider.end());
        sd.push_back(static_cast<uint8_t>(name.size()));
        sd.insert(sd.end(), name.begin(), name.end());
        addDescriptor(d, 0x48, sd);
        put16(sdt, static_cast<uint16_t>((4 << 13) | (scrambled ? 0x1000 : 0) | d.size()));
        sdt.insert(sdt.end(), d.begin(), d.end());
    };
    service(10, 0x19, {'P'}, {0x02, 0xC7, 0xE4, 0xD9}, true);
    service(20, 0x02, {'P'}, dvbString("Radio 20"), false);
    add(makeSection(0x42, 0x0457, 5, sdt), 0x0011, ccSdt);

    // NIT actual: "Test Net", TS 0x0457 on 11494 MHz H 27.5 MS/s DVB-S2 8PSK 3/4 at 13.0E, ISI 3
    Bytes nit;
    Bytes nd;
    addDescriptor(nd, 0x40, dvbString("Test Net"));
    put16(nit, static_cast<uint16_t>(0xF000 | nd.size()));
    nit.insert(nit.end(), nd.begin(), nd.end());
    Bytes ts;
    put16(ts, 0x0457);
    put16(ts, 0x0001);
    Bytes td;
    addDescriptor(td, 0x43, {0x01, 0x14, 0x94, 0x00, 0x01, 0x30, 0x80 | 0x00 | 0x04 | 0x02, 0x02, 0x75, 0x00, 0x03});
    addDescriptor(td, 0x79, {0x40, 0x03});
    addDescriptor(td, 0x41, {0x00, 0x0A, 0x19, 0x00, 0x14, 0x02});
    put16(ts, static_cast<uint16_t>(0xF000 | td.size()));
    ts.insert(ts.end(), td.begin(), td.end());
    put16(nit, static_cast<uint16_t>(0xF000 | ts.size()));
    nit.insert(nit.end(), ts.begin(), ts.end());
    add(makeSection(0x40, 0x0022, 2, nit), 0x0010, ccNit);

    // EIT p/f actual for service 10: present "News" 2026-10-10 12:00:00 for 30 minutes
    auto eit = [&](uint8_t section, uint16_t eventId, uint8_t hour, const std::string &name) {
        Bytes e;
        put16(e, 0x0457);
        put16(e, 0x0001);
        e.push_back(1);
        e.push_back(0x4E);
        put16(e, eventId);
        const uint16_t mjd = 40587 + 20736; // 2026-10-10
        put16(e, mjd);
        e.push_back(static_cast<uint8_t>(((hour / 10) << 4) | (hour % 10)));
        e.push_back(0x00);
        e.push_back(0x00);
        e.push_back(0x00);
        e.push_back(0x30);
        e.push_back(0x00);
        Bytes d;
        Bytes se = {'e', 'n', 'g', static_cast<uint8_t>(name.size())};
        se.insert(se.end(), name.begin(), name.end());
        se.push_back(4);
        se.insert(se.end(), {'T', 'e', 'x', 't'});
        addDescriptor(d, 0x4D, se);
        put16(e, static_cast<uint16_t>((4 << 13) | d.size()));
        e.insert(e.end(), d.begin(), d.end());
        add(makeSection(0x4E, 10, 1, e, section, 1), 0x0012, ccEit);
    };
    eit(0, 100, 12, "News");
    eit(1, 101, 12, "Weather");

    // CAT with an Irdeto EMM
    Bytes cat;
    addDescriptor(cat, 0x09, {0x06, 0x04, 0xE6, 0x00});
    add(makeSection(0x01, 0xFFFF, 0, cat), 0x0001, ccCat);

    // TDT: 2026-10-10 12:34:56 (short section, no CRC)
    Bytes tdt = {0x70, 0x70, 0x05};
    put16(tdt, 40587 + 20736);
    tdt.push_back(0x12);
    tdt.push_back(0x34);
    tdt.push_back(0x56);
    add(tdt, 0x0014, ccTdt);
    return pk;
}

static void testPsi()
{
    const auto pk = buildSiPackets();
    TsTableAnalyzer a;
    for (const Bytes &p : pk)
        a.pushPackets(p.data(), 1);
    const TsReport r = a.report();

    CHECK(r.patSeen);
    CHECK_EQ(r.tsid, 0x0457);
    CHECK_EQ(r.patVersion, 3);
    CHECK_EQ(r.programs.size(), 2u);
    CHECK_EQ(r.crcErrors, 0u);
    const ProgramInfo &p10 = r.programs[0];
    CHECK_EQ(p10.programNumber, 10);
    CHECK(p10.pmtSeen);
    CHECK_EQ(p10.pcrPid, 0x0101);
    CHECK_EQ(p10.streams.size(), 3u);
    if (p10.streams.size() == 3) {
        CHECK_STR(p10.streams[0].kind, "Video");
        CHECK_STR(p10.streams[0].codec, "H.264");
        CHECK_STR(p10.streams[1].codec, "AC-3");
        CHECK_STR(p10.streams[1].language, "fas");
        CHECK_STR(p10.streams[2].kind, "Subtitles");
        CHECK_STR(p10.streams[2].language, "eng");
    }
    CHECK_EQ(p10.ca.size(), 1u);
    if (!p10.ca.empty()) {
        CHECK_EQ(p10.ca[0].pid, 0x0500);
        CHECK_STR(p10.ca[0].systemName, "Viaccess");
    }
    CHECK(!r.programs[1].pmtSeen);

    CHECK_EQ(r.onid, 1);
    CHECK_EQ(r.services.size(), 2u);
    if (r.services.size() == 2) {
        const ServiceInfo &s = r.services[0];
        CHECK_EQ(s.serviceId, 10);
        CHECK_STR(s.name, "\xD8\xA7\xD9\x84\xD8\xB9");
        CHECK_STR(s.typeName, "TV HD (H.264)");
        CHECK(s.scrambled);
        CHECK(s.present.valid);
        CHECK_STR(s.present.name, "News");
        CHECK_STR(s.present.text, "Text");
        CHECK_STR(s.following.name, "Weather");
        CHECK_EQ(s.present.durationSec, 1800u);
        CHECK_EQ(s.present.startUtc, 20736LL * 86400 + 12 * 3600);
        CHECK_STR(r.services[1].typeName, "Radio");
    }

    CHECK_EQ(r.networks.size(), 1u);
    if (!r.networks.empty()) {
        const NetworkInfo &n = r.networks[0];
        CHECK_STR(n.name, "Test Net");
        CHECK_EQ(n.networkId, 0x22);
        CHECK_EQ(n.transports.size(), 1u);
        if (!n.transports.empty()) {
            CHECK_EQ(n.transports[0].serviceIds.size(), 2u);
            const std::string d = n.transports[0].delivery;
            CHECK(d.find("DVB-S2 11494.000 MHz H") != std::string::npos);
            CHECK(d.find("27.500 MS/s") != std::string::npos);
            CHECK(d.find("8PSK 3/4") != std::string::npos);
            CHECK(d.find("13.0°E") != std::string::npos);
            CHECK(d.find("ISI 3") != std::string::npos);
        }
    }
    CHECK_EQ(r.emm.size(), 1u);
    CHECK(r.hasTime);
    CHECK_EQ(r.utcTime, 20736LL * 86400 + 12 * 3600 + 34 * 60 + 56);

    CHECK(r.pidUsage.count(0x0101) && r.pidUsage.at(0x0101).find("Video H.264") != std::string::npos);
    CHECK(r.pidUsage.count(0x0500) && r.pidUsage.at(0x0500).find("ECM Viaccess") != std::string::npos);
    CHECK(r.pidUsage.count(0x0600) && r.pidUsage.at(0x0600).find("EMM Irdeto") != std::string::npos);

    // A corrupted section is counted and ignored
    Bytes bad = builders::makeSection(0x00, 0x0999, 4, {0x00, 0x01, 0xE1, 0x00});
    bad[9] ^= 0xFF;
    a.pushSection(0x0000, bad.data(), bad.size());
    const TsReport r2 = a.report();
    CHECK_EQ(r2.crcErrors, 1u);
    CHECK_EQ(r2.tsid, 0x0457);
}

// ---------------------------------------------------------------------------
// GSE

static std::vector<Bytes> buildGsePackets(size_t &pdus, size_t &ipBytes)
{
    std::vector<Bytes> g;
    pdus = 0;
    ipBytes = 0;
    uint8_t frag = 0;
    for (int i = 0; i < 40; ++i) {
        // Mix: small UDP multicast (whole), large TCP unicast (fragmented), IPv6 with 3-byte label
        Bytes ip;
        uint8_t lt = 0;
        const uint8_t label3[3] = {0xAA, 0xBB, 0xCC};
        const uint8_t *label = kMac;
        uint16_t type = 0x0800;
        if (i % 3 == 0) {
            ip = builders::makeIpv4(17, kSrc, kDst, 5000, 6000, 200, static_cast<uint8_t>(i));
        } else if (i % 3 == 1) {
            ip = builders::makeIpv4(6, kSrc, kDst2, 443, 50000, 3000, static_cast<uint8_t>(i));
            lt = i % 2 ? 3 : 0; // exercise label re-use
        } else {
            uint8_t s6[16] = {0x20, 0x01, 0x0d, 0xb8};
            uint8_t d6[16] = {0xff, 0x05};
            d6[15] = 1;
            s6[15] = 2;
            ip = builders::makeIpv6Udp(s6, d6, 1234, 5678, 500);
            type = 0x86DD;
            lt = 1;
            label = label3;
        }
        ipBytes += ip.size();
        ++pdus;
        for (auto &p : builders::gseEncapsulate(ip, type, lt, label, 1000, frag++))
            g.push_back(p);
    }
    return g;
}

static ProcessorSnapshot runFrames(const std::vector<Bytes> &frames, const ProcessorConfig &cfg = ProcessorConfig())
{
    Processor proc(cfg);
    std::string err;
    CHECK(proc.open(&err));
    for (const Bytes &f : frames) {
        tbs::BbHeader h;
        CHECK(h.parse(f.data()));
        proc.pushBbFrame(f.data(), f.size(), h);
    }
    proc.close();
    return proc.snapshot();
}

static void testGse(bool hem)
{
    size_t pdus = 0, ipBytes = 0;
    const auto packets = buildGsePackets(pdus, ipBytes);
    const auto frames = builders::gseToBbFrames(packets, 3000, hem);
    const ProcessorSnapshot s = runFrames(frames);
    CHECK_EQ(s.streams.size(), 1u);
    if (s.streams.empty())
        return;
    const StreamReport &r = s.streams[0];
    CHECK(r.content == StreamContent::Gse);
    CHECK_EQ(r.gse.pdus, pdus);
    CHECK_EQ(r.gse.crcErrors, 0u);
    CHECK_EQ(r.gse.lengthErrors, 0u);
    CHECK_EQ(r.gse.fragmentErrors, 0u);
    CHECK(r.gse.reassembled > 0);
    CHECK_EQ(r.gse.invalidDataFields, 0u);
    CHECK_EQ(r.ip.ipv4, 27u);
    CHECK_EQ(r.ip.ipv6, 13u);
    CHECK_EQ(r.ip.ipBytes, ipBytes);
    CHECK_EQ(r.ip.malformed, 0u);
    CHECK_EQ(r.ip.flowCount, 3u);
    CHECK_EQ(r.ip.multicast, 27u); // 14 IPv4 multicast + 13 IPv6 multicast
    bool sawTcp = false;
    for (const auto &f : r.ip.topFlows) {
        if (f.protocol == 6) {
            sawTcp = true;
            CHECK_STR(f.src, "10.0.0.1");
            CHECK_STR(f.dst, "192.168.1.20");
            CHECK_EQ(f.srcPort, 443);
            CHECK_EQ(f.packets, 13u);
        }
        if (f.ipVersion == 6) {
            CHECK_STR(f.src, "2001:db8::2");
            CHECK_STR(f.dst, "ff05::1");
        }
    }
    CHECK(sawTcp);
    bool sawMac = false;
    for (const auto &l : r.ip.labels)
        sawMac = sawMac || l.label == "01:00:5e:01:02:03";
    CHECK(sawMac);
}

static void testGseErrors()
{
    // Corrupt one byte inside a fragmented PDU: the reassembly CRC must catch it
    size_t pdus = 0, ipBytes = 0;
    auto packets = buildGsePackets(pdus, ipBytes);
    for (auto &p : packets) {
        if (!(p[0] & 0x80) && (p[0] & 0x40)) { // a last fragment
            p[10] ^= 0x55;
            break;
        }
    }
    const auto frames = builders::gseToBbFrames(packets, 3000, false);
    ProcessorSnapshot s = runFrames(frames);
    CHECK_EQ(s.streams[0].gse.crcErrors, 1u);
    CHECK_EQ(s.streams[0].gse.pdus, pdus - 1);

    // Random data in a generic continuous stream is not GSE
    std::vector<Bytes> noise;
    for (int i = 0; i < 20; ++i) {
        Bytes df(2000);
        for (size_t k = 0; k < df.size(); ++k)
            df[k] = static_cast<uint8_t>((k * 131 + i * 17) | 0x10);
        noise.push_back(builders::makeBbFrame(0x70, 0, 0, df, 0, 0));
    }
    s = runFrames(noise);
    CHECK(s.streams[0].content == StreamContent::GenericContinuous);
}

static void testPcap()
{
    size_t pdus = 0, ipBytes = 0;
    const auto packets = buildGsePackets(pdus, ipBytes);
    const auto frames = builders::gseToBbFrames(packets, 3000, false);
    for (bool eth : {true, false}) {
        ProcessorConfig cfg;
        cfg.pcap.enabled = true;
        cfg.pcap.path = eth ? "tbs_test_eth.pcap" : "tbs_test_raw.pcap";
        cfg.pcap.timestampSuffix = false;
        cfg.pcap.ethernetFraming = eth;
        const ProcessorSnapshot s = runFrames(frames, cfg);
        CHECK_EQ(s.pcapPackets, pdus);
        const uint64_t expected = 24 + pdus * 16 + ipBytes + (eth ? pdus * 14 : 0);
        CHECK_EQ(s.pcapBytes, expected);
        std::FILE *f = std::fopen(cfg.pcap.path.c_str(), "rb");
        CHECK(f != nullptr);
        if (f) {
            uint8_t hdr[24];
            CHECK_EQ(std::fread(hdr, 1, 24, f), 24u);
            CHECK(hdr[0] == 0xD4 && hdr[1] == 0xC3 && hdr[2] == 0xB2 && hdr[3] == 0xA1);
            CHECK_EQ(hdr[20], eth ? 1 : 101);
            uint8_t rec[16 + 14];
            CHECK_EQ(std::fread(rec, 1, sizeof(rec), f), sizeof(rec));
            if (eth)
                CHECK(std::memcmp(rec + 16, kMac, 6) == 0); // label became the destination MAC
            else
                CHECK_EQ(rec[16] >> 4, 4); // raw IPv4
            std::fseek(f, 0, SEEK_END);
            CHECK_EQ(static_cast<uint64_t>(std::ftell(f)), expected);
            std::fclose(f);
        }
        std::remove(cfg.pcap.path.c_str());
    }
}

static void testMultistreamAndFiles()
{
    // ISI 1: TS with tables, ISI 2: GSE (HEM)
    std::vector<Bytes> tsPackets = buildSiPackets();
    const auto tsFrames = [&] {
        auto f = builders::tsToBbFrames(tsPackets, 2000);
        for (auto &fr : f) {
            fr[0] = 0xD0; // TS, MIS, CCM
            fr[1] = 1;    // ISI 1
            fr[9] = tbs::BbHeader::crc8(fr.data(), 9);
        }
        return f;
    }();
    size_t pdus = 0, ipBytes = 0;
    const auto gseFrames = builders::gseToBbFrames(buildGsePackets(pdus, ipBytes), 3000, true, 2);
    std::vector<Bytes> all;
    for (size_t i = 0; i < std::max(tsFrames.size(), gseFrames.size()); ++i) {
        if (i < tsFrames.size())
            all.push_back(tsFrames[i]);
        if (i < gseFrames.size())
            all.push_back(gseFrames[i]);
    }

    const ProcessorSnapshot s = runFrames(all);
    CHECK_EQ(s.streams.size(), 2u);
    if (s.streams.size() == 2) {
        CHECK_EQ(s.streams[0].isi, 1);
        CHECK(s.streams[0].content == StreamContent::TransportStream);
        CHECK_EQ(s.streams[0].ts.services.size(), 2u);
        CHECK_EQ(s.streams[1].isi, 2);
        CHECK(s.streams[1].content == StreamContent::Gse);
        CHECK_EQ(s.streams[1].gse.pdus, pdus);
    }

    // The same frames through each capture file format
    struct Case
    {
        const char *name;
        CaptureFormat format;
    };
    for (const Case c : {Case{"tbs_test_raw.bbf", CaptureFormat::RawBbFrames}, Case{"tbs_test_b8.bbf", CaptureFormat::B8BbFrames},
                         Case{"tbs_test_pseudo.ts", CaptureFormat::PseudoTs}}) {
        std::FILE *f = std::fopen(c.name, "wb");
        uint8_t cc = 0;
        for (const Bytes &fr : all) {
            if (c.format == CaptureFormat::PseudoTs) {
                for (const Bytes &p : builders::bbFrameToPseudoTs(fr, cc))
                    std::fwrite(p.data(), 1, 188, f);
            } else {
                if (c.format == CaptureFormat::B8BbFrames)
                    std::fputc(0xB8, f);
                std::fwrite(fr.data(), 1, fr.size(), f);
            }
        }
        std::fclose(f);
        Processor proc;
        CaptureCallbacks cb;
        size_t frames = 0;
        cb.onBbFrame = [&](const uint8_t *fr, size_t n, const tbs::BbHeader &h) {
            ++frames;
            proc.pushBbFrame(fr, n, h);
        };
        CaptureFormat detected = CaptureFormat::Unknown;
        std::string err;
        CHECK(readCaptureFile(c.name, cb, &detected, &err));
        CHECK(detected == c.format);
        CHECK_EQ(frames, all.size());
        const ProcessorSnapshot fs = proc.snapshot();
        CHECK_EQ(fs.streams.size(), 2u);
        if (fs.streams.size() == 2)
            CHECK_EQ(fs.streams[1].gse.pdus, pdus);
        std::remove(c.name);
    }

    // Plain TS through the reader
    {
        std::FILE *f = std::fopen("tbs_test_plain.ts", "wb");
        for (int rep = 0; rep < 10; ++rep)
            for (const Bytes &p : tsPackets)
                std::fwrite(p.data(), 1, 188, f);
        std::fclose(f);
        Processor proc;
        CaptureCallbacks cb;
        cb.onTs = [&](const uint8_t *p, size_t n) { proc.pushTs(p, n); };
        CaptureFormat detected = CaptureFormat::Unknown;
        CHECK(readCaptureFile("tbs_test_plain.ts", cb, &detected, nullptr));
        CHECK(detected == CaptureFormat::TransportStream);
        const ProcessorSnapshot fs = proc.snapshot();
        CHECK(fs.streams.size() == 1 && fs.streams[0].content == StreamContent::TsInput);
        if (!fs.streams.empty())
            CHECK_EQ(fs.streams[0].ts.programs.size(), 2u);
        CHECK(!formatReport(fs).empty());
        std::remove("tbs_test_plain.ts");
    }
}

int main()
{
    testDvbText();
    testPsi();
    testGse(false);
    testGse(true);
    testGseErrors();
    testPcap();
    testMultistreamAndFiles();
    if (g_failures) {
        std::fprintf(stderr, "%d check(s) failed\n", g_failures);
        return 1;
    }
    std::printf("All bbproc tests passed\n");
    return 0;
}
