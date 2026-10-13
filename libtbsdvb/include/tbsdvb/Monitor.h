#pragma once

#include "Status.h"
#include "export.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace tbs {

// One second (one stats interval) of receiver history.
struct TBSDVB_API MonitorSample
{
    int64_t timeMs = 0;        // wall clock, ms since 1970
    bool running = false;
    bool locked = false;
    double levelDbm = 0.0;     // NaN when not reported (also the other optional values)
    double levelPercent = 0.0;
    double cnrDb = 0.0;
    double ber = 0.0;
    double inputMbps = 0.0;
    double payloadMbps = 0.0;  // BBFrame data fields in BBFrame mode, else the input rate
    // Counters for this interval
    uint32_t ccErrors = 0;
    uint32_t teiErrors = 0;
    uint32_t syncLosses = 0;
    uint32_t bbCrcErrors = 0;
    uint32_t overflows = 0;    // driver overflows
    uint64_t droppedBytes = 0; // processing could not keep up
    uint32_t outputErrors = 0; // network output send errors
};

enum class AlarmType {
    NoLock = 0,
    LowCnr,
    LowLevel,
    CcErrors,
    BbCrcErrors,
    LowBitrate,
    NoData,
    Drops,
    OutputError,
    Count
};

TBSDVB_API const char *alarmTypeName(AlarmType t);  // "no-lock", "low-cnr", ...
TBSDVB_API const char *alarmTypeTitle(AlarmType t); // "Lock lost", "Low C/N", ...

// Thresholds. "hold" = how long a condition must last before the alarm is raised;
// error counts are summed over the last windowSec seconds.
struct TBSDVB_API MonitorConfig
{
    int historySeconds = 86400;

    bool noLock = true;
    int noLockHoldSec = 3;
    bool lowCnr = false;
    double cnrMinDb = 6.0;
    int cnrHoldSec = 10;
    bool lowLevel = false;
    double levelMinDbm = -70.0;
    int levelHoldSec = 10;
    bool ccErrors = true;
    uint32_t ccMaxPerWindow = 0;   // alarm when more CC errors than this in the window
    bool bbCrcErrors = true;
    uint32_t bbCrcMaxPerWindow = 0;
    int windowSec = 60;
    bool lowBitrate = false;
    double bitrateMinMbps = 1.0;
    int bitrateHoldSec = 10;
    bool noData = true;
    int noDataHoldSec = 5;
    bool drops = true;
    bool outputErrors = true;

    // Run on every alarm change (raised or cleared), without waiting. Placeholders, shell quoted:
    // {alarm} (e.g. no-lock), {state} (raised|cleared), {message}, {time} (ISO 8601 UTC)
    std::string command;
};

struct TBSDVB_API Alarm
{
    AlarmType type = AlarmType::NoLock;
    bool enabled = false;
    bool active = false;
    int64_t sinceMs = 0;    // when the current state began (0 = never raised)
    uint64_t raisedCount = 0;
    std::string message;    // why it is (or was last) raised
};

struct TBSDVB_API AlarmEvent
{
    int64_t timeMs = 0;
    AlarmType type = AlarmType::NoLock;
    bool raised = true;     // false = cleared
    std::string message;
};

// Receiver health monitoring: keeps a per-interval history, evaluates alarms and runs an
// optional command. Feed it from Receiver::Callbacks::onStats. Thread safe.
class TBSDVB_API Monitor
{
public:
    explicit Monitor(const MonitorConfig &cfg = MonitorConfig());
    ~Monitor();
    Monitor(const Monitor &) = delete;
    Monitor &operator=(const Monitor &) = delete;

    void setConfig(const MonitorConfig &cfg);
    MonitorConfig config() const;
    // Called (on the feeding thread) for each raised or cleared alarm
    void setAlarmCallback(std::function<void(const AlarmEvent &)> cb);

    // One stats interval. nowMs = 0 uses the system clock.
    void update(ReceiverState state, const FrontendStatus &status, const StreamStats &stats, int64_t nowMs = 0);
    // Start of a new capture: counters restart, active alarms are cleared
    void reset();

    std::vector<MonitorSample> history(int64_t sinceMs = 0) const;
    std::vector<Alarm> alarms() const;               // one entry per AlarmType
    std::vector<AlarmEvent> events(size_t max = 500) const; // newest last
    size_t activeAlarms() const;

    // CSV of the history (one line per sample, header first)
    static std::string csvHeader();
    static std::string csvLine(const MonitorSample &s);
    bool writeCsv(const std::string &path, int64_t sinceMs = 0, std::string *error = nullptr) const;

private:
    struct Impl;
    std::unique_ptr<Impl> d;
};

} // namespace tbs
