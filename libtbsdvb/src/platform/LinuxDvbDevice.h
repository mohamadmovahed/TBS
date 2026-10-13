#pragma once

#include "tbsdvb/Device.h"
#include "tbsdvb/Scan.h"

#include <vector>

namespace tbs {

// Linux DVB API backend (TBS open source driver "linux_media", STiD135 on the TBS 6903x):
//   /dev/dvb/adapterN/frontendM  tuning, SEC and status
//   /dev/dvb/adapterN/demuxM     0x2000 (all PIDs) filter in TSDEMUX_TAP mode for data
class LinuxDvbDevice : public ITunerDevice
{
public:
    ~LinuxDvbDevice() override;

    static std::vector<DeviceInfo> enumerate();
    // rootPrefix lets tests run the checks against a fake /sys, /proc, /dev and /lib tree
    static std::vector<DiagnosticItem> diagnose(const DeviceSelection &sel, const std::string &rootPrefix = std::string());

    std::string name() const override { return m_name; }
    bool open(const DeviceSelection &sel, const TuningParams &tuning, CaptureMode mode, std::string *error) override;
    bool tune(const TuningParams &tuning, std::string *error) override;
    bool startStream(std::string *error) override;
    long read(uint8_t *buf, size_t maxLen, int timeoutMs) override;
    FrontendStatus readStatus() override;
    void close() override;
    uint64_t overflows() const override { return m_overflows; }
    bool readConstellation(std::vector<ConstellationSample> *out, int maxSamples, std::string *error) override;

    // Spectrum / blind scan (neumoDVB driver API, LinuxDvbScan.cpp). The frontend is opened
    // without tuning; a segment is one LNB setting (see Scan::bandSegments).
    bool openFrontend(const DeviceSelection &sel, std::string *error);
    ScanCapabilities scanCapabilities();
    bool spectrumSegment(const Scan::BandSegment &seg, const SpectrumRequest &req, SpectrumResult *out, std::string *error,
                         const std::function<bool()> &cancelled);
    bool blindScanSegment(const Scan::BandSegment &seg, const BlindScanRequest &req, std::vector<BlindScanCarrier> *out,
                          SpectrumResult *spectrumOut, std::string *error, const std::function<bool()> &cancelled,
                          const std::function<void(const BlindScanCarrier &, double ifKHz)> &onCarrier);

private:
    bool setupSec(const TuningParams &t, std::string *error);
    void readSignalStatistics(FrontendStatus &st);
    void readTuningParameters(FrontendStatus &st);
    // Waits for a frontend event; returns its status flags, 0 on timeout, -1 on error/cancel
    int waitEvent(int timeoutMs, const std::function<bool()> &cancelled);
    void drainEvents();
    bool readSpectrum(const Scan::BandSegment &seg, SpectrumResult *out, std::string *error);
    bool readCarrier(const Scan::BandSegment &seg, BlindScanCarrier *c, double *ifKHz);

    DeviceSelection m_sel;
    std::string m_name = "DVB frontend";
    int m_fe = -1;
    int m_dmx = -1;
    uint64_t m_overflows = 0;
    bool m_tempSupported = true;

    uint64_t m_lastErrBits = 0;
    uint64_t m_lastTotalBits = 0;
    bool m_haveBerCounters = false;
    double m_lastBer = 0.0;
    int m_constellationState = 0; // 0 unknown, 1 works, -1 unsupported
};

} // namespace tbs
