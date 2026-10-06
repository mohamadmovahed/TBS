#pragma once

#include <QString>
#include <cstdint>

class QSettings;

enum class DeviceKind { LinuxDvb = 0, FileReplay = 1 };

// The STiD135 driver handles DVB-S2X as part of SYS_DVBS2.
enum class DeliverySystem { DvbS = 0, DvbS2 = 1 };

enum class ModulationHint { Auto = 0, Qpsk, Psk8, Apsk16, Apsk32 };

enum class LnbType {
    Direct = 0,    // No LNB / L-band input: frequency is already the IF
    Universal = 1, // Two LOs with a 22 kHz band switch (Ku band)
    SingleLo = 2,  // Single LO (C band 5150, Ku 10750, ...)
};

enum class Polarization { Off = 0, VerticalRight13V = 1, HorizontalLeft18V = 2 };

enum class ToneMode { Auto = 0, Off = 1, On = 2 };

enum class PlsMode { Root = 0, Gold = 1, Combo = 2 };

enum class CaptureMode {
    TransportStream = 0, // Card outputs MPEG-TS; forward TS packets
    BBFrame = 1,         // Card outputs BBFrames wrapped in pseudo-TS (stid135 bbframe=1)
};

enum class BbFileFormat {
    Raw = 0,       // BBHeader + data field, frames back to back
    B8Marker = 1,  // 0xB8 + BBFrame (format used by bbframe-tools / bbfedi2eti)
    PseudoTs = 2,  // The 188-byte pseudo-TS packets exactly as received from the card
};

enum class ZmqSocketKind { Pub = 0, Push = 1 };

struct TunerConfig
{
    // Device selection
    DeviceKind deviceKind = DeviceKind::LinuxDvb;
    int adapter = 0;
    int frontend = 0;
    int demux = 0;
    QString replayFile;
    double replayRateMbps = 0.0; // 0 = as fast as possible
    bool replayLoop = true;

    // Tuning
    double frequencyMHz = 11494.0;
    double symbolRateKsps = 27500.0;
    DeliverySystem deliverySystem = DeliverySystem::DvbS2;
    ModulationHint modulation = ModulationHint::Auto;

    // LNB / SEC
    LnbType lnbType = LnbType::Universal;
    double lnbLo1MHz = 9750.0;
    double lnbLo2MHz = 10600.0;
    double lnbSwitchMHz = 11700.0;
    Polarization polarization = Polarization::HorizontalLeft18V;
    ToneMode tone = ToneMode::Auto;
    int diseqcPort = -1; // -1 = no DiSEqC, 0..3 = committed port A..D

    // Multistream / physical layer scrambling
    int inputStreamId = -1; // -1 = no ISI filter
    PlsMode plsMode = PlsMode::Gold;
    uint32_t plsCode = 0;

    // Data
    CaptureMode captureMode = CaptureMode::TransportStream;

    // File output
    bool fileEnabled = false;
    QString filePath;
    bool fileTimestampSuffix = true;
    BbFileFormat bbFileFormat = BbFileFormat::Raw;

    // ZeroMQ output
    bool zmqEnabled = false;
    QString zmqEndpoint = QStringLiteral("tcp://*:5555");
    ZmqSocketKind zmqSocket = ZmqSocketKind::Pub;
    bool zmqBind = true;
    QString zmqTopic;
    bool zmqMetadataHeader = false;
    int zmqTsPacketsPerMessage = 7;
    int zmqSendHwm = 10000;

    // Preview
    bool videoPreview = true;

    // ---- Derived values ----
    bool isHighBand() const;
    // Intermediate frequency handed to the frontend (kHz)
    uint32_t intermediateFrequencyKHz() const;
    // Whether the 22 kHz tone must be on
    bool toneOn() const;
    uint32_t symbolRateSps() const { return static_cast<uint32_t>(symbolRateKsps * 1000.0 + 0.5); }
    // Value for DTV_STREAM_ID following the TBS driver convention
    // (isi | pls_code << 8 | pls_mode << 26), or ~0u (NO_STREAM_ID_FILTER)
    uint32_t dvbStreamId() const;
    // Gold code for DTV_SCRAMBLING_SEQUENCE_INDEX (used when no ISI filter is set)
    uint32_t goldCode() const;

    QString describe() const;

    void load(QSettings &s);
    void save(QSettings &s) const;
};
