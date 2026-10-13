// Spectrum, blind scan and constellation for drivers with the neumoDVB extensions of the
// Linux DVB frontend API (github.com/deeptho/linux_media, "deepthought" branch). The
// constants and structures are not in the kernel headers, so the ones needed are declared
// here with the layout of that driver's include/uapi/linux/dvb/frontend.h.

#include "LinuxDvbDevice.h"

#include "../Util.h"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <linux/dvb/frontend.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <unistd.h>

namespace tbs {
namespace {

namespace neumo {

constexpr uint32_t DTV_ALGORITHM = 74;
constexpr uint32_t DTV_SEARCH_RANGE = 75;
constexpr uint32_t DTV_ISI_LIST = 76;
constexpr uint32_t DTV_PLS_SEARCH_LIST = 77;
constexpr uint32_t DTV_SCAN_START_FREQUENCY = 79;
constexpr uint32_t DTV_SCAN_END_FREQUENCY = 80;
constexpr uint32_t DTV_SCAN_RESOLUTION = 81;
constexpr uint32_t DTV_SCAN_FFT_SIZE = 82;
constexpr uint32_t DTV_SCAN = 83;
constexpr uint32_t DTV_SPECTRUM = 84;
constexpr uint32_t DTV_MAX_SYMBOL_RATE = 85;
constexpr uint32_t DTV_CONSTELLATION = 86;
constexpr uint32_t DTV_MATYPE = 71;

constexpr uint32_t SYS_AUTO = 22;
constexpr uint32_t ALGORITHM_BLIND = 3;

constexpr uint32_t FE_CAN_SPECTRUM_SWEEP = 0x01;
constexpr uint32_t FE_CAN_IQ = 0x02;
constexpr uint32_t FE_CAN_BLINDSEARCH = 0x04;
constexpr uint32_t FE_CAN_SPECTRUM_FFT = 0x08;

struct SpectralPeak
{
    int32_t freq;        // IF kHz
    int32_t symbolRate;  // S/s
    int32_t snr;         // 0.001 dB
    int32_t level;       // 0.001 dB
};

struct Spectrum
{
    uint32_t *freq;
    int32_t *rfLevel;
    SpectralPeak *candidates;
    uint32_t numFreq;
    uint32_t numCandidates;
    uint32_t scale;
    uint8_t method;
};

struct ConstellationSample
{
    int16_t real;
    int16_t imag;
};

struct Constellation
{
    uint32_t numSamples;
    ConstellationSample *samples;
    uint8_t method;
    uint8_t constelSelect;
};

struct PlsSearchList
{
    int numCodes;
    uint32_t *codes;
};

struct ExtendedInfo
{
    char cardName[64];
    char adapterName[64];
    char cardAddress[64];
    char cardShortName[64];
    uint8_t supportsNeumo;
    uint8_t numRfInputs;
    int8_t defaultRfInput;
    uint8_t supportsBbframes;
    int32_t reserved4;
    int64_t cardMacAddress;
    int64_t adapterMacAddress;
    char unused[64 - 24 - 16];
    int8_t rfInputs[16];
    uint32_t frequencyMin;
    uint32_t frequencyMax;
    uint32_t frequencyStepsize;
    uint32_t frequencyTolerance;
    uint32_t symbolRateMin;
    uint32_t symbolRateMax;
    uint32_t symbolRateTolerance;
    uint32_t caps;         // enum fe_caps
    uint32_t extendedCaps; // enum fe_extended_caps
};

const unsigned long FE_GET_EXTENDED_INFO = _IOR('o', 86, ExtendedInfo);

static_assert(sizeof(Spectrum) <= sizeof(dtv_property::u), "spectrum descriptor must fit the property union");
static_assert(sizeof(Constellation) <= sizeof(dtv_property::u), "constellation descriptor must fit the property union");

} // namespace neumo

int xioctl(int fd, unsigned long req, void *arg)
{
    int r;
    do {
        r = ::ioctl(fd, req, arg);
    } while (r < 0 && errno == EINTR);
    return r;
}

dtv_property makeProp(uint32_t cmd, uint32_t data = 0)
{
    dtv_property p;
    std::memset(&p, 0, sizeof(p));
    p.cmd = cmd;
    p.u.data = data;
    return p;
}

template <typename T>
dtv_property makeStructProp(uint32_t cmd, const T &value)
{
    dtv_property p = makeProp(cmd);
    std::memcpy(&p.u, &value, sizeof(T));
    return p;
}

template <typename T>
T structOf(const dtv_property &p)
{
    T v;
    std::memcpy(&v, &p.u, sizeof(T));
    return v;
}

bool setProps(int fd, std::vector<dtv_property> &props)
{
    dtv_properties cmd{static_cast<uint32_t>(props.size()), props.data()};
    return xioctl(fd, FE_SET_PROPERTY, &cmd) == 0;
}

std::string notSupported(const char *what)
{
    return util::format("%s is not supported by this driver: it needs the neumoDVB frontend API "
                        "(driver from github.com/deeptho/linux_media, branch deepthought)",
                        what);
}

bool unsupportedErrno(int err)
{
    return err == EINVAL || err == ENOTSUP || err == EOPNOTSUPP || err == ENOTTY || err == 524 /* ENOTSUPP */;
}

uint32_t voltageFor(Polarization p)
{
    switch (p) {
    case Polarization::VerticalRight13V:
        return SEC_VOLTAGE_13;
    case Polarization::HorizontalLeft18V:
        return SEC_VOLTAGE_18;
    case Polarization::Off:
        break;
    }
    return SEC_VOLTAGE_OFF;
}

} // namespace

bool LinuxDvbDevice::openFrontend(const DeviceSelection &sel, std::string *error)
{
    close();
    m_sel = sel;
    const std::string path = util::format("/dev/dvb/adapter%d/frontend%d", sel.adapter, sel.frontend);
    m_fe = ::open(path.c_str(), O_RDWR | O_NONBLOCK);
    if (m_fe < 0) {
        if (error)
            *error = util::format("Cannot open %s: %s%s", path.c_str(), std::strerror(errno),
                                  errno == EBUSY ? " (stop the receiver using this tuner first)" : "");
        return false;
    }
    dvb_frontend_info fi;
    std::memset(&fi, 0, sizeof(fi));
    if (xioctl(m_fe, FE_GET_INFO, &fi) == 0)
        m_name = fi.name;
    return true;
}

ScanCapabilities LinuxDvbDevice::scanCapabilities()
{
    ScanCapabilities caps;
    if (m_fe < 0) {
        caps.note = "Frontend not open";
        return caps;
    }
    caps.probed = true;
    neumo::ExtendedInfo info;
    std::memset(&info, 0, sizeof(info));
    if (xioctl(m_fe, neumo::FE_GET_EXTENDED_INFO, &info) != 0) {
        caps.note = notSupported("Spectrum analysis, blind scan and constellation display");
        return caps;
    }
    caps.neumoApi = true;
    caps.spectrumSweep = info.extendedCaps & neumo::FE_CAN_SPECTRUM_SWEEP;
    caps.spectrumFft = info.extendedCaps & neumo::FE_CAN_SPECTRUM_FFT;
    caps.blindScan = info.extendedCaps & neumo::FE_CAN_BLINDSEARCH;
    caps.constellation = info.extendedCaps & neumo::FE_CAN_IQ;
    if (!caps.spectrumSweep && !caps.spectrumFft && !caps.blindScan && !caps.constellation)
        caps.note = "The driver has the neumoDVB API but this frontend reports no spectrum, blind scan or IQ support";
    return caps;
}

void LinuxDvbDevice::drainEvents()
{
    dvb_frontend_event ev;
    for (int i = 0; i < 64; ++i) {
        std::memset(&ev, 0, sizeof(ev));
        if (::ioctl(m_fe, FE_GET_EVENT, &ev) < 0)
            break;
    }
}

int LinuxDvbDevice::waitEvent(int timeoutMs, const std::function<bool()> &cancelled)
{
    const int64_t deadline = util::nowMs() + timeoutMs;
    for (;;) {
        if (cancelled && cancelled())
            return -1;
        const int64_t left = deadline - util::nowMs();
        if (left <= 0)
            return 0;
        pollfd pfd{m_fe, POLLIN | POLLPRI, 0};
        const int pr = ::poll(&pfd, 1, static_cast<int>(std::min<int64_t>(left, 200)));
        if (pr < 0 && errno != EINTR)
            return -1;
        if (pr <= 0)
            continue;
        dvb_frontend_event ev;
        std::memset(&ev, 0, sizeof(ev));
        if (::ioctl(m_fe, FE_GET_EVENT, &ev) < 0) {
            if (errno == EWOULDBLOCK || errno == EINTR)
                continue;
            if (errno == EOVERFLOW) // events were lost; the next one is current
                continue;
            return -1;
        }
        const int st = static_cast<int>(ev.status);
        if (st & (FE_HAS_SYNC | FE_HAS_LOCK | FE_TIMEDOUT))
            return st;
    }
}

bool LinuxDvbDevice::readSpectrum(const Scan::BandSegment &seg, SpectrumResult *out, std::string *error)
{
    constexpr uint32_t kMax = 65536;
    std::vector<uint32_t> freq(kMax);
    std::vector<int32_t> level(kMax);
    std::vector<neumo::SpectralPeak> cand(kMax);
    neumo::Spectrum sp;
    std::memset(&sp, 0, sizeof(sp));
    sp.freq = freq.data();
    sp.rfLevel = level.data();
    sp.candidates = cand.data();
    sp.numFreq = kMax;
    sp.numCandidates = kMax;

    std::vector<dtv_property> props{makeStructProp(neumo::DTV_SPECTRUM, sp)};
    dtv_properties cmd{1, props.data()};
    if (xioctl(m_fe, FE_GET_PROPERTY, &cmd) < 0) {
        if (error)
            *error = unsupportedErrno(errno) ? notSupported("Spectrum analysis") : "Reading the spectrum failed: " + std::string(std::strerror(errno));
        return false;
    }
    sp = structOf<neumo::Spectrum>(props[0]);
    const uint32_t n = std::min(sp.numFreq, kMax);
    const uint32_t nc = std::min(sp.numCandidates, kMax);
    out->decibel = sp.scale != FE_SCALE_RELATIVE;
    const double levelScale = out->decibel ? 0.001 : 100.0 / 65535.0;
    for (uint32_t i = 0; i < n; ++i) {
        const double rf = Scan::ifToRfMHz(seg.tuning, freq[i]);
        if (rf < seg.startMHz - 1e-6 || rf > seg.endMHz + 1e-6)
            continue;
        out->points.push_back({rf, level[i] * levelScale});
    }
    for (uint32_t i = 0; i < nc; ++i) {
        SpectrumPeak p;
        p.frequencyMHz = Scan::ifToRfMHz(seg.tuning, cand[i].freq);
        if (p.frequencyMHz < seg.startMHz || p.frequencyMHz > seg.endMHz)
            continue;
        p.symbolRateKsps = cand[i].symbolRate / 1000.0;
        p.level = cand[i].level * 0.001;
        p.snrDb = cand[i].snr * 0.001;
        out->peaks.push_back(p);
    }
    return true;
}

bool LinuxDvbDevice::spectrumSegment(const Scan::BandSegment &seg, const SpectrumRequest &req, SpectrumResult *out,
                                     std::string *error, const std::function<bool()> &cancelled)
{
    drainEvents();
    std::vector<dtv_property> clear{makeProp(DTV_CLEAR)};
    setProps(m_fe, clear);
    if (!setupSec(seg.tuning, error))
        return false;

    std::vector<dtv_property> props;
    props.push_back(makeProp(DTV_DELIVERY_SYSTEM, SYS_DVBS2));
    props.push_back(makeProp(neumo::DTV_SCAN_START_FREQUENCY, seg.ifStartKHz));
    props.push_back(makeProp(neumo::DTV_SCAN_END_FREQUENCY, seg.ifEndKHz));
    if (req.resolutionKHz > 0)
        props.push_back(makeProp(neumo::DTV_SCAN_RESOLUTION, static_cast<uint32_t>(req.resolutionKHz)));
    props.push_back(makeProp(neumo::DTV_SCAN_FFT_SIZE, static_cast<uint32_t>(std::max(32, req.fftSize))));
    props.push_back(makeProp(DTV_SYMBOL_RATE, 2000000)); // tuner bandwidth
    props.push_back(makeProp(neumo::DTV_SPECTRUM, req.method == SpectrumMethod::Fft ? 1u : 0u));
    if (!setProps(m_fe, props)) {
        if (error)
            *error = unsupportedErrno(errno) ? notSupported("Spectrum analysis")
                                             : "Starting the spectrum scan failed: " + std::string(std::strerror(errno));
        return false;
    }
    log(util::format("Spectrum %.1f-%.1f MHz (IF %.1f-%.1f MHz), %s", seg.startMHz, seg.endMHz, seg.ifStartKHz / 1000.0,
                     seg.ifEndKHz / 1000.0, req.method == SpectrumMethod::Fft ? "FFT" : "sweep"));

    const int st = waitEvent(req.timeoutMs, cancelled);
    if (st < 0) {
        if (error)
            *error = cancelled && cancelled() ? "Cancelled" : "Waiting for the spectrum failed: " + std::string(std::strerror(errno));
        return false;
    }
    if (st == 0) {
        if (error)
            *error = "The driver did not finish the spectrum scan in time";
        return false;
    }
    return readSpectrum(seg, out, error);
}

bool LinuxDvbDevice::readCarrier(const Scan::BandSegment &seg, BlindScanCarrier *c, double *ifKHz)
{
    dtv_property props[13] = {
        makeProp(DTV_DELIVERY_SYSTEM), makeProp(DTV_FREQUENCY),     makeProp(DTV_SYMBOL_RATE),
        makeProp(DTV_MODULATION),      makeProp(DTV_INNER_FEC),     makeProp(DTV_ROLLOFF),
        makeProp(DTV_PILOT),           makeProp(DTV_STREAM_ID),     makeProp(neumo::DTV_ISI_LIST),
        makeProp(neumo::DTV_MATYPE),   makeProp(DTV_STAT_SIGNAL_STRENGTH), makeProp(DTV_STAT_CNR),
        makeProp(DTV_SCRAMBLING_SEQUENCE_INDEX),
    };
    dtv_properties cmd{13, props};
    if (xioctl(m_fe, FE_GET_PROPERTY, &cmd) < 0)
        return false;
    const uint32_t delsys = props[0].u.data;
    *ifKHz = props[1].u.data;
    c->frequencyMHz = Scan::ifToRfMHz(seg.tuning, props[1].u.data);
    c->symbolRateKsps = props[2].u.data / 1000.0;
    c->deliverySystem = delsys == SYS_DVBS ? DeliverySystem::DvbS : DeliverySystem::DvbS2;
    c->modulation = static_cast<int>(props[3].u.data);
    c->fec = static_cast<int>(props[4].u.data);
    c->rolloff = static_cast<int>(props[5].u.data);
    c->pilot = static_cast<int>(props[6].u.data);
    const uint32_t streamId = props[7].u.data;
    // 256-bit set of the ISIs present (the driver fills the first 32 bytes of the buffer)
    for (int isi = 0; isi < 256 && isi / 8 < static_cast<int>(sizeof(props[8].u.buffer.data)); ++isi) {
        if (props[8].u.buffer.data[isi / 8] & (1u << (isi % 8)))
            c->isis.push_back(isi);
    }
    c->matype = static_cast<int>(props[9].u.data);
    if (props[10].u.st.len > 0 && props[10].u.st.stat[0].scale == FE_SCALE_DECIBEL) {
        c->hasSignalDbm = true;
        c->signalDbm = props[10].u.st.stat[0].svalue / 1000.0;
    }
    if (props[11].u.st.len > 0 && props[11].u.st.stat[0].scale == FE_SCALE_DECIBEL) {
        c->hasCnrDb = true;
        c->cnrDb = props[11].u.st.stat[0].svalue / 1000.0;
    }

    c->tuning = seg.tuning;
    c->tuning.frequencyMHz = c->frequencyMHz;
    c->tuning.symbolRateKsps = c->symbolRateKsps;
    c->tuning.deliverySystem = c->deliverySystem;
    c->tuning.modulation = ModulationHint::Auto;
    c->tuning.tone = ToneMode::Auto;
    if (streamId != ~0u && c->isis.size() > 1) {
        c->tuning.inputStreamId = static_cast<int>(streamId & 0xFF);
        c->tuning.plsMode = PlsMode::Gold;
        c->tuning.plsCode = (streamId >> 8) & 0x3FFFF;
    } else if (c->isis.size() <= 1) {
        c->tuning.inputStreamId = -1;
        const uint32_t gold = props[12].u.data & 0x3FFFF;
        c->tuning.plsMode = PlsMode::Gold;
        c->tuning.plsCode = gold;
    }
    return true;
}

bool LinuxDvbDevice::blindScanSegment(const Scan::BandSegment &seg, const BlindScanRequest &req,
                                      std::vector<BlindScanCarrier> *out, SpectrumResult *spectrumOut, std::string *error,
                                      const std::function<bool()> &cancelled,
                                      const std::function<void(const BlindScanCarrier &, double ifKHz)> &onCarrier)
{
    drainEvents();
    std::vector<dtv_property> clear{makeProp(DTV_CLEAR)};
    setProps(m_fe, clear);
    if (!setupSec(seg.tuning, error))
        return false;

    std::vector<uint32_t> plsCodes = req.plsGoldCodes;
    auto start = [&](bool init) {
        std::vector<dtv_property> props;
        if (init) {
            props.push_back(makeProp(DTV_DELIVERY_SYSTEM, neumo::SYS_AUTO));
            props.push_back(makeProp(neumo::DTV_ALGORITHM, neumo::ALGORITHM_BLIND));
            props.push_back(makeProp(neumo::DTV_SCAN_START_FREQUENCY, seg.ifStartKHz));
            props.push_back(makeProp(neumo::DTV_SCAN_END_FREQUENCY, seg.ifEndKHz));
            props.push_back(makeProp(neumo::DTV_SCAN_RESOLUTION, static_cast<uint32_t>(std::max(1, req.resolutionKHz))));
            props.push_back(makeProp(neumo::DTV_SCAN_FFT_SIZE, static_cast<uint32_t>(std::max(32, req.fftSize))));
            props.push_back(makeProp(DTV_VOLTAGE, voltageFor(seg.tuning.polarization)));
            props.push_back(makeProp(neumo::DTV_MAX_SYMBOL_RATE, static_cast<uint32_t>(req.maxSymbolRateKsps) * 1000u));
        } else {
            std::vector<dtv_property> c{makeProp(DTV_CLEAR)};
            setProps(m_fe, c);
        }
        props.push_back(makeProp(neumo::DTV_SEARCH_RANGE, static_cast<uint32_t>(req.searchRangeKHz) * 1000u));
        if (!plsCodes.empty()) {
            neumo::PlsSearchList list{static_cast<int>(plsCodes.size()), plsCodes.data()};
            props.push_back(makeStructProp(neumo::DTV_PLS_SEARCH_LIST, list));
        }
        props.push_back(makeProp(DTV_STREAM_ID, seg.tuning.dvbStreamId()));
        props.push_back(makeProp(neumo::DTV_SCAN, init ? 1u : 0u));
        return setProps(m_fe, props);
    };

    if (!start(true)) {
        if (error)
            *error = unsupportedErrno(errno) ? notSupported("Blind scan") : "Starting the blind scan failed: " + std::string(std::strerror(errno));
        return false;
    }
    log(util::format("Blind scan %.1f-%.1f MHz (IF %.1f-%.1f MHz), %s", seg.startMHz, seg.endMHz, seg.ifStartKHz / 1000.0,
                     seg.ifEndKHz / 1000.0, seg.tuning.polarization == Polarization::VerticalRight13V ? "V" : "H"));

    bool first = true;
    for (;;) {
        const int st = waitEvent(req.timeoutMs, cancelled);
        if (st < 0) {
            if (error)
                *error = cancelled && cancelled() ? "Cancelled" : "Waiting for the driver failed: " + std::string(std::strerror(errno));
            return false;
        }
        if (st == 0) {
            if (error)
                *error = "The driver did not report progress in time";
            return false;
        }
        const bool found = st & FE_HAS_LOCK;
        const bool done = st & FE_TIMEDOUT;
        if (first && spectrumOut) {
            std::string ignored;
            readSpectrum(seg, spectrumOut, &ignored); // the spectrum the search was based on
        }
        first = false;
        if (found) {
            BlindScanCarrier c;
            double ifKHz = 0;
            if (readCarrier(seg, &c, &ifKHz)) {
                out->push_back(c);
                if (onCarrier)
                    onCarrier(c, ifKHz);
            }
        }
        if (done)
            return true;
        if (found && !start(false)) {
            if (error)
                *error = "Continuing the blind scan failed: " + std::string(std::strerror(errno));
            return false;
        }
    }
}

bool LinuxDvbDevice::readConstellation(std::vector<ConstellationSample> *out, int maxSamples, std::string *error)
{
    if (m_fe < 0 || m_constellationState < 0) {
        if (error)
            *error = m_fe < 0 ? "Frontend not open" : notSupported("Constellation display");
        return false;
    }
    maxSamples = std::clamp(maxSamples, 16, 8192);
    std::vector<neumo::ConstellationSample> buf(static_cast<size_t>(maxSamples));
    neumo::Constellation cs;
    std::memset(&cs, 0, sizeof(cs));
    cs.numSamples = static_cast<uint32_t>(maxSamples);
    cs.samples = buf.data();
    dtv_property p = makeStructProp(neumo::DTV_CONSTELLATION, cs);
    dtv_properties cmd{1, &p};
    if (xioctl(m_fe, FE_GET_PROPERTY, &cmd) < 0) {
        const int err = errno;
        if (m_constellationState == 0 && unsupportedErrno(err)) {
            m_constellationState = -1;
            log("Constellation display: " + notSupported("IQ sample readout"));
        }
        if (error)
            *error = unsupportedErrno(err) ? notSupported("Constellation display") : std::string(std::strerror(err));
        return false;
    }
    m_constellationState = 1;
    cs = structOf<neumo::Constellation>(p);
    const uint32_t n = std::min<uint32_t>(cs.numSamples, static_cast<uint32_t>(maxSamples));
    out->resize(n);
    for (uint32_t i = 0; i < n; ++i)
        (*out)[i] = ConstellationSample{buf[i].real, buf[i].imag};
    return true;
}

} // namespace tbs
