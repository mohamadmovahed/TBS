#include "tbsdvb/Monitor.h"

#include "Util.h"

#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <deque>
#include <mutex>
#include <thread>

namespace tbs {

namespace {

constexpr size_t kAlarmCount = static_cast<size_t>(AlarmType::Count);
constexpr size_t kMaxEvents = 2000;

int64_t wallMs()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch())
        .count();
}

std::string isoTime(int64_t ms)
{
    const std::time_t t = static_cast<std::time_t>(ms / 1000);
    std::tm tm{};
#ifdef _WIN32
    gmtime_s(&tm, &t);
#else
    gmtime_r(&t, &tm);
#endif
    char buf[48];
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02d.%03dZ", tm.tm_year + 1900, tm.tm_mon + 1,
                  tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec, static_cast<int>(ms % 1000));
    return buf;
}

std::string shellQuote(const std::string &s)
{
#ifdef _WIN32
    std::string out = "\"";
    for (char c : s)
        out += c == '"' ? std::string("\\\"") : std::string(1, c);
    return out + "\"";
#else
    std::string out = "'";
    for (char c : s)
        out += c == '\'' ? std::string("'\\''") : std::string(1, c);
    return out + "'";
#endif
}

void replaceAll(std::string &s, const std::string &from, const std::string &to)
{
    for (size_t pos = s.find(from); pos != std::string::npos; pos = s.find(from, pos + to.size()))
        s.replace(pos, from.size(), to);
}

std::string number(double v, int decimals)
{
    if (std::isnan(v))
        return {};
    return util::format("%.*f", decimals, v);
}

} // namespace

const char *alarmTypeName(AlarmType t)
{
    switch (t) {
    case AlarmType::NoLock: return "no-lock";
    case AlarmType::LowCnr: return "low-cnr";
    case AlarmType::LowLevel: return "low-level";
    case AlarmType::CcErrors: return "cc-errors";
    case AlarmType::BbCrcErrors: return "bbframe-crc";
    case AlarmType::LowBitrate: return "low-bitrate";
    case AlarmType::NoData: return "no-data";
    case AlarmType::Drops: return "drops";
    case AlarmType::OutputError: return "output-error";
    case AlarmType::Count: break;
    }
    return "?";
}

const char *alarmTypeTitle(AlarmType t)
{
    switch (t) {
    case AlarmType::NoLock: return "Lock lost";
    case AlarmType::LowCnr: return "Low C/N";
    case AlarmType::LowLevel: return "Low signal level";
    case AlarmType::CcErrors: return "CC errors";
    case AlarmType::BbCrcErrors: return "BBFrame CRC errors";
    case AlarmType::LowBitrate: return "Low bitrate";
    case AlarmType::NoData: return "No data";
    case AlarmType::Drops: return "Data dropped";
    case AlarmType::OutputError: return "Output error";
    case AlarmType::Count: break;
    }
    return "?";
}

struct Monitor::Impl
{
    mutable std::mutex mutex;
    MonitorConfig cfg;
    std::function<void(const AlarmEvent &)> callback;
    std::deque<MonitorSample> history;
    std::deque<AlarmEvent> events;
    std::array<Alarm, kAlarmCount> alarms;
    std::array<int64_t, kAlarmCount> conditionSince{}; // 0 = condition not present

    struct Counters
    {
        bool valid = false;
        uint64_t cc = 0, tei = 0, sync = 0, crc = 0, overflows = 0, dropped = 0, netErrors = 0;
    } prev;

    Impl()
    {
        for (size_t i = 0; i < kAlarmCount; ++i)
            alarms[i].type = static_cast<AlarmType>(i);
    }

    bool enabled(AlarmType t) const
    {
        switch (t) {
        case AlarmType::NoLock: return cfg.noLock;
        case AlarmType::LowCnr: return cfg.lowCnr;
        case AlarmType::LowLevel: return cfg.lowLevel;
        case AlarmType::CcErrors: return cfg.ccErrors;
        case AlarmType::BbCrcErrors: return cfg.bbCrcErrors;
        case AlarmType::LowBitrate: return cfg.lowBitrate;
        case AlarmType::NoData: return cfg.noData;
        case AlarmType::Drops: return cfg.drops;
        case AlarmType::OutputError: return cfg.outputErrors;
        case AlarmType::Count: break;
        }
        return false;
    }

    // Events produced under the lock, delivered after it is released
    std::vector<AlarmEvent> pending;

    void change(AlarmType t, bool raise, int64_t now, const std::string &msg)
    {
        Alarm &a = alarms[static_cast<size_t>(t)];
        if (a.active == raise)
            return;
        a.active = raise;
        a.sinceMs = now;
        if (raise) {
            ++a.raisedCount;
            a.message = msg;
        }
        AlarmEvent e;
        e.timeMs = now;
        e.type = t;
        e.raised = raise;
        e.message = msg;
        events.push_back(e);
        while (events.size() > kMaxEvents)
            events.pop_front();
        pending.push_back(e);
    }

    // Condition that must persist for holdSec before the alarm is raised
    void held(AlarmType t, bool cond, int holdSec, int64_t now, const std::string &msg, const std::string &clearMsg)
    {
        int64_t &since = conditionSince[static_cast<size_t>(t)];
        if (!enabled(t)) {
            since = 0;
            change(t, false, now, "alarm disabled");
            return;
        }
        if (cond) {
            if (since == 0)
                since = now;
            if (now - since >= static_cast<int64_t>(holdSec) * 1000)
                change(t, true, now, msg);
        } else {
            since = 0;
            change(t, false, now, clearMsg);
        }
    }

    // Counter summed over the last window: raised above max, cleared at or below it
    void windowed(AlarmType t, uint64_t sum, uint64_t max, int64_t now, const std::string &msg)
    {
        if (!enabled(t)) {
            change(t, false, now, "alarm disabled");
            return;
        }
        if (sum > max)
            change(t, true, now, msg);
        else
            change(t, false, now, "no errors in the last " + std::to_string(cfg.windowSec) + " s");
    }

    void evaluate(const MonitorSample &s, const StreamStats &st)
    {
        const int64_t now = s.timeMs;
        if (!s.running)
            return;

        held(AlarmType::NoLock, !s.locked, cfg.noLockHoldSec, now,
             util::format("no lock for %d s", cfg.noLockHoldSec), "locked");
        held(AlarmType::LowCnr, s.locked && !std::isnan(s.cnrDb) && s.cnrDb < cfg.cnrMinDb, cfg.cnrHoldSec, now,
             util::format("C/N %.1f dB, below %.1f dB", s.cnrDb, cfg.cnrMinDb), "C/N back above the limit");
        held(AlarmType::LowLevel, !std::isnan(s.levelDbm) && s.levelDbm < cfg.levelMinDbm, cfg.levelHoldSec, now,
             util::format("level %.1f dBm, below %.1f dBm", s.levelDbm, cfg.levelMinDbm), "level back above the limit");
        held(AlarmType::LowBitrate, s.inputMbps < cfg.bitrateMinMbps, cfg.bitrateHoldSec, now,
             util::format("input %.2f Mbit/s, below %.2f Mbit/s", s.inputMbps, cfg.bitrateMinMbps),
             "bitrate back above the limit");
        held(AlarmType::NoData, s.inputMbps <= 0.0, cfg.noDataHoldSec, now,
             util::format("no data for %d s", cfg.noDataHoldSec), "data is arriving");

        // Windowed counters
        const int64_t from = now - static_cast<int64_t>(cfg.windowSec) * 1000;
        uint64_t cc = 0, crc = 0, overflows = 0, dropped = 0, out = 0;
        for (auto it = history.rbegin(); it != history.rend() && it->timeMs > from; ++it) {
            cc += it->ccErrors;
            crc += it->bbCrcErrors;
            overflows += it->overflows;
            dropped += it->droppedBytes;
            out += it->outputErrors;
        }
        const std::string window = util::format(" in the last %d s", cfg.windowSec);
        windowed(AlarmType::CcErrors, cc, cfg.ccMaxPerWindow, now,
                 util::format("%llu CC errors", static_cast<unsigned long long>(cc)) + window);
        windowed(AlarmType::BbCrcErrors, crc, cfg.bbCrcMaxPerWindow, now,
                 util::format("%llu BBFrame CRC errors", static_cast<unsigned long long>(crc)) + window);
        windowed(AlarmType::Drops, overflows + dropped, 0, now,
                 util::format("%llu driver overflows, %llu bytes dropped by processing",
                              static_cast<unsigned long long>(overflows), static_cast<unsigned long long>(dropped))
                     + window);
        std::string outMsg = st.fileStopped ? "recording stopped" : std::string();
        if (out)
            outMsg += (outMsg.empty() ? "" : ", ")
                      + util::format("%llu network send errors", static_cast<unsigned long long>(out)) + window;
        windowed(AlarmType::OutputError, (st.fileStopped ? 1 : 0) + out, 0, now, outMsg);
    }
};

Monitor::Monitor(const MonitorConfig &cfg)
    : d(new Impl)
{
    d->cfg = cfg;
    for (size_t i = 0; i < kAlarmCount; ++i)
        d->alarms[i].enabled = d->enabled(static_cast<AlarmType>(i));
}

Monitor::~Monitor() = default;

void Monitor::setConfig(const MonitorConfig &cfg)
{
    std::lock_guard<std::mutex> lock(d->mutex);
    d->cfg = cfg;
    for (size_t i = 0; i < kAlarmCount; ++i)
        d->alarms[i].enabled = d->enabled(static_cast<AlarmType>(i));
}

MonitorConfig Monitor::config() const
{
    std::lock_guard<std::mutex> lock(d->mutex);
    return d->cfg;
}

void Monitor::setAlarmCallback(std::function<void(const AlarmEvent &)> cb)
{
    std::lock_guard<std::mutex> lock(d->mutex);
    d->callback = std::move(cb);
}

void Monitor::reset()
{
    std::vector<AlarmEvent> out;
    std::function<void(const AlarmEvent &)> cb;
    {
        std::lock_guard<std::mutex> lock(d->mutex);
        d->prev = Impl::Counters();
        const int64_t now = wallMs();
        for (size_t i = 0; i < kAlarmCount; ++i) {
            d->conditionSince[i] = 0;
            d->change(static_cast<AlarmType>(i), false, now, "capture restarted");
        }
        out.swap(d->pending);
        cb = d->callback;
    }
    // Clearing on restart goes to the callback but not to the alarm command
    for (const AlarmEvent &e : out) {
        if (cb)
            cb(e);
    }
}

void Monitor::update(ReceiverState state, const FrontendStatus &fe, const StreamStats &st, int64_t nowMs)
{
    const double nan = std::nan("");
    MonitorSample s;
    s.timeMs = nowMs ? nowMs : wallMs();
    s.running = state == ReceiverState::Running;
    s.locked = fe.valid && fe.locked();
    s.levelDbm = fe.hasSignalDbm ? fe.signalDbm : nan;
    s.levelPercent = fe.hasSignalPercent ? fe.signalPercent : nan;
    s.cnrDb = fe.hasCnrDb ? fe.cnrDb : nan;
    s.ber = fe.hasBer ? fe.ber : nan;
    s.inputMbps = st.inputMbps;
    s.payloadMbps = st.bbFrames ? st.bbPayloadMbps : st.inputMbps;

    uint64_t netErrors = 0;
    for (const NetOutputStatus &n : st.network)
        netErrors += n.sendErrors;

    std::vector<AlarmEvent> out;
    std::function<void(const AlarmEvent &)> cb;
    std::string command;
    {
        std::lock_guard<std::mutex> lock(d->mutex);
        Impl::Counters &p = d->prev;
        // Counters restart with a new capture: a smaller value is a fresh count
        auto delta = [&](uint64_t now, uint64_t before) { return !p.valid ? 0 : (now >= before ? now - before : now); };
        s.ccErrors = static_cast<uint32_t>(delta(st.ccErrors, p.cc));
        s.teiErrors = static_cast<uint32_t>(delta(st.teiErrors, p.tei));
        s.syncLosses = static_cast<uint32_t>(delta(st.syncLosses, p.sync));
        s.bbCrcErrors = static_cast<uint32_t>(delta(st.bbCrcErrors, p.crc));
        s.overflows = static_cast<uint32_t>(delta(st.driverOverflows, p.overflows));
        s.droppedBytes = delta(st.pipelineDroppedBytes, p.dropped);
        s.outputErrors = static_cast<uint32_t>(delta(netErrors, p.netErrors));
        p.valid = true;
        p.cc = st.ccErrors;
        p.tei = st.teiErrors;
        p.sync = st.syncLosses;
        p.crc = st.bbCrcErrors;
        p.overflows = st.driverOverflows;
        p.dropped = st.pipelineDroppedBytes;
        p.netErrors = netErrors;

        d->history.push_back(s);
        const int64_t keepFrom = s.timeMs - static_cast<int64_t>(d->cfg.historySeconds) * 1000;
        while (!d->history.empty() && d->history.front().timeMs < keepFrom)
            d->history.pop_front();
        d->evaluate(s, st);
        out.swap(d->pending);
        cb = d->callback;
        command = d->cfg.command;
    }
    for (const AlarmEvent &e : out) {
        if (cb)
            cb(e);
        if (!command.empty()) {
            std::string c = command;
            replaceAll(c, "{alarm}", shellQuote(alarmTypeName(e.type)));
            replaceAll(c, "{state}", shellQuote(e.raised ? "raised" : "cleared"));
            replaceAll(c, "{message}", shellQuote(e.message));
            replaceAll(c, "{time}", shellQuote(isoTime(e.timeMs)));
            std::thread([c]() {
                const int rc = std::system(c.c_str());
                (void)rc;
            }).detach();
        }
    }
}

std::vector<MonitorSample> Monitor::history(int64_t sinceMs) const
{
    std::lock_guard<std::mutex> lock(d->mutex);
    std::vector<MonitorSample> out;
    for (const MonitorSample &s : d->history)
        if (s.timeMs >= sinceMs)
            out.push_back(s);
    return out;
}

std::vector<Alarm> Monitor::alarms() const
{
    std::lock_guard<std::mutex> lock(d->mutex);
    return std::vector<Alarm>(d->alarms.begin(), d->alarms.end());
}

std::vector<AlarmEvent> Monitor::events(size_t max) const
{
    std::lock_guard<std::mutex> lock(d->mutex);
    const size_t n = std::min(max, d->events.size());
    return std::vector<AlarmEvent>(d->events.end() - static_cast<std::ptrdiff_t>(n), d->events.end());
}

size_t Monitor::activeAlarms() const
{
    std::lock_guard<std::mutex> lock(d->mutex);
    size_t n = 0;
    for (const Alarm &a : d->alarms)
        n += a.active ? 1 : 0;
    return n;
}

std::string Monitor::csvHeader()
{
    return "time,unix_ms,running,locked,level_dbm,level_percent,cnr_db,ber,input_mbps,payload_mbps,cc_errors,"
           "tei_errors,sync_losses,bbframe_crc_errors,driver_overflows,dropped_bytes,output_errors\n";
}

std::string Monitor::csvLine(const MonitorSample &s)
{
    return isoTime(s.timeMs) + "," + std::to_string(s.timeMs) + "," + (s.running ? "1" : "0") + ","
           + (s.locked ? "1" : "0") + "," + number(s.levelDbm, 1) + "," + number(s.levelPercent, 1) + ","
           + number(s.cnrDb, 2) + "," + (std::isnan(s.ber) ? std::string() : util::format("%.3e", s.ber)) + ","
           + number(s.inputMbps, 3) + "," + number(s.payloadMbps, 3) + "," + std::to_string(s.ccErrors) + ","
           + std::to_string(s.teiErrors) + "," + std::to_string(s.syncLosses) + "," + std::to_string(s.bbCrcErrors)
           + "," + std::to_string(s.overflows) + "," + std::to_string(s.droppedBytes) + ","
           + std::to_string(s.outputErrors) + "\n";
}

bool Monitor::writeCsv(const std::string &path, int64_t sinceMs, std::string *error) const
{
    std::FILE *f = std::fopen(path.c_str(), "wb");
    if (!f) {
        if (error)
            *error = "cannot write " + path;
        return false;
    }
    std::string text = csvHeader();
    for (const MonitorSample &s : history(sinceMs))
        text += csvLine(s);
    const bool ok = std::fwrite(text.data(), 1, text.size(), f) == text.size();
    std::fclose(f);
    if (!ok && error)
        *error = "write error on " + path;
    return ok;
}

} // namespace tbs
