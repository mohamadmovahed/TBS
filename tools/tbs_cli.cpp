// tbs-cli: headless front end for the tbsdvb library.
//
//   tbs-cli --list
//   tbs-cli --device linux:0:0 --freq 11494 --sr 27500 --pol h --out capture.ts
//   tbs-cli --device bda:0 --freq 10971 --sr 29700 --pol h --bbframe --zmq tcp://*:5555
//   tbs-cli --device replay:video_bb.ts --rate 5 --bbframe --out frames.bbf --duration 10
//   tbs-cli --device linux:0 --spectrum 10700-12750 --pol both --csv spectrum.csv
//   tbs-cli --device linux:0 --blindscan 10700-12750 --pol both
//   tbs-cli --device linux:0 --freq 11494 --sr 27500 --pol h --nit-scan

#include "tbsbbproc/NetworkScan.h"
#include "tbsbbproc/Processor.h"
#include "tbsdvb/tbsdvb.h"

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>

namespace {

std::atomic<bool> g_stop{false};

void onSignal(int)
{
    g_stop = true;
}

void usage(const char *argv0)
{
    std::printf(
        "Usage: %s [options]\n"
        "  --list                      list receivers and exit\n"
        "  --diagnose                  check driver / device setup for --device and exit\n"
        "Device:\n"
        "  --device linux:A[:F[:D]]    Linux DVB adapter A, frontend F, demux D\n"
        "  --device bda:N|bda:PATH     Windows BDA tuner by index or device path\n"
        "  --device replay:FILE        replay a recording (--rate MBIT, --no-loop)\n"
        "Tuning:\n"
        "  --freq MHZ  --sr KSPS  --dvbs | --dvbs2\n"
        "  --lnb universal|single|direct  --lo1 MHZ --lo2 MHZ --switch MHZ\n"
        "  --pol h|v|off  --tone auto|on|off  --diseqc 0..3\n"
        "  --isi N  --pls-mode root|gold|combo  --pls-code N\n"
        "Capture / outputs:\n"
        "  --bbframe                   capture BBFrames instead of TS\n"
        "  --out FILE [--bb-format raw|b8|pseudo] [--timestamp]\n"
        "      [--split-size MIB] [--split-time MIN] [--keep N] [--min-free MIB]\n"
        "                              new file every MIB / MIN, keep newest N, stop below MIB free\n"
        "  --buffer MIB                capture -> processing buffer (default 128)\n"
        "  --zmq ENDPOINT [--push] [--connect] [--topic T] [--header] [--packets N]\n"
        "  --net URL                   stream the TS over the network (repeatable):\n"
        "                                udp://239.1.1.1:1234  rtp://host:5004  srt://host:9000  srt://:9000?mode=listener\n"
        "                                options: ?program=N  pids=0x100,0x101  isi=N  ttl=N  iface=eth1|ADDR  pkts=1..7\n"
        "                                         null=1  latency=MS  passphrase=..  streamid=..\n"
        "  --analyze                   analyse the content (TS tables, GSE/IP) and print a report at the end\n"
        "  --pcap FILE [--pcap-raw] [--pcapng]\n"
        "                              write IP packets from GSE (BBFrame mode) or MPE (TS) to a pcap file\n"
        "  --tun NAME [--tap]          send those packets to a Linux TUN (IP) or TAP (Ethernet) interface\n"
        "  --duration SEC              stop after SEC seconds (default: until Ctrl+C)\n"
        "  --quiet                     no periodic status line\n"
        "Scanning (spectrum, blind scan and IQ need the neumoDVB driver; --pol both scans H and V):\n"
        "  --caps                      show what the driver supports\n"
        "  --spectrum MHZ-MHZ [--sweep] [--resolution KHZ] [--fft-size N] [--csv FILE]\n"
        "                              measure the spectrum, list carrier candidates\n"
        "  --blindscan MHZ-MHZ [--max-sr KSPS]\n"
        "                              find and lock all carriers in the range\n"
        "  --nit-scan [--nit-other] [--all-positions]\n"
        "                              tune --freq, then every transport the NIT lists; print the services\n",
        argv0);
}

bool parseRange(const std::string &v, double &a, double &b)
{
    return std::sscanf(v.c_str(), "%lf-%lf", &a, &b) == 2 && b > a;
}

std::vector<tbs::Polarization> polarizations(bool both, tbs::Polarization p)
{
    if (both)
        return {tbs::Polarization::HorizontalLeft18V, tbs::Polarization::VerticalRight13V};
    return {p};
}

bool progressLine(double f, const std::string &msg)
{
    if (f >= 0)
        std::fprintf(stderr, "\r%3.0f%% %-70s", f * 100.0, msg.substr(0, 70).c_str());
    return !g_stop;
}

int runSpectrum(const tbs::ReceiverConfig &cfg, double a, double b, bool both, bool sweep, int resolution, int fft,
                const std::string &csv)
{
    std::ofstream out;
    if (!csv.empty()) {
        out.open(csv);
        if (!out) {
            std::fprintf(stderr, "error: cannot write %s\n", csv.c_str());
            return 1;
        }
        out << "frequency_mhz,polarization,level_db\n";
    }
    for (tbs::Polarization pol : polarizations(both, cfg.tuning.polarization)) {
        tbs::SpectrumRequest req;
        req.startMHz = a;
        req.endMHz = b;
        req.lnb = cfg.tuning;
        req.lnb.polarization = pol;
        req.method = sweep ? tbs::SpectrumMethod::Sweep : tbs::SpectrumMethod::Fft;
        req.resolutionKHz = resolution;
        req.fftSize = fft;
        tbs::SpectrumResult res;
        std::string err;
        const char p = pol == tbs::Polarization::VerticalRight13V ? 'V' : 'H';
        const bool ok = tbs::Scan::spectrum(cfg.device, req, &res, &err, progressLine);
        std::fprintf(stderr, "\n");
        if (!ok) {
            std::fprintf(stderr, "error: %s\n", err.c_str());
            return 1;
        }
        std::printf("%c: %zu points, %zu carrier candidates\n", p, res.points.size(), res.peaks.size());
        for (const tbs::SpectrumPeak &pk : res.peaks)
            std::printf("  %10.3f MHz %c  ~%6.0f kS/s  %6.1f dB%s\n", pk.frequencyMHz, p, pk.symbolRateKsps, pk.level,
                        res.decibel ? "" : " (relative)");
        if (out)
            for (const tbs::SpectrumPoint &pt : res.points)
                out << pt.frequencyMHz << ',' << p << ',' << pt.level << '\n';
    }
    return 0;
}

int runBlindScan(const tbs::ReceiverConfig &cfg, double a, double b, bool both, int maxSr)
{
    size_t total = 0;
    for (tbs::Polarization pol : polarizations(both, cfg.tuning.polarization)) {
        tbs::BlindScanRequest req;
        req.startMHz = a;
        req.endMHz = b;
        req.lnb = cfg.tuning;
        req.lnb.polarization = pol;
        if (maxSr > 0)
            req.maxSymbolRateKsps = maxSr;
        std::vector<tbs::BlindScanCarrier> found;
        std::string err;
        const bool ok = tbs::Scan::blindScan(cfg.device, req, &found, &err, progressLine,
                                             [](const tbs::BlindScanCarrier &c) {
                                                 std::fprintf(stderr, "\r%-78s\n", ("  " + c.describe()).c_str());
                                             });
        std::fprintf(stderr, "\n");
        if (!ok) {
            std::fprintf(stderr, "error: %s\n", err.c_str());
            return 1;
        }
        for (const tbs::BlindScanCarrier &c : found)
            std::printf("%s\n", c.describe().c_str());
        total += found.size();
    }
    std::printf("%zu carriers\n", total);
    return 0;
}

int runNitScan(tbs::ReceiverConfig cfg, const tbs::bb::NetworkScanOptions &opt)
{
    tbs::bb::Processor proc;
    tbs::Receiver rx;
    tbs::Receiver::Callbacks cb;
    cb.onLog = [](const std::string &m) { std::fprintf(stderr, "%s\n", m.c_str()); };
    if (cfg.capture == tbs::CaptureMode::BBFrame)
        cb.onBbFrame = [&](const uint8_t *f, size_t n, const tbs::BbHeader &h) { proc.pushBbFrame(f, n, h); };
    else
        cb.onTsPackets = [&](const uint8_t *p, size_t n) { proc.pushTs(p, n); };
    rx.setCallbacks(cb);
    rx.setStatusInterval(200);
    cfg.file.enabled = false;
    cfg.zmq.enabled = false;
    cfg.network.clear();
    if (!rx.start(cfg))
        return 1;
    std::atomic<bool> cancel{false};
    std::thread watch([&]() {
        while (!g_stop && !cancel)
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        cancel = true;
    });
    const auto result = tbs::bb::runNetworkScan(
        rx, proc, {cfg.tuning}, opt,
        [](const tbs::bb::NetworkScanProgress &p) {
            if (p.index < p.total)
                std::fprintf(stderr, "[%d/%d] %s\n", p.index + 1, p.total, p.message.c_str());
        },
        {}, &cancel);
    cancel = true;
    watch.join();
    rx.stop();
    std::printf("%s", tbs::bb::formatNetworkScan(result).c_str());
    return result.transponders.empty() ? 1 : 0;
}

std::string kindName(tbs::DeviceKind k)
{
    switch (k) {
    case tbs::DeviceKind::LinuxDvb:
        return "linux";
    case tbs::DeviceKind::WindowsBda:
        return "bda";
    case tbs::DeviceKind::FileReplay:
        return "replay";
    }
    return "?";
}

bool parseDevice(const std::string &v, tbs::DeviceSelection &d)
{
    const auto colon = v.find(':');
    const std::string kind = v.substr(0, colon);
    const std::string rest = colon == std::string::npos ? std::string() : v.substr(colon + 1);
    if (kind == "linux") {
        d.kind = tbs::DeviceKind::LinuxDvb;
        int a = 0, f = 0, x = 0;
        const int n = std::sscanf(rest.c_str(), "%d:%d:%d", &a, &f, &x);
        d.adapter = a;
        d.frontend = n >= 2 ? f : 0;
        d.demux = n >= 3 ? x : 0;
        return true;
    }
    if (kind == "bda") {
        d.kind = tbs::DeviceKind::WindowsBda;
        char *end = nullptr;
        const long idx = std::strtol(rest.c_str(), &end, 10);
        if (!rest.empty() && end && *end == 0)
            d.bdaIndex = static_cast<int>(idx);
        else
            d.bdaDevicePath = rest;
        return true;
    }
    if (kind == "replay") {
        d.kind = tbs::DeviceKind::FileReplay;
        d.replayFile = rest;
        return !rest.empty();
    }
    return false;
}

} // namespace

int main(int argc, char **argv)
{
    tbs::ReceiverConfig cfg;
    double duration = 0;
    bool quiet = false;
    bool diagnoseOnly = false;
    bool analyze = false;
    size_t bufferMB = 128;
    tbs::bb::ProcessorConfig procCfg;
    enum class Mode { Receive, Caps, Spectrum, BlindScan, NitScan } mode = Mode::Receive;
    double rangeA = 0, rangeB = 0;
    bool bothPol = false, sweep = false;
    int resolution = 0, fftSize = 512, maxSr = 0;
    std::string csv;
    tbs::bb::NetworkScanOptions nitOpt;

    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto next = [&]() -> std::string {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "missing value for %s\n", a.c_str());
                std::exit(2);
            }
            return argv[++i];
        };
        if (a == "--help" || a == "-h") {
            usage(argv[0]);
            return 0;
        } else if (a == "--list") {
            for (const auto &d : tbs::Devices::enumerate(false))
                std::printf("%-7s %s\n        %s\n", kindName(d.kind).c_str(), d.label().c_str(), d.path.c_str());
            return 0;
        } else if (a == "--diagnose") {
            diagnoseOnly = true;
        } else if (a == "--device") {
            if (!parseDevice(next(), cfg.device)) {
                std::fprintf(stderr, "bad --device value\n");
                return 2;
            }
        } else if (a == "--rate") {
            cfg.device.replayRateMbps = std::stod(next());
        } else if (a == "--no-loop") {
            cfg.device.replayLoop = false;
        } else if (a == "--freq") {
            cfg.tuning.frequencyMHz = std::stod(next());
        } else if (a == "--sr") {
            cfg.tuning.symbolRateKsps = std::stod(next());
        } else if (a == "--dvbs") {
            cfg.tuning.deliverySystem = tbs::DeliverySystem::DvbS;
        } else if (a == "--dvbs2") {
            cfg.tuning.deliverySystem = tbs::DeliverySystem::DvbS2;
        } else if (a == "--lnb") {
            const std::string v = next();
            cfg.tuning.lnbType = v == "direct" ? tbs::LnbType::Direct : v == "single" ? tbs::LnbType::SingleLo : tbs::LnbType::Universal;
        } else if (a == "--lo1") {
            cfg.tuning.lnbLo1MHz = std::stod(next());
        } else if (a == "--lo2") {
            cfg.tuning.lnbLo2MHz = std::stod(next());
        } else if (a == "--switch") {
            cfg.tuning.lnbSwitchMHz = std::stod(next());
        } else if (a == "--pol") {
            const std::string v = next();
            bothPol = v == "both";
            cfg.tuning.polarization = v == "v" || v == "r" ? tbs::Polarization::VerticalRight13V
                : v == "off"                               ? tbs::Polarization::Off
                                                           : tbs::Polarization::HorizontalLeft18V;
        } else if (a == "--tone") {
            const std::string v = next();
            cfg.tuning.tone = v == "on" ? tbs::ToneMode::On : v == "off" ? tbs::ToneMode::Off : tbs::ToneMode::Auto;
        } else if (a == "--diseqc") {
            cfg.tuning.diseqcPort = std::stoi(next());
        } else if (a == "--isi") {
            cfg.tuning.inputStreamId = std::stoi(next());
        } else if (a == "--pls-mode") {
            const std::string v = next();
            cfg.tuning.plsMode = v == "root" ? tbs::PlsMode::Root : v == "combo" ? tbs::PlsMode::Combo : tbs::PlsMode::Gold;
        } else if (a == "--pls-code") {
            cfg.tuning.plsCode = static_cast<uint32_t>(std::stoul(next()));
        } else if (a == "--bbframe") {
            cfg.capture = tbs::CaptureMode::BBFrame;
        } else if (a == "--out") {
            cfg.file.enabled = true;
            cfg.file.path = next();
            cfg.file.timestampSuffix = false;
        } else if (a == "--timestamp") {
            cfg.file.timestampSuffix = true;
        } else if (a == "--bb-format") {
            const std::string v = next();
            cfg.file.bbFormat = v == "b8" ? tbs::BbFileFormat::B8Marker : v == "pseudo" ? tbs::BbFileFormat::PseudoTs : tbs::BbFileFormat::Raw;
        } else if (a == "--split-size") {
            cfg.file.splitSizeMB = std::stoull(next());
        } else if (a == "--split-time") {
            cfg.file.splitMinutes = std::stoi(next());
        } else if (a == "--keep") {
            cfg.file.keepFiles = std::stoi(next());
        } else if (a == "--min-free") {
            cfg.file.minFreeMB = std::stoull(next());
        } else if (a == "--buffer") {
            bufferMB = std::stoul(next());
        } else if (a == "--zmq") {
            cfg.zmq.enabled = true;
            cfg.zmq.endpoint = next();
        } else if (a == "--push") {
            cfg.zmq.socket = tbs::ZmqSocketKind::Push;
        } else if (a == "--connect") {
            cfg.zmq.bind = false;
        } else if (a == "--topic") {
            cfg.zmq.topic = next();
        } else if (a == "--header") {
            cfg.zmq.metadataHeader = true;
        } else if (a == "--packets") {
            cfg.zmq.tsPacketsPerMessage = std::stoi(next());
        } else if (a == "--net") {
            tbs::TsNetworkOutput o;
            std::string err;
            if (!tbs::parseNetworkUrl(next(), &o, &err)) {
                std::fprintf(stderr, "--net: %s\n", err.c_str());
                return 2;
            }
            cfg.network.push_back(o);
        } else if (a == "--analyze") {
            analyze = true;
        } else if (a == "--pcap") {
            analyze = true;
            procCfg.pcap.enabled = true;
            procCfg.pcap.path = next();
            procCfg.pcap.timestampSuffix = false;
        } else if (a == "--pcap-raw") {
            procCfg.pcap.ethernetFraming = false;
        } else if (a == "--pcapng") {
            procCfg.pcap.format = tbs::bb::PcapFormat::PcapNg;
        } else if (a == "--tun") {
            analyze = true;
            procCfg.tun.enabled = true;
            procCfg.tun.name = next();
        } else if (a == "--tap") {
            procCfg.tun.tap = true;
        } else if (a == "--duration") {
            duration = std::stod(next());
        } else if (a == "--quiet") {
            quiet = true;
        } else if (a == "--caps") {
            mode = Mode::Caps;
        } else if (a == "--spectrum" || a == "--blindscan") {
            mode = a == "--spectrum" ? Mode::Spectrum : Mode::BlindScan;
            if (!parseRange(next(), rangeA, rangeB)) {
                std::fprintf(stderr, "%s needs a range like 10700-12750\n", a.c_str());
                return 2;
            }
        } else if (a == "--sweep") {
            sweep = true;
        } else if (a == "--resolution") {
            resolution = std::stoi(next());
        } else if (a == "--fft-size") {
            fftSize = std::stoi(next());
        } else if (a == "--csv") {
            csv = next();
        } else if (a == "--max-sr") {
            maxSr = std::stoi(next());
        } else if (a == "--nit-scan") {
            mode = Mode::NitScan;
        } else if (a == "--nit-other") {
            nitOpt.includeNitOther = true;
        } else if (a == "--all-positions") {
            nitOpt.sameOrbitalPosition = false;
        } else {
            std::fprintf(stderr, "unknown option %s\n", a.c_str());
            usage(argv[0]);
            return 2;
        }
    }

    if (diagnoseOnly) {
        const auto items = tbs::Devices::diagnose(cfg.device);
        std::printf("%s", tbs::Devices::formatDiagnostics(items).c_str());
        for (const auto &i : items) {
            if (i.level == tbs::DiagnosticItem::Level::Error)
                return 1;
        }
        return 0;
    }

    std::signal(SIGINT, onSignal);
    std::signal(SIGTERM, onSignal);

    switch (mode) {
    case Mode::Caps: {
        const tbs::ScanCapabilities c = tbs::Scan::capabilities(cfg.device);
        std::printf("neumoDVB API     : %s\nspectrum (sweep) : %s\nspectrum (FFT)   : %s\nblind scan       : %s\n"
                    "constellation    : %s\n",
                    c.neumoApi ? "yes" : "no", c.spectrumSweep ? "yes" : "no", c.spectrumFft ? "yes" : "no",
                    c.blindScan ? "yes" : "no", c.constellation ? "yes" : "no");
        if (!c.note.empty())
            std::printf("%s\n", c.note.c_str());
        return c.probed ? 0 : 1;
    }
    case Mode::Spectrum:
        return runSpectrum(cfg, rangeA, rangeB, bothPol, sweep, resolution, fftSize, csv);
    case Mode::BlindScan:
        return runBlindScan(cfg, rangeA, rangeB, bothPol, maxSr);
    case Mode::NitScan:
        return runNitScan(cfg, nitOpt);
    case Mode::Receive:
        break;
    }

    tbs::Receiver rx;
    tbs::Receiver::Callbacks cb;
    cb.onLog = [](const std::string &m) { std::fprintf(stderr, "%s\n", m.c_str()); };
    cb.onStatus = [&](const tbs::FrontendStatus &st) {
        if (quiet)
            return;
        const tbs::StreamStats s = rx.stats();
        std::string level = st.hasSignalDbm ? std::to_string(static_cast<int>(st.signalDbm)) + " dBm"
            : st.hasSignalPercent           ? std::to_string(static_cast<int>(st.signalPercent)) + " %"
                                            : "-";
        char cnr[32] = "-";
        if (st.hasCnrDb)
            std::snprintf(cnr, sizeof(cnr), "%.1f dB", st.cnrDb);
        else if (st.hasCnrPercent)
            std::snprintf(cnr, sizeof(cnr), "%.0f %%", st.cnrPercent);
        std::fprintf(stderr, "\r[%s] level %s  C/N %s  %.2f Mbit/s  TS %llu  CC %llu  BB %llu (crc %llu)  buf %llu%%%s   ",
                     st.locked() ? "LOCK" : "----", level.c_str(), cnr, s.inputMbps,
                     static_cast<unsigned long long>(s.tsPackets), static_cast<unsigned long long>(s.ccErrors),
                     static_cast<unsigned long long>(s.bbFrames), static_cast<unsigned long long>(s.bbCrcErrors),
                     static_cast<unsigned long long>(s.pipelineCapacityBytes ? 100 * s.pipelineQueuedBytes / s.pipelineCapacityBytes : 0),
                     s.pipelineDroppedBytes ? " DROPPING" : "");
        for (const tbs::NetOutputStatus &n : s.network)
            std::fprintf(stderr, " NET %.2f Mbit/s%s", n.mbps, n.active ? "" : (" (" + n.state + ")").c_str());
    };
    std::shared_ptr<tbs::bb::Processor> proc;
    if (analyze) {
        proc = std::make_shared<tbs::bb::Processor>(procCfg);
        std::string perr;
        if (!proc->open(&perr)) {
            std::fprintf(stderr, "error: %s\n", perr.c_str());
            return 1;
        }
        const auto first = proc->snapshot();
        if (!first.tunWarning.empty())
            std::fprintf(stderr, "warning: %s\n", first.tunWarning.c_str());
        if (cfg.capture == tbs::CaptureMode::BBFrame)
            cb.onBbFrame = [proc](const uint8_t *f, size_t n, const tbs::BbHeader &h) { proc->pushBbFrame(f, n, h); };
        else
            cb.onTsPackets = [proc](const uint8_t *p, size_t n) { proc->pushTs(p, n); };
    }
    rx.setCallbacks(cb);
    rx.setStatusInterval(1000);
    rx.setPipelineBufferSize(bufferMB << 20);

    if (!rx.start(cfg))
        return 1;
    const auto t0 = std::chrono::steady_clock::now();
    while (!g_stop && rx.isRunning()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        if (duration > 0 && std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() >= duration)
            break;
    }
    rx.stop();
    std::fprintf(stderr, "\n");
    for (const tbs::NetOutputStatus &n : rx.stats().network)
        std::fprintf(stderr, "%s: %llu TS packets, %llu bytes, %llu send errors, %s\n", n.url.c_str(),
                     static_cast<unsigned long long>(n.packets), static_cast<unsigned long long>(n.bytes),
                     static_cast<unsigned long long>(n.sendErrors), n.state.c_str());
    if (proc) {
        proc->close();
        std::printf("%s", tbs::bb::formatReport(proc->snapshot()).c_str());
    }
    const std::string err = rx.lastError();
    if (!err.empty()) {
        std::fprintf(stderr, "error: %s\n", err.c_str());
        return 1;
    }
    return 0;
}
