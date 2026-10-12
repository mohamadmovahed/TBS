#pragma once

#include "export.h"

#include <cstdint>
#include <string>

namespace tbs {

enum class DeviceKind {
    LinuxDvb = 0,   // Linux DVB API (TBS open source driver)
    FileReplay = 1, // Recorded TS / pseudo-TS file
    WindowsBda = 2, // Windows BDA / DirectShow (TBS BDA driver)
};

// The STiD135 handles DVB-S2X as part of DVB-S2.
enum class DeliverySystem { DvbS = 0, DvbS2 = 1 };

enum class ModulationHint { Auto = 0, Qpsk, Psk8, Apsk16, Apsk32 };

enum class LnbType {
    Direct = 0,    // No LNB / L-band input: the frequency is already the IF
    Universal = 1, // Two LOs with a 22 kHz band switch (Ku band)
    SingleLo = 2,  // Single LO (C band 5150, Ku 10750, ...)
};

enum class Polarization { Off = 0, VerticalRight13V = 1, HorizontalLeft18V = 2 };

enum class ToneMode { Auto = 0, Off = 1, On = 2 };

enum class PlsMode { Root = 0, Gold = 1, Combo = 2 };

enum class CaptureMode {
    TransportStream = 0, // forward TS packets
    BBFrame = 1,         // reassemble BBFrames from the card's pseudo-TS
};

enum class BbFileFormat {
    Raw = 0,      // BBHeader + data field, back to back
    B8Marker = 1, // 0xB8 + BBFrame (bbframe-tools format)
    PseudoTs = 2, // the 188-byte pseudo-TS packets as received
};

enum class ZmqSocketKind { Pub = 0, Push = 1 };

// Everything needed to tune a satellite carrier.
struct TBSDVB_API TuningParams
{
    double frequencyMHz = 11494.0;   // RF frequency (IF if lnbType == Direct)
    double symbolRateKsps = 27500.0;
    DeliverySystem deliverySystem = DeliverySystem::DvbS2;
    ModulationHint modulation = ModulationHint::Auto;

    LnbType lnbType = LnbType::Universal;
    double lnbLo1MHz = 9750.0;
    double lnbLo2MHz = 10600.0;
    double lnbSwitchMHz = 11700.0;
    Polarization polarization = Polarization::HorizontalLeft18V;
    ToneMode tone = ToneMode::Auto;
    int diseqcPort = -1; // -1 = none, 0..3 = committed port A..D

    int inputStreamId = -1; // -1 = no ISI filter
    PlsMode plsMode = PlsMode::Gold;
    uint32_t plsCode = 0;

    bool isHighBand() const;
    uint32_t intermediateFrequencyKHz() const;
    // LO actually used for the current frequency (MHz, 0 for Direct)
    double activeLoMHz() const;
    bool toneOn() const;
    uint32_t symbolRateSps() const { return static_cast<uint32_t>(symbolRateKsps * 1000.0 + 0.5); }
    // DTV_STREAM_ID following the TBS driver convention (isi | code << 8 | mode << 26), ~0u = no filter
    uint32_t dvbStreamId() const;
    // Gold code (root codes are converted)
    uint32_t goldCode() const;
    std::string describe() const;
};

// Which device to open.
struct TBSDVB_API DeviceSelection
{
#if defined(_WIN32)
    DeviceKind kind = DeviceKind::WindowsBda;
#else
    DeviceKind kind = DeviceKind::LinuxDvb;
#endif
    // Linux DVB
    int adapter = 0;
    int frontend = 0;
    int demux = 0;
    // Windows BDA: device path of the tuner filter (preferred) or index in the BDA tuner list
    std::string bdaDevicePath;
    int bdaIndex = 0;
    // File replay
    std::string replayFile;
    double replayRateMbps = 0.0; // 0 = as fast as possible
    bool replayLoop = true;
};

struct TBSDVB_API FileOutputOptions
{
    bool enabled = false;
    std::string path;            // empty = <home>/capture.ts|.bbf
    bool timestampSuffix = true;
    BbFileFormat bbFormat = BbFileFormat::Raw;
    // Recording management
    uint64_t splitSizeMB = 0;  // start a new file every N MiB (0 = never)
    int splitMinutes = 0;      // start a new file every N minutes (0 = never)
    int keepFiles = 0;         // with splitting: keep only the newest N files (0 = keep all)
    uint64_t minFreeMB = 500;  // stop recording when free disk space drops below N MiB (0 = no check)
};

struct TBSDVB_API ZmqOutputOptions
{
    bool enabled = false;
    std::string endpoint = "tcp://*:5555";
    ZmqSocketKind socket = ZmqSocketKind::Pub;
    bool bind = true;
    std::string topic;
    bool metadataHeader = false;
    int tsPacketsPerMessage = 7;
    int sendHwm = 10000;
};

struct TBSDVB_API ReceiverConfig
{
    DeviceSelection device;
    TuningParams tuning;
    CaptureMode capture = CaptureMode::TransportStream;
    FileOutputOptions file;
    ZmqOutputOptions zmq;
};

// A receiver found on this machine.
struct TBSDVB_API DeviceInfo
{
    DeviceKind kind = DeviceKind::LinuxDvb;
    std::string name;  // e.g. "TBS 6903x DVBS/S2X Tuner A" or "STiD135 Multistandard"
    std::string path;  // /dev/dvb/adapter0/frontend0 or the BDA device path
    int adapter = -1;  // Linux DVB
    int frontend = -1; // Linux DVB
    int index = -1;    // Windows BDA tuner index

    std::string label() const;
    // Fills the device part of a selection for this device
    void applyTo(DeviceSelection &sel) const;
};

TBSDVB_API const char *version();

} // namespace tbs
