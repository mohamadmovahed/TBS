#pragma once

#include "Device.h"
#include "Sink.h"
#include "Status.h"
#include "Types.h"
#include "export.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace tbs {

// High level receiver: opens a device, tunes it, captures TS or BBFrames,
// feeds file / ZeroMQ / custom sinks and application callbacks, and keeps
// the latest frontend status and stream statistics.
//
//   tbs::Receiver rx;
//   tbs::Receiver::Callbacks cb;
//   cb.onBbFrame = [](const uint8_t *f, size_t n, const tbs::BbHeader &h) { ... };
//   rx.setCallbacks(cb);
//   tbs::ReceiverConfig cfg;            // fill device / tuning / outputs
//   rx.start(cfg);
//   ... rx.status(), rx.stats(), rx.retune(newTuning) ...
//   rx.stop();
//
// Two worker threads are used: the capture thread owns the device (reads,
// retunes, onStatus) and hands the data through a bounded buffer to the
// processing thread (parsing, sinks, onTsPackets/onBbFrame/onPreviewTs, onStats).
// A slow sink therefore cannot stall the device; if the buffer overflows the
// data is dropped and counted in StreamStats::pipelineDroppedBytes.
// Callbacks must not block for long.
class TBSDVB_API Receiver
{
public:
    struct Callbacks
    {
        std::function<void(const std::string &)> onLog;
        std::function<void(ReceiverState state, const std::string &error)> onStateChanged;
        std::function<void(const FrontendStatus &)> onStatus; // every statusIntervalMs
        std::function<void(const StreamStats &)> onStats;     // every statusIntervalMs
        // Aligned 188-byte packets as received (TS mode: the TS; BBFrame mode: the pseudo-TS)
        std::function<void(const uint8_t *packets, size_t count)> onTsPackets;
        // Reassembled BBFrames (BBFrame mode): BBHeader + data field
        std::function<void(const uint8_t *frame, size_t len, const BbHeader &hdr)> onBbFrame;
        // Decodable transport stream for preview/analysis; in BBFrame mode this is the
        // TS recovered from BBFrames that carry one. Only called while preview is enabled.
        std::function<void(const uint8_t *data, size_t len)> onPreviewTs;
    };

    Receiver();
    ~Receiver();
    Receiver(const Receiver &) = delete;
    Receiver &operator=(const Receiver &) = delete;

    // Set before start(); not thread safe against a running capture.
    void setCallbacks(Callbacks cb);
    // Extra sinks used on the next start() (in addition to the file/ZeroMQ outputs from the config)
    void addSink(std::shared_ptr<IOutputSink> sink);
    void clearSinks();
    void setStatusInterval(int ms);
    // Size of the buffer between the capture and the processing thread (default 128 MiB)
    void setPipelineBufferSize(size_t bytes);

    // Starts the capture thread. Returns false if already running. Opening errors are
    // reported through onStateChanged(Stopped, error) / lastError().
    bool start(const ReceiverConfig &cfg);
    // Stops and joins the capture thread.
    void stop();
    bool isRunning() const;
    ReceiverState state() const;
    std::string lastError() const;

    // Retunes a running receiver without restarting the stream (thread safe).
    void retune(const TuningParams &tuning);
    // Enables/disables onPreviewTs (thread safe).
    void setPreviewEnabled(bool on);

    // Latest snapshots (thread safe)
    FrontendStatus status() const;
    StreamStats stats() const;
    ReceiverConfig config() const;

    // Convenience: devices present on this machine
    static std::vector<DeviceInfo> enumerateDevices(bool includeReplay = true) { return Devices::enumerate(includeReplay); }

private:
    struct Impl;
    std::unique_ptr<Impl> d;
};

} // namespace tbs
