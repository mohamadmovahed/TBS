#include "LinuxDvbDevice.h"

#include "../Util.h"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <linux/dvb/dmx.h>
#include <linux/dvb/frontend.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <unistd.h>

// TBS driver extension: SoC temperature of the STiD135 (degrees C)
#ifndef FE_READ_TEMP
#define FE_READ_TEMP _IOR('o', 98, __s16)
#endif

namespace tbs {
namespace {

std::string errnoString()
{
    return std::strerror(errno);
}

std::string frontendPath(int adapter, int frontend)
{
    return util::format("/dev/dvb/adapter%d/frontend%d", adapter, frontend);
}

std::string demuxPath(int adapter, int demux)
{
    return util::format("/dev/dvb/adapter%d/demux%d", adapter, demux);
}

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

// Numeric suffix of names like "adapter12"; -1 if none
int numberAfter(const char *name, const char *prefix)
{
    const size_t n = std::strlen(prefix);
    if (std::strncmp(name, prefix, n) != 0 || !name[n])
        return -1;
    char *end = nullptr;
    const long v = std::strtol(name + n, &end, 10);
    return (*end == 0 && v >= 0) ? static_cast<int>(v) : -1;
}

std::vector<int> listNumbered(const std::string &dir, const char *prefix)
{
    std::vector<int> out;
    if (DIR *d = ::opendir(dir.c_str())) {
        while (dirent *e = ::readdir(d)) {
            const int n = numberAfter(e->d_name, prefix);
            if (n >= 0)
                out.push_back(n);
        }
        ::closedir(d);
    }
    std::sort(out.begin(), out.end());
    return out;
}

} // namespace

LinuxDvbDevice::~LinuxDvbDevice()
{
    close();
}

std::vector<DeviceInfo> LinuxDvbDevice::enumerate()
{
    std::vector<DeviceInfo> list;
    for (int adapter : listNumbered("/dev/dvb", "adapter")) {
        for (int frontend : listNumbered(util::format("/dev/dvb/adapter%d", adapter), "frontend")) {
            DeviceInfo info;
            info.kind = DeviceKind::LinuxDvb;
            info.adapter = adapter;
            info.frontend = frontend;
            info.path = frontendPath(adapter, frontend);
            info.name = "unknown (busy or no permission)";
            const int fd = ::open(info.path.c_str(), O_RDONLY | O_NONBLOCK);
            if (fd >= 0) {
                dvb_frontend_info fi;
                std::memset(&fi, 0, sizeof(fi));
                if (xioctl(fd, FE_GET_INFO, &fi) == 0)
                    info.name = fi.name;
                ::close(fd);
            }
            list.push_back(info);
        }
    }
    return list;
}

bool LinuxDvbDevice::open(const DeviceSelection &sel, const TuningParams &tuning, CaptureMode, std::string *error)
{
    close();
    m_sel = sel;

    const std::string fePath = frontendPath(sel.adapter, sel.frontend);
    m_fe = ::open(fePath.c_str(), O_RDWR | O_NONBLOCK);
    if (m_fe < 0) {
        if (error)
            *error = util::format("Cannot open %s: %s (is the TBS driver loaded and the device free?)", fePath.c_str(),
                                  errnoString().c_str());
        return false;
    }

    dvb_frontend_info fi;
    std::memset(&fi, 0, sizeof(fi));
    if (xioctl(m_fe, FE_GET_INFO, &fi) == 0) {
        m_name = fi.name;
        log(util::format("Opened %s: %s (%u-%u MHz, %u-%u kS/s)", fePath.c_str(), m_name.c_str(), fi.frequency_min / 1000,
                         fi.frequency_max / 1000, fi.symbol_rate_min / 1000, fi.symbol_rate_max / 1000));
    }

    if (!tune(tuning, error)) {
        close();
        return false;
    }
    return true;
}

bool LinuxDvbDevice::setupSec(const TuningParams &t, std::string *error)
{
    // Tone off while switching voltage and sending DiSEqC
    if (xioctl(m_fe, FE_SET_TONE, reinterpret_cast<void *>(SEC_TONE_OFF)) < 0)
        log("FE_SET_TONE(off) failed: " + errnoString());

    fe_sec_voltage_t voltage = SEC_VOLTAGE_OFF;
    switch (t.polarization) {
    case Polarization::VerticalRight13V:
        voltage = SEC_VOLTAGE_13;
        break;
    case Polarization::HorizontalLeft18V:
        voltage = SEC_VOLTAGE_18;
        break;
    case Polarization::Off:
        voltage = SEC_VOLTAGE_OFF;
        break;
    }
    if (xioctl(m_fe, FE_SET_VOLTAGE, reinterpret_cast<void *>(voltage)) < 0) {
        if (error)
            *error = "FE_SET_VOLTAGE failed: " + errnoString();
        return false;
    }
    util::sleepMs(20);

    if (t.diseqcPort >= 0) {
        dvb_diseqc_master_cmd cmd;
        std::memset(&cmd, 0, sizeof(cmd));
        // DiSEqC 1.0 "write N0": framing E0, any LNB/switcher 10, command 38
        cmd.msg[0] = 0xE0;
        cmd.msg[1] = 0x10;
        cmd.msg[2] = 0x38;
        cmd.msg[3] = static_cast<uint8_t>(0xF0 | ((t.diseqcPort & 0x3) << 2)
                                          | (t.polarization == Polarization::HorizontalLeft18V ? 0x02 : 0x00)
                                          | (t.toneOn() ? 0x01 : 0x00));
        cmd.msg_len = 4;
        if (xioctl(m_fe, FE_DISEQC_SEND_MASTER_CMD, &cmd) < 0) {
            if (error)
                *error = "FE_DISEQC_SEND_MASTER_CMD failed: " + errnoString();
            return false;
        }
        util::sleepMs(20);
        // Tone burst for simple A/B switches
        const fe_sec_mini_cmd_t burst = (t.diseqcPort & 1) ? SEC_MINI_B : SEC_MINI_A;
        if (xioctl(m_fe, FE_DISEQC_SEND_BURST, reinterpret_cast<void *>(burst)) < 0)
            log("FE_DISEQC_SEND_BURST failed: " + errnoString());
        util::sleepMs(20);
        log(util::format("DiSEqC committed port %c sent", 'A' + t.diseqcPort));
    }

    const fe_sec_tone_mode_t tone = t.toneOn() ? SEC_TONE_ON : SEC_TONE_OFF;
    if (xioctl(m_fe, FE_SET_TONE, reinterpret_cast<void *>(tone)) < 0) {
        if (error)
            *error = "FE_SET_TONE failed: " + errnoString();
        return false;
    }
    util::sleepMs(20);
    return true;
}

bool LinuxDvbDevice::tune(const TuningParams &t, std::string *error)
{
    if (m_fe < 0) {
        if (error)
            *error = "Frontend not open";
        return false;
    }
    if (!setupSec(t, error))
        return false;

    dtv_property clear = makeProp(DTV_CLEAR);
    dtv_properties clearCmd{1, &clear};
    if (xioctl(m_fe, FE_SET_PROPERTY, &clearCmd) < 0)
        log("DTV_CLEAR failed: " + errnoString());

    const bool s2 = t.deliverySystem == DeliverySystem::DvbS2;
    std::vector<dtv_property> props;
    props.push_back(makeProp(DTV_DELIVERY_SYSTEM, s2 ? SYS_DVBS2 : SYS_DVBS));
    props.push_back(makeProp(DTV_FREQUENCY, t.intermediateFrequencyKHz()));
    props.push_back(makeProp(DTV_SYMBOL_RATE, t.symbolRateSps()));
    props.push_back(makeProp(DTV_INNER_FEC, FEC_AUTO));
    props.push_back(makeProp(DTV_INVERSION, INVERSION_AUTO));
    if (s2) {
        props.push_back(makeProp(DTV_ROLLOFF, ROLLOFF_AUTO));
        props.push_back(makeProp(DTV_PILOT, PILOT_AUTO));
    }
    switch (t.modulation) {
    case ModulationHint::Auto:
        break;
    case ModulationHint::Qpsk:
        props.push_back(makeProp(DTV_MODULATION, QPSK));
        break;
    case ModulationHint::Psk8:
        props.push_back(makeProp(DTV_MODULATION, PSK_8));
        break;
    case ModulationHint::Apsk16:
        props.push_back(makeProp(DTV_MODULATION, APSK_16));
        break;
    case ModulationHint::Apsk32:
        props.push_back(makeProp(DTV_MODULATION, APSK_32));
        break;
    }
    props.push_back(makeProp(DTV_STREAM_ID, t.dvbStreamId()));
    // Without an ISI filter the PLS can only be passed as a gold code
    if (t.inputStreamId < 0 && t.goldCode() != 0)
        props.push_back(makeProp(DTV_SCRAMBLING_SEQUENCE_INDEX, t.goldCode()));
    props.push_back(makeProp(DTV_TUNE));

    dtv_properties cmd{static_cast<uint32_t>(props.size()), props.data()};
    if (xioctl(m_fe, FE_SET_PROPERTY, &cmd) < 0) {
        if (error)
            *error = "FE_SET_PROPERTY (tune) failed: " + errnoString();
        return false;
    }
    log(util::format("Tuning %s, stream_id 0x%08x", t.describe().c_str(), t.dvbStreamId()));
    return true;
}

bool LinuxDvbDevice::startStream(std::string *error)
{
    const std::string path = demuxPath(m_sel.adapter, m_sel.demux);
    m_dmx = ::open(path.c_str(), O_RDWR | O_NONBLOCK);
    if (m_dmx < 0) {
        if (error)
            *error = "Cannot open " + path + ": " + errnoString();
        return false;
    }

    // Large kernel buffer: high symbol rate S2X carriers deliver >100 Mbit/s
    for (unsigned long size : {64ul << 20, 32ul << 20, 16ul << 20, 8ul << 20, 2ul << 20}) {
        if (::ioctl(m_dmx, DMX_SET_BUFFER_SIZE, size) == 0) {
            log(util::format("Demux buffer %lu MiB", size >> 20));
            break;
        }
    }

    dmx_pes_filter_params filter;
    std::memset(&filter, 0, sizeof(filter));
    filter.pid = 0x2000; // whole transport stream
    filter.input = DMX_IN_FRONTEND;
    filter.output = DMX_OUT_TSDEMUX_TAP;
    filter.pes_type = DMX_PES_OTHER;
    filter.flags = DMX_IMMEDIATE_START;
    if (xioctl(m_dmx, DMX_SET_PES_FILTER, &filter) < 0) {
        if (error)
            *error = "DMX_SET_PES_FILTER failed: " + errnoString();
        ::close(m_dmx);
        m_dmx = -1;
        return false;
    }
    return true;
}

long LinuxDvbDevice::read(uint8_t *buf, size_t maxLen, int timeoutMs)
{
    if (m_dmx < 0)
        return -1;
    pollfd pfd{m_dmx, POLLIN, 0};
    const int pr = ::poll(&pfd, 1, timeoutMs);
    if (pr < 0)
        return errno == EINTR ? 0 : -1;
    if (pr == 0)
        return 0;

    const ssize_t n = ::read(m_dmx, buf, maxLen);
    if (n >= 0)
        return static_cast<long>(n);
    switch (errno) {
    case EAGAIN:
    case EINTR:
        return 0;
    case EOVERFLOW:
        ++m_overflows;
        return 0;
    case ETIMEDOUT:
        return 0;
    default:
        return -1;
    }
}

void LinuxDvbDevice::readSignalStatistics(FrontendStatus &st)
{
    dtv_property props[6] = {
        makeProp(DTV_STAT_SIGNAL_STRENGTH),
        makeProp(DTV_STAT_CNR),
        makeProp(DTV_STAT_PRE_ERROR_BIT_COUNT),
        makeProp(DTV_STAT_PRE_TOTAL_BIT_COUNT),
        makeProp(DTV_STAT_ERROR_BLOCK_COUNT),
        makeProp(DTV_STAT_POST_ERROR_BIT_COUNT),
    };
    dtv_properties cmd{6, props};
    bool v5 = xioctl(m_fe, FE_GET_PROPERTY, &cmd) == 0;

    if (v5 && props[0].u.st.len > 0) {
        const auto &s = props[0].u.st.stat[0];
        if (s.scale == FE_SCALE_DECIBEL) {
            st.hasSignalDbm = true;
            st.signalDbm = static_cast<double>(s.svalue) / 1000.0;
        } else if (s.scale == FE_SCALE_RELATIVE) {
            st.hasSignalPercent = true;
            st.signalPercent = static_cast<double>(s.uvalue) * 100.0 / 65535.0;
        }
    }
    if (v5 && props[1].u.st.len > 0) {
        const auto &s = props[1].u.st.stat[0];
        if (s.scale == FE_SCALE_DECIBEL) {
            st.hasCnrDb = true;
            st.cnrDb = static_cast<double>(s.svalue) / 1000.0;
        } else if (s.scale == FE_SCALE_RELATIVE) {
            st.hasCnrPercent = true;
            st.cnrPercent = static_cast<double>(s.uvalue) * 100.0 / 65535.0;
        }
    }
    if (v5 && props[2].u.st.len > 0 && props[3].u.st.len > 0
        && props[2].u.st.stat[0].scale == FE_SCALE_COUNTER && props[3].u.st.stat[0].scale == FE_SCALE_COUNTER) {
        const uint64_t err = props[2].u.st.stat[0].uvalue;
        const uint64_t total = props[3].u.st.stat[0].uvalue;
        if (m_haveBerCounters && total > m_lastTotalBits && err >= m_lastErrBits)
            m_lastBer = static_cast<double>(err - m_lastErrBits) / static_cast<double>(total - m_lastTotalBits);
        else if (!m_haveBerCounters && total > 0)
            m_lastBer = static_cast<double>(err) / static_cast<double>(total);
        m_lastErrBits = err;
        m_lastTotalBits = total;
        m_haveBerCounters = true;
        st.hasBer = true;
        st.ber = m_lastBer;
    }
    if (v5 && props[4].u.st.len > 0 && props[4].u.st.stat[0].scale == FE_SCALE_COUNTER) {
        st.hasErrorBlocks = true;
        st.errorBlocks = props[4].u.st.stat[0].uvalue;
    }

    // DVBv3 fallbacks
    if (!st.hasSignalDbm && !st.hasSignalPercent) {
        uint16_t v = 0;
        if (xioctl(m_fe, FE_READ_SIGNAL_STRENGTH, &v) == 0) {
            st.hasSignalPercent = true;
            st.signalPercent = v * 100.0 / 65535.0;
        }
    }
    if (!st.hasCnrDb && !st.hasCnrPercent) {
        uint16_t v = 0;
        if (xioctl(m_fe, FE_READ_SNR, &v) == 0) {
            st.hasCnrPercent = true;
            st.cnrPercent = v * 100.0 / 65535.0;
        }
    }
    if (!st.hasErrorBlocks) {
        uint32_t v = 0;
        if (xioctl(m_fe, FE_READ_UNCORRECTED_BLOCKS, &v) == 0) {
            st.hasErrorBlocks = true;
            st.errorBlocks = v;
        }
    }

    if (m_tempSupported) {
        int16_t t = 0;
        if (xioctl(m_fe, FE_READ_TEMP, &t) == 0) {
            st.hasTemperature = true;
            st.temperatureC = t;
        } else {
            m_tempSupported = false;
        }
    }
}

void LinuxDvbDevice::readTuningParameters(FrontendStatus &st)
{
    dtv_property props[8] = {
        makeProp(DTV_DELIVERY_SYSTEM), makeProp(DTV_FREQUENCY), makeProp(DTV_SYMBOL_RATE),
        makeProp(DTV_MODULATION),      makeProp(DTV_INNER_FEC), makeProp(DTV_ROLLOFF),
        makeProp(DTV_PILOT),           makeProp(DTV_STREAM_ID),
    };
    dtv_properties cmd{8, props};
    if (xioctl(m_fe, FE_GET_PROPERTY, &cmd) < 0)
        return;
    st.deliverySystem = static_cast<int>(props[0].u.data);
    st.frequencyKHz = props[1].u.data;
    st.symbolRate = props[2].u.data;
    st.modulation = static_cast<int>(props[3].u.data);
    st.fec = static_cast<int>(props[4].u.data);
    st.rolloff = static_cast<int>(props[5].u.data);
    st.pilot = static_cast<int>(props[6].u.data);
    st.streamId = props[7].u.data == ~0u ? -1 : static_cast<int64_t>(props[7].u.data);
}

FrontendStatus LinuxDvbDevice::readStatus()
{
    FrontendStatus st;
    st.deviceName = m_name;
    if (m_fe < 0)
        return st;

    fe_status_t status = static_cast<fe_status_t>(0);
    if (xioctl(m_fe, FE_READ_STATUS, &status) < 0)
        return st;
    st.valid = true;
    st.flags = static_cast<uint32_t>(status);

    readSignalStatistics(st);
    if (st.locked())
        readTuningParameters(st);
    return st;
}

void LinuxDvbDevice::close()
{
    if (m_dmx >= 0) {
        ::ioctl(m_dmx, DMX_STOP);
        ::close(m_dmx);
        m_dmx = -1;
    }
    if (m_fe >= 0) {
        ::close(m_fe);
        m_fe = -1;
    }
    m_haveBerCounters = false;
}

} // namespace tbs
