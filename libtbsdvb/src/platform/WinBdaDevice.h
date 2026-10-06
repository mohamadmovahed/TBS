#pragma once

#include "tbsdvb/Device.h"

#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <vector>

namespace tbs {

// Windows backend for the TBS BDA driver (DirectShow).
//
// Graph: Network Provider -> TBS tuner -> [receiver/capture] -> Infinite Tee
//          +-> Sample Grabber -> Null Renderer   (transport stream to this class)
//          +-> MPEG-2 Demultiplexer -> TIF       (some network providers need it to tune)
//
// Standard BDA interfaces carry frequency, symbol rate, polarization, LNB
// settings and the DiSEqC port. TBS extensions (property set
// {FAA8F3E5-31D4-4e41-88EF-D9EB716F6EC9} on the tuner input pin) are used when
// present for LNB power, 22 kHz tone, raw DiSEqC, DVB-S2 parameters and the
// stream ID (ISI/PLS).
class WinBdaDevice : public ITunerDevice
{
public:
    WinBdaDevice();
    ~WinBdaDevice() override;

    static std::vector<DeviceInfo> enumerate();

    std::string name() const override { return m_name; }
    bool open(const DeviceSelection &sel, const TuningParams &tuning, CaptureMode mode, std::string *error) override;
    bool tune(const TuningParams &tuning, std::string *error) override;
    bool startStream(std::string *error) override;
    long read(uint8_t *buf, size_t maxLen, int timeoutMs) override;
    FrontendStatus readStatus() override;
    void close() override;
    uint64_t overflows() const override { return m_overflows; }

    // Called on the DirectShow streaming thread
    void onBuffer(const uint8_t *data, size_t len);

private:
    struct Impl;
    std::unique_ptr<Impl> d;
    std::string m_name = "BDA tuner";

    std::mutex m_mutex;
    std::condition_variable m_cv;
    std::deque<std::vector<uint8_t>> m_queue;
    size_t m_queuedBytes = 0;
    std::vector<uint8_t> m_current;
    size_t m_currentPos = 0;
    uint64_t m_overflows = 0;
};

} // namespace tbs
