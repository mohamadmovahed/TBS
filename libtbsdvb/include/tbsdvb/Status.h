#pragma once

#include "BbHeader.h"
#include "export.h"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace tbs {

enum class ReceiverState { Stopped = 0, Starting, Running, Stopping };

// Snapshot of the demodulator state.
struct TBSDVB_API FrontendStatus
{
    enum Flag : uint32_t {
        HasSignal = 0x01,
        HasCarrier = 0x02,
        HasViterbi = 0x04,
        HasSync = 0x08,
        HasLock = 0x10,
        TimedOut = 0x20,
    };

    bool valid = false;
    uint32_t flags = 0;

    bool hasSignalDbm = false;
    double signalDbm = 0.0;
    bool hasSignalPercent = false;
    double signalPercent = 0.0;

    bool hasCnrDb = false;
    double cnrDb = 0.0;
    bool hasCnrPercent = false;
    double cnrPercent = 0.0;

    bool hasBer = false;
    double ber = 0.0;

    bool hasErrorBlocks = false;
    uint64_t errorBlocks = 0;

    bool hasTemperature = false;
    double temperatureC = 0.0;

    // Parameters read back from the demodulator (linux-dvb enum values, -1 = unknown)
    int deliverySystem = -1;
    int modulation = -1;
    int fec = -1;
    int rolloff = -1;
    int pilot = -1;
    uint32_t frequencyKHz = 0; // IF
    uint32_t symbolRate = 0;
    int64_t streamId = -1;

    std::string deviceName;

    bool locked() const { return (flags & HasLock) != 0; }
};

struct TBSDVB_API StreamStats
{
    double elapsedSec = 0.0;
    uint64_t bytesIn = 0;
    double inputMbps = 0.0;
    uint64_t driverOverflows = 0;

    // Hand-over buffer between the capture and the processing thread
    uint64_t pipelineCapacityBytes = 0;
    uint64_t pipelineQueuedBytes = 0;
    uint64_t pipelinePeakBytes = 0;
    uint64_t pipelineDroppedBytes = 0; // data lost because processing could not keep up

    uint64_t tsPackets = 0;
    uint64_t syncLosses = 0;
    uint64_t ccErrors = 0;
    uint64_t teiErrors = 0;
    std::vector<std::pair<uint16_t, uint64_t>> pidCounts; // sorted by PID

    uint64_t bbFrames = 0;
    double bbFramesPerSec = 0.0;
    double bbPayloadMbps = 0.0;
    uint64_t bbCrcErrors = 0;
    uint64_t bbSequenceErrors = 0;
    uint64_t bbLengthErrors = 0;
    bool hasBbHeader = false;
    BbHeader lastBbHeader;

    uint64_t fileBytes = 0;     // all segments of this recording
    std::string fileName;       // current segment
    uint32_t fileSegments = 0;  // files written so far
    bool fileStopped = false;   // recording ended early (disk full, write error)
    int64_t fileFreeBytes = -1; // free space on the recording volume (-1 = unknown)
    uint64_t zmqMessages = 0;
    uint64_t zmqDropped = 0;
};

} // namespace tbs
