#pragma once

#include "Scan.h"
#include "Status.h"
#include "Types.h"
#include "export.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace tbs {

// A receiver backend (Linux DVB, Windows BDA, file replay).
//
// Thread model: an instance is used from a single thread (the Receiver worker
// thread); it may create thread-affine resources such as COM objects in open().
class TBSDVB_API ITunerDevice
{
public:
    using LogFn = std::function<void(const std::string &)>;

    virtual ~ITunerDevice();

    virtual std::string name() const = 0;

    // Opens the device and tunes it.
    virtual bool open(const DeviceSelection &sel, const TuningParams &tuning, CaptureMode mode, std::string *error) = 0;
    // Retunes an open device.
    virtual bool tune(const TuningParams &tuning, std::string *error) = 0;
    // Starts delivery of the transport stream.
    virtual bool startStream(std::string *error) = 0;
    // Reads up to maxLen bytes: number of bytes, 0 on timeout, -1 on a fatal error.
    virtual long read(uint8_t *buf, size_t maxLen, int timeoutMs) = 0;
    virtual FrontendStatus readStatus() = 0;
    virtual void close() = 0;

    virtual uint64_t overflows() const { return 0; }
    virtual bool atEnd() const { return false; }
    // IQ samples of the locked carrier (needs driver support, see Scan.h)
    virtual bool readConstellation(std::vector<ConstellationSample> *out, int maxSamples, std::string *error)
    {
        (void)out;
        (void)maxSamples;
        if (error)
            *error = "Constellation display is not supported by this device";
        return false;
    }

    void setLogger(LogFn fn) { m_log = std::move(fn); }

protected:
    void log(const std::string &msg) const
    {
        if (m_log)
            m_log(msg);
    }

private:
    LogFn m_log;
};

// One finding of Devices::diagnose()
struct TBSDVB_API DiagnosticItem
{
    enum class Level { Ok, Info, Warning, Error };
    Level level = Level::Info;
    std::string message;
    std::string hint; // what to do about it (may be empty)
};

namespace Devices {

// Receivers present on this machine. The replay pseudo-device is appended when includeReplay is set.
TBSDVB_API std::vector<DeviceInfo> enumerate(bool includeReplay = true);

// Whether this build can drive the given device kind.
TBSDVB_API bool isSupported(DeviceKind kind);

TBSDVB_API std::unique_ptr<ITunerDevice> create(DeviceKind kind, std::string *error = nullptr);

// Checks why a device cannot be opened: card on the PCI bus, driver bound, modules
// loaded/installed for the running kernel, Secure Boot, /dev/dvb nodes, permissions
// (Linux) or the BDA tuner list (Windows).
TBSDVB_API std::vector<DiagnosticItem> diagnose(const DeviceSelection &sel);
// Plain text report: one line per item, hints indented below
TBSDVB_API std::string formatDiagnostics(const std::vector<DiagnosticItem> &items);

// Linux: value of /sys/module/stid135/parameters/bbframe, or -1 if unavailable.
TBSDVB_API int stid135BbFrameParameter();

} // namespace Devices
} // namespace tbs
