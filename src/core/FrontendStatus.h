#pragma once

#include <QMetaType>
#include <QString>
#include <cstdint>

// Snapshot of the demodulator state, filled by an ITunerDevice.
struct FrontendStatus
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

    // Parameters read back from the demodulator (raw linux-dvb enum values, -1 = unknown)
    int deliverySystem = -1;
    int modulation = -1;
    int fec = -1;
    int rolloff = -1;
    int pilot = -1;
    uint32_t frequencyKHz = 0;
    uint32_t symbolRate = 0;
    int64_t streamId = -1;

    QString deviceName;

    bool locked() const { return (flags & HasLock) != 0; }
};

Q_DECLARE_METATYPE(FrontendStatus)
