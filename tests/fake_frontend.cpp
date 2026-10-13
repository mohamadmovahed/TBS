// LD_PRELOAD shim that simulates a DVB-S2 frontend with the neumoDVB API extensions
// (spectrum, blind scan, constellation) and a demux that plays a TS file.
// Used by the tests to exercise LinuxDvbDevice/LinuxDvbScan without hardware, and for
// demos of the scan dialog. Nothing in it is used by the library itself.
//
//   FAKE_DVB_ADAPTER  adapter number to simulate (default 97): /dev/dvb/adapterN/frontend0, demux0
//   FAKE_DVB_TS       transport stream file served by demux0 (looped, ~FAKE_DVB_MBPS Mbit/s, default 8)
//   FAKE_DVB_NEUMO=0  behave like a stock driver (no neumoDVB properties)
//
// The simulated sky (universal LNB, 9750/10600 MHz) is fixed, see kCarriers.

#include <linux/dvb/dmx.h>
#include <linux/dvb/frontend.h>

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <dlfcn.h>
#include <fcntl.h>
#include <mutex>
#include <poll.h>
#include <random>
#include <string>
#include <sys/eventfd.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <vector>

namespace {

// --- neumoDVB declarations (same layout as LinuxDvbScan.cpp) -----------------
constexpr uint32_t DTV_MATYPE_ = 71, DTV_ALGORITHM_ = 74, DTV_SEARCH_RANGE_ = 75, DTV_ISI_LIST_ = 76,
                   DTV_PLS_SEARCH_LIST_ = 77, DTV_SCAN_START_ = 79, DTV_SCAN_END_ = 80, DTV_SCAN_RES_ = 81,
                   DTV_SCAN_FFT_ = 82, DTV_SCAN_ = 83, DTV_SPECTRUM_ = 84, DTV_MAX_SR_ = 85, DTV_CONSTELLATION_ = 86;
struct SpectralPeak { int32_t freq, symbolRate, snr, level; };
struct Spectrum { uint32_t *freq; int32_t *rfLevel; SpectralPeak *candidates; uint32_t numFreq, numCandidates, scale; uint8_t method; };
struct IqSample { int16_t real, imag; };
struct Constellation { uint32_t numSamples; IqSample *samples; uint8_t method, constelSelect; };
struct ExtendedInfo
{
    char names[4][64];
    uint8_t supportsNeumo, numRfInputs;
    int8_t defaultRfInput;
    uint8_t supportsBbframes;
    int32_t reserved4;
    int64_t mac1, mac2;
    char unused[24];
    int8_t rfInputs[16];
    uint32_t fmin, fmax, fstep, ftol, srmin, srmax, srtol, caps, extendedCaps;
};
const unsigned long FE_GET_EXTENDED_INFO_ = _IOR('o', 86, ExtendedInfo);

struct Carrier
{
    double rfMHz;
    char pol; // 'H' or 'V'
    double srKsps;
    bool s2;
    int modulation; // enum fe_modulation
    int fec;        // enum fe_code_rate
    std::vector<int> isis;
    double levelDb; // above the noise floor
};

const std::vector<Carrier> kCarriers = {
    {10744.0, 'H', 22000, false, QPSK, FEC_5_6, {}, 14},
    {10832.0, 'V', 22000, true, PSK_8, FEC_3_4, {}, 12},
    {11053.0, 'H', 27500, true, PSK_8, FEC_2_3, {}, 16},
    {11494.0, 'H', 27500, true, PSK_8, FEC_3_4, {}, 13},
    {11597.0, 'V', 22000, false, QPSK, FEC_5_6, {}, 11},
    {11817.0, 'H', 27500, true, PSK_8, FEC_3_4, {}, 15},
    {12187.0, 'V', 27500, true, QPSK, FEC_3_4, {}, 10},
    {12603.0, 'H', 30000, true, APSK_16, FEC_3_4, {1, 2, 3}, 12},
};

using OpenFn = int (*)(const char *, int, ...);
using IoctlFn = int (*)(int, unsigned long, ...);
using CloseFn = int (*)(int);
using ReadFn = ssize_t (*)(int, void *, size_t);

template <typename T>
T real(const char *name)
{
    return reinterpret_cast<T>(dlsym(RTLD_NEXT, name));
}

struct State
{
    std::mutex mutex;
    int feFd = -1;  // eventfd standing in for the frontend
    int dmxFd = -1; // the TS file
    bool neumo = true;
    // LNB
    bool tone = false;
    int voltage = SEC_VOLTAGE_OFF;
    // properties being set
    uint32_t delsys = SYS_DVBS2, freq = 0, sr = 0, streamId = ~0u;
    uint32_t scanStart = 0, scanEnd = 0, resolution = 0;
    bool tuned = false;
    int locked = -1; // index into kCarriers
    // blind scan
    std::vector<int> pending;
    std::deque<uint32_t> events;
    // demux pacing
    double mbps = 8.0;
    double t0 = 0.0;
    uint64_t bytesOut = 0;
    std::mt19937 rng{1234};
};

State &st()
{
    static State s;
    return s;
}

std::string adapterPrefix()
{
    const char *a = std::getenv("FAKE_DVB_ADAPTER");
    return std::string("/dev/dvb/adapter") + (a ? a : "97") + "/";
}

double nowSec()
{
    timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
}

double loMHz(const State &s) { return s.tone ? 10600.0 : 9750.0; }
char polOf(const State &s) { return s.voltage == SEC_VOLTAGE_13 ? 'V' : 'H'; }
double ifToRf(const State &s, double ifKHz) { return loMHz(s) + ifKHz / 1000.0; }
uint32_t rfToIf(const State &s, double rfMHz) { return static_cast<uint32_t>(std::lround((rfMHz - loMHz(s)) * 1000.0)); }

bool visible(const State &s, const Carrier &c)
{
    if (c.pol != polOf(s) || s.voltage == SEC_VOLTAGE_OFF)
        return false;
    // the band an LNB setting can deliver
    return s.tone ? c.rfMHz >= 11700.0 : c.rfMHz < 11700.0;
}

void pushEvent(State &s, uint32_t status)
{
    s.events.push_back(status);
    const uint64_t one = 1;
    if (::write(s.feFd, &one, sizeof(one)) < 0) {
    }
}

double levelAt(State &s, double rfMHz)
{
    std::normal_distribution<double> noise(0.0, 0.4);
    double lin = 1.0; // noise floor, linear power
    for (const Carrier &c : kCarriers) {
        if (!visible(s, c))
            continue;
        const double half = c.srKsps / 2000.0; // MHz
        const double d = std::fabs(rfMHz - c.rfMHz);
        const double ro = 0.2 * half;
        double g = 0.0;
        if (d <= half - ro)
            g = 1.0;
        else if (d < half + ro)
            g = 0.5 * (1.0 + std::cos(M_PI * (d - (half - ro)) / (2.0 * ro)));
        lin += g * std::pow(10.0, c.levelDb / 10.0);
    }
    return -68.0 + 10.0 * std::log10(lin) + noise(s.rng);
}

int handleSet(State &s, dtv_properties *cmd)
{
    for (uint32_t i = 0; i < cmd->num; ++i) {
        dtv_property &p = cmd->props[i];
        const uint32_t v = p.u.data;
        if (!s.neumo && p.cmd >= DTV_MATYPE_ && p.cmd <= DTV_CONSTELLATION_ + 10) {
            errno = EINVAL;
            return -1;
        }
        switch (p.cmd) {
        case DTV_CLEAR:
            s.tuned = false;
            s.locked = -1;
            s.streamId = ~0u;
            break;
        case DTV_DELIVERY_SYSTEM: s.delsys = v; break;
        case DTV_FREQUENCY: s.freq = v; break;
        case DTV_SYMBOL_RATE: s.sr = v; break;
        case DTV_STREAM_ID: s.streamId = v; break;
        case DTV_VOLTAGE: s.voltage = static_cast<int>(v); break;
        case DTV_SCAN_START_: s.scanStart = v; break;
        case DTV_SCAN_END_: s.scanEnd = v; break;
        case DTV_SCAN_RES_: s.resolution = v; break;
        case DTV_TUNE: {
            s.tuned = true;
            s.locked = -1;
            const double rf = ifToRf(s, s.freq);
            for (size_t k = 0; k < kCarriers.size(); ++k)
                if (visible(s, kCarriers[k]) && std::fabs(kCarriers[k].rfMHz - rf) < 3.0)
                    s.locked = static_cast<int>(k);
            pushEvent(s, s.locked >= 0 ? FE_HAS_SIGNAL | FE_HAS_CARRIER | FE_HAS_VITERBI | FE_HAS_SYNC | FE_HAS_LOCK : FE_HAS_SIGNAL);
            break;
        }
        case DTV_SPECTRUM_:
            pushEvent(s, FE_HAS_SIGNAL | FE_HAS_SYNC);
            break;
        case DTV_SCAN_:
            if (v) { // init: the carriers in range, in frequency order
                s.pending.clear();
                for (size_t k = 0; k < kCarriers.size(); ++k) {
                    const Carrier &c = kCarriers[k];
                    if (!visible(s, c))
                        continue;
                    const uint32_t f = rfToIf(s, c.rfMHz);
                    if (f >= s.scanStart && f <= s.scanEnd)
                        s.pending.push_back(static_cast<int>(k));
                }
            }
            if (s.pending.empty()) {
                s.locked = -1;
                pushEvent(s, FE_TIMEDOUT);
            } else {
                s.locked = s.pending.front();
                s.pending.erase(s.pending.begin());
                pushEvent(s, FE_HAS_SIGNAL | FE_HAS_CARRIER | FE_HAS_VITERBI | FE_HAS_SYNC | FE_HAS_LOCK);
            }
            break;
        default:
            break;
        }
    }
    return 0;
}

void fillStat(dtv_property &p, int64_t milliDb)
{
    p.u.st.len = 1;
    p.u.st.stat[0].scale = FE_SCALE_DECIBEL;
    p.u.st.stat[0].svalue = milliDb;
}

int handleGet(State &s, dtv_properties *cmd)
{
    const Carrier *c = s.locked >= 0 ? &kCarriers[static_cast<size_t>(s.locked)] : nullptr;
    for (uint32_t i = 0; i < cmd->num; ++i) {
        dtv_property &p = cmd->props[i];
        if (!s.neumo && p.cmd >= DTV_MATYPE_ && p.cmd <= DTV_CONSTELLATION_ + 10) {
            errno = EINVAL;
            return -1;
        }
        switch (p.cmd) {
        case DTV_DELIVERY_SYSTEM: p.u.data = c ? (c->s2 ? SYS_DVBS2 : SYS_DVBS) : s.delsys; break;
        case DTV_FREQUENCY: p.u.data = c ? rfToIf(s, c->rfMHz) + 120 : s.freq; break; // a small offset, like a real lock
        case DTV_SYMBOL_RATE: p.u.data = c ? static_cast<uint32_t>(c->srKsps * 1000 - 3) : s.sr; break;
        case DTV_MODULATION: p.u.data = c ? static_cast<uint32_t>(c->modulation) : QPSK; break;
        case DTV_INNER_FEC: p.u.data = c ? static_cast<uint32_t>(c->fec) : FEC_AUTO; break;
        case DTV_ROLLOFF: p.u.data = ROLLOFF_20; break;
        case DTV_PILOT: p.u.data = PILOT_ON; break;
        case DTV_STREAM_ID: p.u.data = c && !c->isis.empty() ? static_cast<uint32_t>(c->isis.front()) : ~0u; break;
        case DTV_SCRAMBLING_SEQUENCE_INDEX: p.u.data = 0; break;
        case DTV_MATYPE_: p.u.data = c ? (c->isis.empty() ? 0xF0 : 0xD0) : 0; break;
        case DTV_ISI_LIST_:
            std::memset(p.u.buffer.data, 0, sizeof(p.u.buffer.data));
            p.u.buffer.len = 32;
            if (c)
                for (int isi : c->isis)
                    p.u.buffer.data[isi / 8] |= static_cast<uint8_t>(1u << (isi % 8));
            break;
        case DTV_STAT_SIGNAL_STRENGTH: fillStat(p, c ? -38000 - static_cast<int>(c->levelDb) * 100 : -68000); break;
        case DTV_STAT_CNR:
            if (c)
                fillStat(p, static_cast<int64_t>(c->levelDb * 1000));
            else
                p.u.st.len = 0;
            break;
        case DTV_SPECTRUM_: {
            Spectrum sp;
            std::memcpy(&sp, &p.u, sizeof(sp));
            const uint32_t step = s.resolution ? s.resolution : 500;
            uint32_t n = 0;
            for (uint32_t f = s.scanStart; f <= s.scanEnd && n < sp.numFreq; f += step, ++n) {
                sp.freq[n] = f;
                sp.rfLevel[n] = static_cast<int32_t>(std::lround(levelAt(s, ifToRf(s, f)) * 1000.0));
            }
            sp.numFreq = n;
            uint32_t m = 0;
            for (const Carrier &cc : kCarriers) {
                const uint32_t f = rfToIf(s, cc.rfMHz);
                if (visible(s, cc) && f >= s.scanStart && f <= s.scanEnd && m < sp.numCandidates)
                    sp.candidates[m++] = SpectralPeak{static_cast<int32_t>(f), static_cast<int32_t>(cc.srKsps * 1000),
                                                      static_cast<int32_t>(cc.levelDb * 1000),
                                                      static_cast<int32_t>((-68 + cc.levelDb) * 1000)};
            }
            sp.numCandidates = m;
            sp.scale = FE_SCALE_DECIBEL;
            std::memcpy(&p.u, &sp, sizeof(sp));
            break;
        }
        case DTV_CONSTELLATION_: {
            Constellation cs;
            std::memcpy(&cs, &p.u, sizeof(cs));
            if (!c) {
                cs.numSamples = 0;
            } else {
                std::normal_distribution<double> noise(0.0, 1200.0 / (1.0 + c->levelDb / 6.0));
                std::uniform_int_distribution<int> sym(0, 63);
                for (uint32_t k = 0; k < cs.numSamples; ++k) {
                    double x = 0, y = 0;
                    const int r = sym(s.rng);
                    if (c->modulation == APSK_16) { // 4 + 12
                        const bool inner = r % 4 == 0;
                        const double rad = inner ? 3000 : 8100;
                        const int n = inner ? 4 : 12;
                        const double a = 2 * M_PI * (r % n) / n + (inner ? M_PI / 4 : M_PI / 12);
                        x = rad * std::cos(a);
                        y = rad * std::sin(a);
                    } else {
                        const int n = c->modulation == PSK_8 ? 8 : 4;
                        const double a = 2 * M_PI * (r % n) / n + (n == 4 ? M_PI / 4 : 0);
                        x = 7000 * std::cos(a);
                        y = 7000 * std::sin(a);
                    }
                    cs.samples[k].real = static_cast<int16_t>(std::clamp(x + noise(s.rng), -32767.0, 32767.0));
                    cs.samples[k].imag = static_cast<int16_t>(std::clamp(y + noise(s.rng), -32767.0, 32767.0));
                }
            }
            std::memcpy(&p.u, &cs, sizeof(cs));
            break;
        }
        default:
            break;
        }
    }
    return 0;
}

int fakeFrontendIoctl(State &s, unsigned long req, void *arg)
{
    if (req == FE_GET_INFO) {
        auto *fi = static_cast<dvb_frontend_info *>(arg);
        std::memset(fi, 0, sizeof(*fi));
        std::snprintf(fi->name, sizeof(fi->name), "Simulated STiD135 (fake frontend)");
        fi->frequency_min = 950000;
        fi->frequency_max = 2150000;
        fi->symbol_rate_min = 1000000;
        fi->symbol_rate_max = 45000000;
        return 0;
    }
    if (req == FE_GET_EXTENDED_INFO_) {
        if (!s.neumo) {
            errno = ENOTTY;
            return -1;
        }
        auto *e = static_cast<ExtendedInfo *>(arg);
        std::memset(e, 0, sizeof(*e));
        e->supportsNeumo = 1;
        e->extendedCaps = 0x0F;
        return 0;
    }
    if (req == FE_SET_TONE) {
        s.tone = reinterpret_cast<uintptr_t>(arg) == SEC_TONE_ON;
        return 0;
    }
    if (req == FE_SET_VOLTAGE) {
        s.voltage = static_cast<int>(reinterpret_cast<uintptr_t>(arg));
        return 0;
    }
    if (req == FE_DISEQC_SEND_MASTER_CMD || req == FE_DISEQC_SEND_BURST)
        return 0;
    if (req == FE_SET_PROPERTY)
        return handleSet(s, static_cast<dtv_properties *>(arg));
    if (req == FE_GET_PROPERTY)
        return handleGet(s, static_cast<dtv_properties *>(arg));
    if (req == FE_READ_STATUS) {
        *static_cast<fe_status_t *>(arg) = static_cast<fe_status_t>(
            s.locked >= 0 && s.tuned ? FE_HAS_SIGNAL | FE_HAS_CARRIER | FE_HAS_VITERBI | FE_HAS_SYNC | FE_HAS_LOCK : 0);
        return 0;
    }
    if (req == FE_GET_EVENT) {
        if (s.events.empty()) {
            errno = EWOULDBLOCK;
            return -1;
        }
        auto *ev = static_cast<dvb_frontend_event *>(arg);
        std::memset(ev, 0, sizeof(*ev));
        ev->status = static_cast<fe_status_t>(s.events.front());
        s.events.pop_front();
        if (s.events.empty()) {
            uint64_t v;
            if (::read(s.feFd, &v, sizeof(v)) < 0) {
            }
        }
        return 0;
    }
    errno = ENOTTY;
    return -1;
}

int openFake(const char *path, int flags, mode_t mode, OpenFn next)
{
    if (!path || std::strncmp(path, "/dev/dvb/", 9) != 0)
        return next(path, flags, mode);
    const std::string prefix = adapterPrefix();
    State &s = st();
    std::lock_guard<std::mutex> lock(s.mutex);
    if (path == prefix + "frontend0") {
        if (s.feFd >= 0) {
            errno = EBUSY;
            return -1;
        }
        const char *n = std::getenv("FAKE_DVB_NEUMO");
        s.neumo = !(n && n[0] == '0');
        s.feFd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
        s.events.clear();
        s.locked = -1;
        s.tuned = false;
        return s.feFd;
    }
    if (path == prefix + "demux0") {
        const char *ts = std::getenv("FAKE_DVB_TS");
        if (!ts) {
            errno = ENOENT;
            return -1;
        }
        if (const char *m = std::getenv("FAKE_DVB_MBPS"))
            s.mbps = std::max(0.1, std::atof(m));
        s.dmxFd = next(ts, O_RDONLY | O_CLOEXEC, 0);
        s.t0 = nowSec();
        s.bytesOut = 0;
        return s.dmxFd;
    }
    return next(path, flags, mode);
}

} // namespace

extern "C" {

int open(const char *path, int flags, ...)
{
    mode_t mode = 0;
    if (flags & (O_CREAT | O_TMPFILE)) {
        va_list ap;
        va_start(ap, flags);
        mode = static_cast<mode_t>(va_arg(ap, int));
        va_end(ap);
    }
    static OpenFn next = real<OpenFn>("open");
    return openFake(path, flags, mode, next);
}

int open64(const char *path, int flags, ...)
{
    mode_t mode = 0;
    if (flags & (O_CREAT | O_TMPFILE)) {
        va_list ap;
        va_start(ap, flags);
        mode = static_cast<mode_t>(va_arg(ap, int));
        va_end(ap);
    }
    static OpenFn next = real<OpenFn>("open64");
    return openFake(path, flags, mode, next);
}

int ioctl(int fd, unsigned long req, ...)
{
    va_list ap;
    va_start(ap, req);
    void *arg = va_arg(ap, void *);
    va_end(ap);
    State &s = st();
    {
        std::lock_guard<std::mutex> lock(s.mutex);
        if (fd >= 0 && fd == s.feFd)
            return fakeFrontendIoctl(s, req, arg);
        if (fd >= 0 && fd == s.dmxFd)
            return 0; // DMX_SET_BUFFER_SIZE, DMX_SET_PES_FILTER, DMX_STOP
    }
    static IoctlFn next = real<IoctlFn>("ioctl");
    return next(fd, req, arg);
}

ssize_t read(int fd, void *buf, size_t len)
{
    static ReadFn next = real<ReadFn>("read");
    State &s = st();
    if (fd < 0 || fd != s.dmxFd)
        return next(fd, buf, len);
    // Pace the stream and loop the file
    const double due = s.t0 + static_cast<double>(s.bytesOut) * 8.0 / (s.mbps * 1e6);
    const double wait = due - nowSec();
    if (wait > 0)
        usleep(static_cast<useconds_t>(std::min(wait, 0.1) * 1e6));
    len = std::min<size_t>(len, 188 * 256);
    ssize_t n = next(fd, buf, len);
    if (n == 0) {
        lseek(fd, 0, SEEK_SET);
        n = next(fd, buf, len);
    }
    if (n > 0)
        s.bytesOut += static_cast<uint64_t>(n);
    return n;
}

int close(int fd)
{
    static CloseFn next = real<CloseFn>("close");
    State &s = st();
    {
        std::lock_guard<std::mutex> lock(s.mutex);
        if (fd >= 0 && fd == s.feFd)
            s.feFd = -1;
        if (fd >= 0 && fd == s.dmxFd)
            s.dmxFd = -1;
    }
    return next(fd);
}

} // extern "C"
