// Generates test input for the receiver's "File replay" device.
//
//   tbs-make-test-stream --out FILE [--in input.ts] [--dfl BYTES] [--hem] [--packets N]
//
// Wraps a transport stream (input file, or a synthetic one) into DVB-S2 BBFrames
// and writes them as STiD135 pseudo-TS (PID 0x010E), i.e. exactly what a TBS 6903x
// delivers with "options stid135 bbframe=1". Replay it with capture mode = BBFrames.

#include "../tests/StreamBuilders.h"

using tbs::BbHeader;

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>

using builders::Bytes;

int main(int argc, char **argv)
{
    std::string in;
    std::string out;
    size_t dfl = 7274 - 10; // QPSK 9/10 normal frame data field
    bool hem = false;
    size_t synthPackets = 20000;

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
        else if (a == "--packets")
            synthPackets = std::stoul(next());
        else {
            std::cerr << "usage: " << argv[0] << " --out FILE [--in input.ts] [--dfl BYTES] [--hem] [--packets N]\n";
            return 2;
        }
    }
    if (out.empty() || dfl < 16 || dfl > 7264) {
        std::cerr << "usage: " << argv[0] << " --out FILE [--in input.ts] [--dfl 16..7264] [--hem] [--packets N]\n";
        return 2;
    }

    std::vector<Bytes> packets;
    if (!in.empty()) {
        std::ifstream f(in, std::ios::binary);
        if (!f) {
            std::cerr << "cannot open " << in << "\n";
            return 1;
        }
        Bytes p(188);
        while (f.read(reinterpret_cast<char *>(p.data()), 188)) {
            if (p[0] != 0x47) {
                std::cerr << "input is not aligned 188-byte TS\n";
                return 1;
            }
            packets.push_back(p);
        }
    } else {
        for (size_t i = 0; i < synthPackets; ++i)
            packets.push_back(builders::makeTsPacket(0x0100, static_cast<uint8_t>(i), static_cast<uint8_t>(i * 7)));
    }

    const auto frames = builders::tsToBbFrames(packets, dfl, hem);
    std::ofstream o(out, std::ios::binary);
    if (!o) {
        std::cerr << "cannot create " << out << "\n";
        return 1;
    }
    uint8_t cc = 0;
    size_t tsOut = 0;
    for (const Bytes &fr : frames) {
        for (const Bytes &p : builders::bbFrameToPseudoTs(fr, cc)) {
            o.write(reinterpret_cast<const char *>(p.data()), 188);
            ++tsOut;
        }
    }
    std::cout << packets.size() << " TS packets -> " << frames.size() << " BBFrames ("
              << (hem ? "HEM" : "NM") << ", DFL " << dfl * 8 << " bits) -> " << tsOut << " pseudo-TS packets\n";
    return 0;
}
