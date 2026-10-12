// Writes the seed corpus for the fuzz targets (valid inputs in each harness format).
//   tbs-fuzz-gen-corpus <corpus dir>

#include "../tests/StreamBuilders.h"

#include <filesystem>
#include <fstream>
#include <string>

using builders::Bytes;
namespace fs = std::filesystem;

static void save(const fs::path &dir, const std::string &name, const Bytes &b)
{
    fs::create_directories(dir);
    std::ofstream(dir / name, std::ios::binary).write(reinterpret_cast<const char *>(b.data()), static_cast<std::streamsize>(b.size()));
}

static std::vector<Bytes> sampleGse(size_t pdus)
{
    std::vector<Bytes> g;
    const uint8_t src[4] = {10, 0, 0, 1}, dst[4] = {239, 1, 1, 1}, host[4] = {192, 168, 1, 2};
    const uint8_t mac[6] = {1, 0, 0x5e, 1, 1, 1};
    uint8_t s6[16] = {0x20, 1, 0x0d, 0xb8}, d6[16] = {0xff, 2};
    for (size_t i = 0; i < pdus; ++i) {
        Bytes ip;
        uint16_t type = 0x0800;
        if (i % 3 == 0)
            ip = builders::makeIpv4(17, src, dst, 1000, 2000, 300, static_cast<uint8_t>(i));
        else if (i % 3 == 1)
            ip = builders::makeIpv4(6, src, host, 443, 5000, 1400, static_cast<uint8_t>(i));
        else {
            ip = builders::makeIpv6Udp(s6, d6, 1, 2, 200);
            type = 0x86DD;
        }
        for (auto &p : builders::gseEncapsulate(ip, type, static_cast<uint8_t>(i % 4 == 3 ? 3 : i % 3), mac, 500, static_cast<uint8_t>(i)))
            g.push_back(p);
    }
    return g;
}

static std::vector<Bytes> sampleSections()
{
    using namespace builders;
    std::vector<Bytes> secs;
    Bytes pat;
    put16(pat, 1);
    put16(pat, 0xE100);
    secs.push_back(makeSection(0x00, 1, 0, pat));
    Bytes pmt;
    put16(pmt, 0xE101);
    put16(pmt, 0xF000);
    pmt.push_back(0x1B);
    put16(pmt, 0xE101);
    Bytes d;
    addDescriptor(d, 0x0A, {'e', 'n', 'g', 0});
    put16(pmt, static_cast<uint16_t>(0xF000 | d.size()));
    pmt.insert(pmt.end(), d.begin(), d.end());
    secs.push_back(makeSection(0x02, 1, 0, pmt));
    Bytes sdt;
    put16(sdt, 1);
    sdt.push_back(0xFF);
    put16(sdt, 1);
    sdt.push_back(0xFF);
    Bytes sd = {0x19, 1, 'P', 4, 0x02, 0xC7, 0xE4, 0xD9};
    Bytes desc;
    addDescriptor(desc, 0x48, sd);
    put16(sdt, static_cast<uint16_t>(0x8000 | desc.size()));
    sdt.insert(sdt.end(), desc.begin(), desc.end());
    secs.push_back(makeSection(0x42, 1, 0, sdt));
    Bytes nit, nd;
    addDescriptor(nd, 0x40, dvbString("Net"));
    put16(nit, static_cast<uint16_t>(0xF000 | nd.size()));
    nit.insert(nit.end(), nd.begin(), nd.end());
    Bytes ts;
    put16(ts, 1);
    put16(ts, 1);
    Bytes td;
    addDescriptor(td, 0x43, {0x01, 0x14, 0x94, 0x00, 0x01, 0x30, 0x86, 0x02, 0x75, 0x00, 0x03});
    addDescriptor(td, 0x79, {0xC0, 0x00, 0x00, 0x08, 0x03});
    put16(ts, static_cast<uint16_t>(0xF000 | td.size()));
    ts.insert(ts.end(), td.begin(), td.end());
    put16(nit, static_cast<uint16_t>(0xF000 | ts.size()));
    nit.insert(nit.end(), ts.begin(), ts.end());
    secs.push_back(makeSection(0x40, 1, 0, nit));
    Bytes eit;
    put16(eit, 1);
    put16(eit, 1);
    eit.push_back(1);
    eit.push_back(0x4E);
    put16(eit, 7);
    put16(eit, 61000);
    eit.insert(eit.end(), {0x12, 0x00, 0x00, 0x00, 0x30, 0x00});
    Bytes ed;
    addDescriptor(ed, 0x4D, {'e', 'n', 'g', 4, 'N', 'e', 'w', 's', 1, 'x'});
    put16(eit, static_cast<uint16_t>(0x8000 | ed.size()));
    eit.insert(eit.end(), ed.begin(), ed.end());
    secs.push_back(makeSection(0x4E, 1, 0, eit));
    return secs;
}

int main(int argc, char **argv)
{
    if (argc < 2)
        return 2;
    const fs::path root = argv[1];

    // fuzz_bbframes: flags(1) + 9 header bytes + data field
    auto bbRecords = [](const std::vector<Bytes> &frames, uint8_t flags) {
        Bytes out;
        for (const Bytes &f : frames) {
            out.push_back(flags);
            out.insert(out.end(), f.begin(), f.begin() + 9);
            out.insert(out.end(), f.begin() + 10, f.end());
        }
        return out;
    };
    const auto gseNm = builders::gseToBbFrames(sampleGse(12), 1200, false);
    const auto gseHem = builders::gseToBbFrames(sampleGse(12), 700, true, 3);
    std::vector<Bytes> tsPackets;
    uint8_t cc[8] = {};
    const uint16_t pids[] = {0x0000, 0x0100, 0x0011, 0x0010, 0x0012};
    int k = 0;
    for (const Bytes &s : sampleSections()) {
        for (auto &p : builders::sectionToTs(s, pids[k], cc[k]))
            tsPackets.push_back(p);
        ++k;
    }
    for (int i = 0; i < 20; ++i)
        tsPackets.push_back(builders::makeTsPacket(0x101, static_cast<uint8_t>(i), static_cast<uint8_t>(i)));
    const auto tsFrames = builders::tsToBbFrames(tsPackets, 1500, false);
    save(root / "bbframes", "gse_nm", bbRecords(gseNm, 0x80));
    save(root / "bbframes", "gse_hem", bbRecords(gseHem, 0x81));
    save(root / "bbframes", "ts", bbRecords(tsFrames, 0x80));

    // fuzz_pseudots
    auto pseudo = [](const std::vector<Bytes> &frames) {
        Bytes out;
        uint8_t c = 0;
        for (const Bytes &f : frames)
            for (const Bytes &p : builders::bbFrameToPseudoTs(f, c))
                out.insert(out.end(), p.begin(), p.end());
        return out;
    };
    save(root / "pseudots", "gse", pseudo(gseNm));
    save(root / "pseudots", "ts_in_bb", pseudo(tsFrames));
    Bytes plain;
    for (const Bytes &p : tsPackets)
        plain.insert(plain.end(), p.begin(), p.end());
    save(root / "pseudots", "plain_ts", plain);

    // fuzz_psi: selector(1) len(2) section-without-CRC
    {
        Bytes out;
        const uint8_t sel[] = {0x00, 0x06, 0x03, 0x02, 0x04};
        int i = 0;
        for (const Bytes &s : sampleSections()) {
            const size_t n = s.size() - 4;
            out.push_back(sel[i++]);
            builders::put16(out, static_cast<uint16_t>(n));
            out.insert(out.end(), s.begin(), s.begin() + static_cast<std::ptrdiff_t>(n));
        }
        save(root / "psi", "tables", out);
    }

    // fuzz_gse: flags(1) syncd(2) len(2) data field
    auto gseRecords = [](const std::vector<Bytes> &frames, uint8_t flags) {
        Bytes out;
        for (const Bytes &f : frames) {
            out.push_back(flags);
            out.push_back(f[7]);
            out.push_back(f[8]);
            builders::put16(out, static_cast<uint16_t>(f.size() - 10));
            out.insert(out.end(), f.begin() + 10, f.end());
        }
        return out;
    };
    save(root / "gse", "nm", gseRecords(gseNm, 0x80));
    save(root / "gse", "hem", gseRecords(gseHem, 0x81));

    // fuzz_dvbtext
    save(root / "dvbtext", "latin", builders::dvbString("BBC One"));
    save(root / "dvbtext", "arabic", {0x02, 0xC7, 0xE4, 0xD9});
    save(root / "dvbtext", "utf8", {0x15, 0xD8, 0xA7, 'x'});
    save(root / "dvbtext", "ucs2", {0x11, 0x00, 'A', 0x06, 0x27});
    save(root / "dvbtext", "table", {0x10, 0x00, 0x05, 'a', 0xE9});

    // fuzz_capture
    Bytes raw, b8;
    for (const Bytes &f : gseNm) {
        raw.insert(raw.end(), f.begin(), f.end());
        b8.push_back(0xB8);
        b8.insert(b8.end(), f.begin(), f.end());
    }
    save(root / "capture", "raw", raw);
    save(root / "capture", "b8", b8);
    save(root / "capture", "pseudo", pseudo(tsFrames));
    return 0;
}
