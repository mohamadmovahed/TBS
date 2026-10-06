#pragma once

#include "BbHeader.h"

#include <QMetaType>
#include <QString>
#include <cstdint>
#include <utility>
#include <vector>

struct StreamStats
{
    double elapsedSec = 0.0;
    uint64_t bytesIn = 0;
    double inputMbps = 0.0;
    uint64_t driverOverflows = 0;

    // Transport stream layer
    uint64_t tsPackets = 0;
    uint64_t syncLosses = 0;
    uint64_t ccErrors = 0;
    uint64_t teiErrors = 0;
    std::vector<std::pair<uint16_t, uint64_t>> pidCounts; // sorted by PID

    // BBFrame layer
    uint64_t bbFrames = 0;
    double bbFramesPerSec = 0.0;
    double bbPayloadMbps = 0.0;
    uint64_t bbCrcErrors = 0;
    uint64_t bbSequenceErrors = 0;
    uint64_t bbLengthErrors = 0;
    bool hasBbHeader = false;
    BbHeader lastBbHeader;

    // Outputs
    uint64_t fileBytes = 0;
    QString fileName;
    uint64_t zmqMessages = 0;
    uint64_t zmqDropped = 0;
};

Q_DECLARE_METATYPE(StreamStats)
