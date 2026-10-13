#pragma once

#include "Types.h"
#include "export.h"

#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

// Spectrum analysis, blind scan and constellation (IQ) display.
//
// These need a driver with the neumoDVB frontend API extensions (DTV_SPECTRUM, DTV_SCAN,
// DTV_CONSTELLATION), e.g. the "deepthought" branch of github.com/deeptho/linux_media,
// which supports the STiD135 of the TBS 6903x. The stock TBS driver does not have them:
// the calls then fail with an error saying so (capabilities() tells beforehand).
// Not available with Windows BDA or file replay.
namespace tbs {

enum class SpectrumMethod { Sweep = 0, Fft = 1 };

struct TBSDVB_API ScanCapabilities
{
    bool probed = false;        // the frontend could be opened and queried
    bool neumoApi = false;      // driver reports neumoDVB extensions
    bool spectrumSweep = false;
    bool spectrumFft = false;
    bool blindScan = false;
    bool constellation = false;
    std::string note;           // why something is missing
};

struct TBSDVB_API SpectrumRequest
{
    // RF range (the IF range for LnbType::Direct). A universal LNB range across the band
    // switch frequency is scanned as two segments (low band, then high band with 22 kHz).
    double startMHz = 10700.0;
    double endMHz = 12750.0;
    // LNB type and LOs, polarization, DiSEqC port; frequency and symbol rate are ignored
    TuningParams lnb;
    SpectrumMethod method = SpectrumMethod::Fft;
    int resolutionKHz = 0;       // 0 = automatic (driver default)
    int fftSize = 512;           // FFT method: power of two
    int timeoutMs = 60000;       // per band segment
};

struct TBSDVB_API SpectrumPoint
{
    double frequencyMHz = 0.0; // RF
    double level = 0.0;        // dB (see SpectrumResult::decibel)
};

// A carrier candidate: from the driver (blind scan / spectrum peaks) or findPeaks()
struct TBSDVB_API SpectrumPeak
{
    double frequencyMHz = 0.0;  // RF
    double symbolRateKsps = 0.0;
    double level = 0.0;
    double snrDb = 0.0;
};

struct TBSDVB_API SpectrumResult
{
    Polarization polarization = Polarization::HorizontalLeft18V;
    bool decibel = true;            // false: relative level 0..100
    std::vector<SpectrumPoint> points;  // sorted by frequency
    std::vector<SpectrumPeak> peaks;    // driver candidates, or findPeaks() if the driver has none
};

struct TBSDVB_API BlindScanRequest
{
    double startMHz = 10700.0;
    double endMHz = 12750.0;
    TuningParams lnb;             // as SpectrumRequest::lnb; inputStreamId/PLS select the stream to lock
    int resolutionKHz = 2000;
    int fftSize = 256;
    int maxSymbolRateKsps = 45000;
    int searchRangeKHz = 10000;   // how far a carrier may be from the candidate frequency
    std::vector<uint32_t> plsGoldCodes; // extra PLS gold codes to try
    int timeoutMs = 600000;       // per band segment
};

// A carrier the driver locked to
struct TBSDVB_API BlindScanCarrier
{
    TuningParams tuning;          // ready to tune (RF frequency, polarization, ISI from the request's LNB)
    double frequencyMHz = 0.0;    // RF
    double symbolRateKsps = 0.0;
    DeliverySystem deliverySystem = DeliverySystem::DvbS2;
    int modulation = -1;          // linux-dvb enum fe_modulation (DvbNames::modulation)
    int fec = -1;                 // enum fe_code_rate
    int rolloff = -1;             // enum fe_rolloff
    int pilot = -1;
    int matype = -1;              // MATYPE-1 of the locked stream
    std::vector<int> isis;        // input stream ids present (multistream), empty = single stream
    bool hasSignalDbm = false;
    double signalDbm = 0.0;
    bool hasCnrDb = false;
    double cnrDb = 0.0;

    std::string describe() const;
};

struct TBSDVB_API ConstellationSample
{
    int16_t i = 0;
    int16_t q = 0;
};

namespace Scan {

// Return false to cancel. fraction is 0..1 over the whole request; while the driver works the
// function is also polled with fraction -1 and an empty message (only the return value matters).
using ProgressFn = std::function<bool(double fraction, const std::string &message)>;

// Opens the frontend briefly and asks the driver (fails if the frontend is busy)
TBSDVB_API ScanCapabilities capabilities(const DeviceSelection &sel);

// Measures the RF spectrum. The frontend must not be in use (stop the receiver first).
TBSDVB_API bool spectrum(const DeviceSelection &sel, const SpectrumRequest &req, SpectrumResult *out,
                         std::string *error = nullptr, const ProgressFn &progress = {});

// Searches the range for carriers and reports each locked one through onCarrier as it is found.
// spectrumOut (optional) receives the spectrum the driver measured for the search.
TBSDVB_API bool blindScan(const DeviceSelection &sel, const BlindScanRequest &req, std::vector<BlindScanCarrier> *out,
                          std::string *error = nullptr, const ProgressFn &progress = {},
                          const std::function<void(const BlindScanCarrier &)> &onCarrier = {},
                          SpectrumResult *spectrumOut = nullptr);

// --- Helpers (pure functions, also used by the tests) ----------------------

struct TBSDVB_API BandSegment
{
    double startMHz = 0.0; // RF
    double endMHz = 0.0;
    TuningParams tuning;   // LNB settings for this segment (band / tone)
    uint32_t ifStartKHz = 0; // driver range, ifStart < ifEnd
    uint32_t ifEndKHz = 0;
};

// Splits an RF range into the parts an LNB can deliver in one setting
TBSDVB_API std::vector<BandSegment> bandSegments(const TuningParams &lnb, double startMHz, double endMHz);
// IF (kHz, as the driver reports it) to RF (MHz) for the segment's LNB setting
TBSDVB_API double ifToRfMHz(const TuningParams &segmentTuning, double ifKHz);

// Carrier candidates in a spectrum: regions at least minRiseDb above the noise floor.
// The symbol rate is estimated from the 3 dB bandwidth.
TBSDVB_API std::vector<SpectrumPeak> findPeaks(const std::vector<SpectrumPoint> &points, double minRiseDb = 3.0,
                                               double minSymbolRateKsps = 500.0);

} // namespace Scan
} // namespace tbs
