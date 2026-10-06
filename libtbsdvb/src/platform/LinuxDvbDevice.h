#pragma once

#include "tbsdvb/Device.h"

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

    std::string name() const override { return m_name; }
    bool open(const DeviceSelection &sel, const TuningParams &tuning, CaptureMode mode, std::string *error) override;
    bool tune(const TuningParams &tuning, std::string *error) override;
    bool startStream(std::string *error) override;
    long read(uint8_t *buf, size_t maxLen, int timeoutMs) override;
    FrontendStatus readStatus() override;
    void close() override;
    uint64_t overflows() const override { return m_overflows; }

private:
    bool setupSec(const TuningParams &t, std::string *error);
    void readSignalStatistics(FrontendStatus &st);
    void readTuningParameters(FrontendStatus &st);

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
};

} // namespace tbs
