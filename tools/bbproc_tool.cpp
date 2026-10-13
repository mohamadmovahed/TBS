// tbs-bbproc: offline analysis of a capture with the tbsbbproc library.
//
//   tbs-bbproc capture.bbf                    report stream content, tables, GSE/IP statistics
//   tbs-bbproc capture.ts --pcap ip.pcap      also extract GSE IP packets to a pcap file
//
// Accepts plain TS, the card's pseudo-TS, raw BBFrames and 0xB8-prefixed BBFrames.

#include "tbsbbproc/CaptureFile.h"
#include "tbsbbproc/Processor.h"

#include <cstdio>
#include <cstring>
#include <string>

int main(int argc, char **argv)
{
    std::string input;
    tbs::bb::ProcessorConfig cfg;
    bool verbose = false;

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
        } else if (a == "--raw-ip") {
            cfg.pcap.ethernetFraming = false;
        } else if (a == "--all-pdus") {
            cfg.pcap.ipOnly = false;
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
        std::printf("Usage: %s CAPTURE [--pcap FILE] [--pcapng] [--raw-ip] [--all-pdus] [--tun NAME [--tap]] [--verbose]\n"
                    "  CAPTURE    TS, pseudo-TS, raw BBFrames or 0xB8 BBFrames\n"
                    "  --pcap     write IP packets carried in GSE or MPE to FILE (--pcapng: one interface per stream)\n"
                    "  --tun      replay the IP packets into a Linux TUN (or --tap) interface\n"
                    "  --raw-ip   pcap with raw IP packets instead of Ethernet framing\n"
                    "  --all-pdus include non-IP PDUs (Ethernet framing only)\n"
                    "  --verbose  all flows and the table list\n",
                    argv[0]);
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
    std::printf("%s", tbs::bb::formatReport(proc.snapshot(), verbose).c_str());
    return 0;
}
