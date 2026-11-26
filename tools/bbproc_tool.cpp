// tbs-bbproc: offline analysis of a capture with the tbsbbproc library.
//
//   tbs-bbproc capture.bbf                    report stream content, tables, GSE/IP statistics
//   tbs-bbproc capture.ts --pcap ip.pcap      also extract GSE IP packets to a pcap file
//   tbs-bbproc capture.ts --xmltv epg.xml     also export the EPG
//   tbs-bbproc capture.bbf --decoder gse-len2 force a (non-standard) generic stream decoder
//
// Accepts plain TS, the card's pseudo-TS, raw BBFrames and 0xB8-prefixed BBFrames.

#include "tbsbbproc/CaptureFile.h"
#include "tbsbbproc/Processor.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

int main(int argc, char **argv)
{
    std::string input;
    tbs::bb::ProcessorConfig cfg;
    bool verbose = false;
    std::string xmltv;

    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--pcap" && i + 1 < argc) {
            cfg.pcap.enabled = true;
            cfg.pcap.path = argv[++i];
            cfg.pcap.timestampSuffix = false;
        } else if (a == "--pcapng") {
            cfg.pcap.format = tbs::bb::PcapFormat::PcapNg;
        } else if (a == "--tun" && i + 1 < argc) {
            cfg.tun.enabled = true;
            cfg.tun.name = argv[++i];
        } else if (a == "--tap") {
            cfg.tun.tap = true;
        } else if (a == "--xmltv" && i + 1 < argc) {
            xmltv = argv[++i];
        } else if (a == "--raw-ip") {
            cfg.pcap.ethernetFraming = false;
        } else if (a == "--all-pdus") {
            cfg.pcap.ipOnly = false;
        } else if (a == "--decoder" && i + 1 < argc) {
            if (!tbs::bb::parseGenericDecoder(argv[++i], &cfg.genericDecoder)) {
                std::fprintf(stderr, "unknown decoder %s\n", argv[i]);
                return 2;
            }
        } else if (a == "--probe-frames" && i + 1 < argc) {
            cfg.probeFrames = static_cast<unsigned>(std::strtoul(argv[++i], nullptr, 10));
        } else if (a == "--keep-frame-crc") {
            cfg.stripFrameCrc32 = false;
        } else if (a == "--verbose" || a == "-v") {
            verbose = true;
        } else if (a == "--help" || a == "-h") {
            input.clear();
            break;
        } else if (input.empty() && a[0] != '-') {
            input = a;
        } else {
            std::fprintf(stderr, "unknown option %s\n", a.c_str());
            return 2;
        }
    }
    if (input.empty()) {
        std::string ids;
        for (int k = 0; k < tbs::bb::kGenericDecoderCount; ++k) {
            const auto dec = static_cast<tbs::bb::GenericDecoder>(k);
            ids += std::string("               ") + tbs::bb::genericDecoderId(dec) + ": " + tbs::bb::genericDecoderName(dec) + "\n";
        }
        std::printf("Usage: %s CAPTURE [--pcap FILE] [--pcapng] [--raw-ip] [--all-pdus] [--tun NAME [--tap]] [--xmltv FILE]\n"
                    "       [--decoder ID] [--probe-frames N] [--keep-frame-crc] [--verbose]\n"
                    "  CAPTURE    TS, pseudo-TS, raw BBFrames or 0xB8 BBFrames\n"
                    "  --pcap     write IP packets carried in GSE or MPE to FILE (--pcapng: one interface per stream)\n"
                    "  --tun      replay the IP packets into a Linux TUN (or --tap) interface\n"
                    "  --xmltv    write the EPG (EIT present/following and schedule) as XMLTV\n"
                    "  --raw-ip   pcap with raw IP packets instead of Ethernet framing\n"
                    "  --all-pdus include non-IP PDUs (Ethernet framing only)\n"
                    "  --decoder  how generic streams are decoded (default auto: standard GSE or the best non-standard variant)\n%s"
                    "  --probe-frames  data fields per stream used by the auto detection (default 64)\n"
                    "  --keep-frame-crc  do not remove a CRC-32 found at the end of BBFrames\n"
                    "  --verbose  all flows and the table list; detection scores\n",
                    argv[0], ids.c_str());
        return input.empty() && argc > 1 ? 0 : 2;
    }

    tbs::bb::Processor proc(cfg);
    std::string err;
    if (!proc.open(&err)) {
        std::fprintf(stderr, "error: %s\n", err.c_str());
        return 1;
    }
    tbs::bb::CaptureCallbacks cb;
    cb.onBbFrame = [&](const uint8_t *f, size_t n, const tbs::BbHeader &h) { proc.pushBbFrame(f, n, h); };
    cb.onTs = [&](const uint8_t *p, size_t n) { proc.pushTs(p, n); };
    tbs::bb::CaptureFormat format = tbs::bb::CaptureFormat::Unknown;
    if (!tbs::bb::readCaptureFile(input, cb, &format, &err)) {
        std::fprintf(stderr, "error: %s\n", err.c_str());
        return 1;
    }
    proc.close();
    std::printf("Input: %s (%s)\n", input.c_str(), tbs::bb::captureFormatName(format));
    const tbs::bb::ProcessorSnapshot snap = proc.snapshot();
    std::printf("%s", tbs::bb::formatReport(snap, verbose).c_str());
    if (!xmltv.empty()) {
        std::vector<tbs::bb::ServiceSchedule> all;
        for (const auto &st : snap.streams)
            for (auto &s : proc.epg(st.isi))
                all.push_back(std::move(s));
        const std::string doc = tbs::bb::formatXmltv(all);
        std::FILE *f = std::fopen(xmltv.c_str(), "wb");
        if (!f || std::fwrite(doc.data(), 1, doc.size(), f) != doc.size()) {
            std::fprintf(stderr, "error: cannot write %s\n", xmltv.c_str());
            if (f)
                std::fclose(f);
            return 1;
        }
        std::fclose(f);
        size_t events = 0;
        for (const auto &s : all)
            events += s.events.size();
        std::printf("XMLTV: %s, %u channels, %u programmes\n", xmltv.c_str(), static_cast<unsigned>(all.size()),
                    static_cast<unsigned>(events));
    }
    return 0;
}
