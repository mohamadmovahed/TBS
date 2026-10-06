#include "WinBdaDevice.h"

#include "../Util.h"

#include <windows.h>

#include <dshow.h>
#include <ks.h>
#include <ksmedia.h>
#include <bdatypes.h>
#include <bdamedia.h>
#include <bdaiface.h>
#include <tuner.h>

#include <algorithm>
#include <cstring>
#include <cmath>
#include <cwchar>

namespace tbs {
namespace {

// ---------------------------------------------------------------------------
// GUIDs (defined here so the build does not depend on which ones a given SDK
// or MinGW version exports from its import libraries)
// ---------------------------------------------------------------------------
const GUID kCLSID_FilterGraph = {0xe436ebb3, 0x524f, 0x11ce, {0x9f, 0x53, 0x00, 0x20, 0xaf, 0x0b, 0xa7, 0x70}};
const GUID kCLSID_SystemDeviceEnum = {0x62be5d10, 0x60eb, 0x11d0, {0xbd, 0x3b, 0x00, 0xa0, 0xc9, 0x11, 0xce, 0x86}};
const GUID kCLSID_NetworkProvider = {0xb2f3a67c, 0x29da, 0x4c78, {0x88, 0x31, 0x09, 0x1e, 0xd5, 0x09, 0xa4, 0x75}};
const GUID kCLSID_DVBSNetworkProvider = {0xfa4b375a, 0x45b4, 0x4d45, {0x84, 0x40, 0x26, 0x39, 0x57, 0xb1, 0x16, 0x23}};
const GUID kCLSID_DVBSTuningSpace = {0xb64016f3, 0xc9a2, 0x4066, {0x96, 0xf0, 0xbd, 0x95, 0x63, 0x31, 0x47, 0x26}};
const GUID kCLSID_DVBSLocator = {0x1df7d126, 0x4050, 0x47f0, {0xa7, 0xcf, 0x4c, 0x4c, 0xa9, 0x24, 0x13, 0x33}};
const GUID kCLSID_InfTee = {0xf8388a40, 0xd5bb, 0x11d0, {0xbe, 0x5a, 0x00, 0x80, 0xc7, 0x06, 0x56, 0x8e}};
const GUID kCLSID_MPEG2Demultiplexer = {0xafb6c280, 0x2c41, 0x11d3, {0x8a, 0x60, 0x00, 0x00, 0xf8, 0x1e, 0x0e, 0x4a}};
const GUID kCLSID_SampleGrabber = {0xc1f400a0, 0x3f08, 0x11d3, {0x9f, 0x0b, 0x00, 0x60, 0x08, 0x03, 0x9e, 0x37}};
const GUID kCLSID_NullRenderer = {0xc1f400a4, 0x3f08, 0x11d3, {0x9f, 0x0b, 0x00, 0x60, 0x08, 0x03, 0x9e, 0x37}};
const GUID kIID_ISampleGrabber = {0x6b652fff, 0x11fe, 0x4fce, {0x92, 0xad, 0x02, 0x66, 0xb5, 0xd7, 0xc7, 0x8f}};
const GUID kIID_ISampleGrabberCB = {0x0579154a, 0x2b53, 0x4994, {0xb0, 0xd0, 0xe7, 0x73, 0x14, 0x8e, 0xff, 0x85}};
const GUID kKSCATEGORY_BDA_NETWORK_TUNER = {0x71985f48, 0x1ca1, 0x11d3, {0x9c, 0xc8, 0x00, 0xc0, 0x4f, 0x79, 0x71, 0xe0}};
const GUID kKSCATEGORY_BDA_RECEIVER_COMPONENT = {0xfd0a5af4, 0xb41d, 0x11d2, {0x9c, 0x95, 0x00, 0xc0, 0x4f, 0x79, 0x71, 0xe0}};
const GUID kKSCATEGORY_BDA_TRANSPORT_INFORMATION = {0xa2e3074f, 0x6c3d, 0x11d3, {0xb6, 0x53, 0x00, 0xc0, 0x4f, 0x79, 0x49, 0x8e}};
const GUID kMEDIATYPE_Stream = {0xe436eb83, 0x524f, 0x11ce, {0x9f, 0x53, 0x00, 0x20, 0xaf, 0x0b, 0xa7, 0x70}};
const GUID kMEDIASUBTYPE_MPEG2_TRANSPORT = {0xe06d8023, 0xdb46, 0x11cf, {0xb4, 0xd1, 0x00, 0x80, 0x5f, 0x6c, 0xbb, 0xea}};

// TBS BDA extension property set (PCI/PCIe cards, on the tuner filter's input pin)
const GUID kTbsPropSet = {0xfaa8f3e5, 0x31d4, 0x4e41, {0x88, 0xef, 0xd9, 0xeb, 0x71, 0x6f, 0x6e, 0xc9}};
constexpr DWORD kTbsPropNbcParams = 10; // DVB-S2 parameters
constexpr DWORD kTbsPropAccess = 21;    // LNB power / tone / DiSEqC
constexpr DWORD kTbsPropStreamId = 96;  // input stream ID (ISI / PLS)

#pragma pack(push, 1)
struct TbsAccessParams
{
    uint32_t accessMode; // 0 = LNB power, 1 = DiSEqC, 2 = tone
    uint32_t tone;       // 0 off, 1 on, 2 burst A (unmodulated), 3 burst B (modulated)
    uint32_t reserved1;
    uint32_t lnbPower;   // 0 off, 1 18 V, 2 13 V, 3 on (previous voltage)
    uint8_t diseqcTx[128];
    uint32_t diseqcTxLength;
    uint8_t diseqcRx[128];
    uint32_t diseqcRxLength;
    uint8_t reserved2[256];
};
struct TbsNbcParams
{
    uint32_t rolloff; // 0 = 0.20, 1 = 0.25, 2 = 0.35, 0xFF = undefined
    uint32_t pilot;   // 0 off, 1 on
    uint32_t dvbType; // 0 auto, 1 DVB-S, 2 DVB-S2
    int32_t fecRate;  // BinaryConvolutionCodeRate
    int32_t modulation; // ModulationType
};
#pragma pack(pop)
static_assert(sizeof(TbsAccessParams) == 0x218, "TBS access parameter layout");
static_assert(sizeof(TbsNbcParams) == 20, "TBS NBC parameter layout");

// Sample Grabber interfaces (qedit.h is no longer shipped with the Windows SDK)
struct ISampleGrabberCBTbs : public IUnknown
{
    virtual HRESULT STDMETHODCALLTYPE SampleCB(double sampleTime, IMediaSample *sample) = 0;
    virtual HRESULT STDMETHODCALLTYPE BufferCB(double sampleTime, BYTE *buffer, long bufferLen) = 0;
};
struct ISampleGrabberTbs : public IUnknown
{
    virtual HRESULT STDMETHODCALLTYPE SetOneShot(BOOL oneShot) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetMediaType(const AM_MEDIA_TYPE *type) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetConnectedMediaType(AM_MEDIA_TYPE *type) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetBufferSamples(BOOL bufferThem) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetCurrentBuffer(long *bufferSize, long *buffer) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetCurrentSample(IMediaSample **sample) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetCallback(ISampleGrabberCBTbs *callback, long whichMethod) = 0;
};

// ---------------------------------------------------------------------------
// Small COM helpers
// ---------------------------------------------------------------------------
template <class T>
class ComPtr
{
public:
    ComPtr() = default;
    ComPtr(const ComPtr &o) : p(o.p) { if (p) p->AddRef(); }
    ComPtr(ComPtr &&o) noexcept : p(o.p) { o.p = nullptr; }
    ~ComPtr() { reset(); }
    ComPtr &operator=(ComPtr o) noexcept { std::swap(p, o.p); return *this; }

    T *get() const { return p; }
    T *operator->() const { return p; }
    explicit operator bool() const { return p != nullptr; }
    T **put() { reset(); return &p; }
    void **putVoid() { return reinterpret_cast<void **>(put()); }
    void reset() { if (p) { p->Release(); p = nullptr; } }

    template <class U>
    ComPtr<U> as() const
    {
        ComPtr<U> out;
        if (p)
            p->QueryInterface(__uuidof(U), out.putVoid());
        return out;
    }
    template <class U>
    ComPtr<U> as(REFIID iid) const
    {
        ComPtr<U> out;
        if (p)
            p->QueryInterface(iid, out.putVoid());
        return out;
    }

private:
    T *p = nullptr;
};

template <class T>
ComPtr<T> createInstance(REFCLSID clsid, REFIID iid = __uuidof(T))
{
    ComPtr<T> out;
    CoCreateInstance(clsid, nullptr, CLSCTX_INPROC_SERVER, iid, out.putVoid());
    return out;
}

class Bstr
{
public:
    explicit Bstr(const wchar_t *s) : b(SysAllocString(s)) {}
    ~Bstr() { SysFreeString(b); }
    Bstr(const Bstr &) = delete;
    Bstr &operator=(const Bstr &) = delete;
    operator BSTR() const { return b; }

private:
    BSTR b;
};

std::string narrow(const wchar_t *w)
{
    if (!w || !*w)
        return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    std::string s(static_cast<size_t>(n > 0 ? n - 1 : 0), '\0');
    if (n > 1)
        WideCharToMultiByte(CP_UTF8, 0, w, -1, &s[0], n, nullptr, nullptr);
    return s;
}

std::string hrString(HRESULT hr)
{
    return util::format("0x%08lx", static_cast<unsigned long>(hr));
}

// Initialises COM for the current thread for the lifetime of the object.
class ComScope
{
public:
    ComScope()
    {
        const HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        m_uninit = SUCCEEDED(hr); // RPC_E_CHANGED_MODE: thread already in an STA, fine
    }
    ~ComScope()
    {
        if (m_uninit)
            CoUninitialize();
    }
    ComScope(const ComScope &) = delete;
    ComScope &operator=(const ComScope &) = delete;

private:
    bool m_uninit = false;
};

std::string readProperty(IMoniker *moniker, const wchar_t *name)
{
    ComPtr<IPropertyBag> bag;
    if (FAILED(moniker->BindToStorage(nullptr, nullptr, __uuidof(IPropertyBag), bag.putVoid())))
        return {};
    VARIANT v;
    VariantInit(&v);
    std::string out;
    if (SUCCEEDED(bag->Read(name, &v, nullptr)) && v.vt == VT_BSTR)
        out = narrow(v.bstrVal);
    VariantClear(&v);
    return out;
}

std::vector<ComPtr<IMoniker>> monikers(REFGUID category)
{
    std::vector<ComPtr<IMoniker>> out;
    auto devEnum = createInstance<ICreateDevEnum>(kCLSID_SystemDeviceEnum);
    if (!devEnum)
        return out;
    ComPtr<IEnumMoniker> e;
    if (devEnum->CreateClassEnumerator(category, e.put(), 0) != S_OK || !e)
        return out;
    ComPtr<IMoniker> m;
    while (e->Next(1, m.put(), nullptr) == S_OK)
        out.push_back(m);
    return out;
}

ComPtr<IBaseFilter> bindFilter(IMoniker *moniker)
{
    ComPtr<IBaseFilter> f;
    moniker->BindToObject(nullptr, nullptr, __uuidof(IBaseFilter), f.putVoid());
    return f;
}

bool isConnected(IPin *pin)
{
    ComPtr<IPin> other;
    return SUCCEEDED(pin->ConnectedTo(other.put())) && other;
}

std::vector<ComPtr<IPin>> pins(IBaseFilter *filter, PIN_DIRECTION dir, bool unconnectedOnly)
{
    std::vector<ComPtr<IPin>> out;
    ComPtr<IEnumPins> e;
    if (FAILED(filter->EnumPins(e.put())))
        return out;
    ComPtr<IPin> p;
    while (e->Next(1, p.put(), nullptr) == S_OK) {
        PIN_DIRECTION d;
        if (SUCCEEDED(p->QueryDirection(&d)) && d == dir && (!unconnectedOnly || !isConnected(p.get())))
            out.push_back(p);
    }
    return out;
}

// Connects the first compatible free output pin of up to a free input pin of down.
bool connectFilters(IGraphBuilder *graph, IBaseFilter *up, IBaseFilter *down)
{
    for (auto &out : pins(up, PINDIR_OUTPUT, true)) {
        for (auto &in : pins(down, PINDIR_INPUT, true)) {
            if (SUCCEEDED(graph->ConnectDirect(out.get(), in.get(), nullptr)))
                return true;
        }
    }
    for (auto &out : pins(up, PINDIR_OUTPUT, true)) {
        for (auto &in : pins(down, PINDIR_INPUT, true)) {
            if (SUCCEEDED(graph->Connect(out.get(), in.get())))
                return true;
        }
    }
    return false;
}

ModulationType bdaModulation(const TuningParams &t)
{
    if (t.deliverySystem == DeliverySystem::DvbS)
        return BDA_MOD_QPSK;
    switch (t.modulation) {
    case ModulationHint::Qpsk:
        return BDA_MOD_NBC_QPSK;
    case ModulationHint::Apsk16:
        return BDA_MOD_16APSK;
    case ModulationHint::Apsk32:
        return BDA_MOD_32APSK;
    case ModulationHint::Psk8:
    case ModulationHint::Auto:
        break;
    }
    return BDA_MOD_NBC_8PSK;
}

// LNB description for BDA (kHz)
struct BdaLnb
{
    long low;
    long high;
    long sw;
};

BdaLnb bdaLnb(const TuningParams &t)
{
    constexpr long kNoSwitch = 30000000; // above any satellite band: always "low band"
    switch (t.lnbType) {
    case LnbType::Universal:
        return {std::lround(t.lnbLo1MHz * 1000), std::lround(t.lnbLo2MHz * 1000), std::lround(t.lnbSwitchMHz * 1000)};
    case LnbType::SingleLo:
        return {std::lround(t.lnbLo1MHz * 1000), std::lround(t.lnbLo1MHz * 1000), kNoSwitch};
    case LnbType::Direct:
        break;
    }
    return {0, 0, kNoSwitch};
}

// Sample grabber callback forwarding to the device
class GrabberCallback : public ISampleGrabberCBTbs
{
public:
    explicit GrabberCallback(WinBdaDevice *dev) : m_dev(dev) {}

    STDMETHODIMP QueryInterface(REFIID riid, void **ppv) override
    {
        if (!ppv)
            return E_POINTER;
        if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, kIID_ISampleGrabberCB)) {
            *ppv = static_cast<ISampleGrabberCBTbs *>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    // Lifetime is owned by WinBdaDevice::Impl
    STDMETHODIMP_(ULONG) AddRef() override { return 2; }
    STDMETHODIMP_(ULONG) Release() override { return 1; }

    STDMETHODIMP SampleCB(double, IMediaSample *) override { return E_NOTIMPL; }
    STDMETHODIMP BufferCB(double, BYTE *buffer, long len) override
    {
        if (buffer && len > 0)
            m_dev->onBuffer(buffer, static_cast<size_t>(len));
        return S_OK;
    }

private:
    WinBdaDevice *m_dev;
};

} // namespace

// ---------------------------------------------------------------------------

struct WinBdaDevice::Impl
{
    explicit Impl(WinBdaDevice *dev) : callback(dev) {}

    std::unique_ptr<ComScope> com;
    ComPtr<IGraphBuilder> graph;
    ComPtr<IMediaControl> control;
    ComPtr<IBaseFilter> provider;
    ComPtr<ITuner> tuner;
    ComPtr<ITuningSpace> tuningSpace;
    ComPtr<IBaseFilter> tunerFilter;
    ComPtr<IBaseFilter> grabberFilter;
    ComPtr<ISampleGrabberTbs> grabber;
    std::vector<ComPtr<IBDA_SignalStatistics>> statistics;
    ComPtr<IKsPropertySet> tbsProps;
    bool tbsAccess = false;
    bool tbsNbc = false;
    bool tbsStreamId = false;
    GrabberCallback callback;

    bool tbsSet(DWORD id, void *data, DWORD size)
    {
        if (!tbsProps)
            return false;
        return SUCCEEDED(tbsProps->Set(kTbsPropSet, id, data, size, data, size));
    }
};

WinBdaDevice::WinBdaDevice()
    : d(std::make_unique<Impl>(this))
{
}

WinBdaDevice::~WinBdaDevice()
{
    close();
}

std::vector<DeviceInfo> WinBdaDevice::enumerate()
{
    ComScope com;
    std::vector<DeviceInfo> list;
    int index = 0;
    for (auto &m : monikers(kKSCATEGORY_BDA_NETWORK_TUNER)) {
        DeviceInfo info;
        info.kind = DeviceKind::WindowsBda;
        info.index = index++;
        info.name = readProperty(m.get(), L"FriendlyName");
        info.path = readProperty(m.get(), L"DevicePath");
        if (info.name.empty())
            info.name = "BDA tuner";
        list.push_back(info);
    }
    return list;
}

void WinBdaDevice::onBuffer(const uint8_t *data, size_t len)
{
    constexpr size_t kMaxQueued = 64u << 20;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_queuedBytes + len > kMaxQueued) {
            ++m_overflows;
            return;
        }
        m_queue.emplace_back(data, data + len);
        m_queuedBytes += len;
    }
    m_cv.notify_one();
}

bool WinBdaDevice::open(const DeviceSelection &sel, const TuningParams &tuning, CaptureMode, std::string *error)
{
    close();
    auto fail = [&](const std::string &msg) {
        if (error)
            *error = msg;
        close();
        return false;
    };

    d->com = std::make_unique<ComScope>();

    // ---- Locate the tuner ----
    auto tuners = monikers(kKSCATEGORY_BDA_NETWORK_TUNER);
    ComPtr<IMoniker> tunerMoniker;
    for (size_t i = 0; i < tuners.size() && !tunerMoniker; ++i) {
        if (!sel.bdaDevicePath.empty()) {
            if (_stricmp(readProperty(tuners[i].get(), L"DevicePath").c_str(), sel.bdaDevicePath.c_str()) == 0)
                tunerMoniker = tuners[i];
        } else if (static_cast<int>(i) == sel.bdaIndex) {
            tunerMoniker = tuners[i];
        }
    }
    if (!tunerMoniker)
        return fail(util::format("BDA tuner not found (%u BDA tuners present). Is the TBS BDA driver installed?", static_cast<unsigned>(tuners.size())));
    m_name = readProperty(tunerMoniker.get(), L"FriendlyName");
    log("Opening BDA tuner: " + m_name);

    // ---- Graph and network provider ----
    d->graph = createInstance<IGraphBuilder>(kCLSID_FilterGraph);
    if (!d->graph)
        return fail("Cannot create the DirectShow filter graph");
    d->control = d->graph.as<IMediaControl>();

    auto space = createInstance<IDVBSTuningSpace>(kCLSID_DVBSTuningSpace);
    if (!space)
        return fail("Cannot create a DVB-S tuning space");
    space->put_UniqueName(Bstr(L"TbsDvb DVB-S"));
    space->put_FriendlyName(Bstr(L"TbsDvb DVB-S"));
    space->put__NetworkType(kCLSID_DVBSNetworkProvider);
    space->put_SystemType(DVB_Satellite);
    space->put_SpectralInversion(BDA_SPECTRAL_INVERSION_AUTOMATIC);
    const BdaLnb lnb = bdaLnb(tuning);
    space->put_LowOscillator(lnb.low);
    space->put_HighOscillator(lnb.high);
    space->put_LNBSwitch(lnb.sw);
    if (auto defLocator = createInstance<IDVBSLocator>(kCLSID_DVBSLocator))
        space->put_DefaultLocator(defLocator.get());
    d->tuningSpace = space.as<ITuningSpace>();

    for (const GUID *clsid : {&kCLSID_NetworkProvider, &kCLSID_DVBSNetworkProvider}) {
        auto np = createInstance<IBaseFilter>(*clsid);
        if (!np || FAILED(d->graph->AddFilter(np.get(), L"Network Provider")))
            continue;
        auto t = np.as<ITuner>();
        if (t && SUCCEEDED(t->put_TuningSpace(d->tuningSpace.get()))) {
            d->provider = np;
            d->tuner = t;
            log(clsid == &kCLSID_NetworkProvider ? "Using the generic network provider" : "Using the DVB-S network provider");
            break;
        }
        d->graph->RemoveFilter(np.get());
    }
    if (!d->provider)
        return fail("No usable BDA network provider");

    // ---- Tuner and capture filters ----
    d->tunerFilter = bindFilter(tunerMoniker.get());
    if (!d->tunerFilter || FAILED(d->graph->AddFilter(d->tunerFilter.get(), L"Tuner")))
        return fail("Cannot load the BDA tuner filter");
    if (!connectFilters(d->graph.get(), d->provider.get(), d->tunerFilter.get()))
        return fail("Cannot connect the network provider to the tuner");

    ComPtr<IBaseFilter> last = d->tunerFilter;
    for (auto &m : monikers(kKSCATEGORY_BDA_RECEIVER_COMPONENT)) {
        auto f = bindFilter(m.get());
        if (!f || FAILED(d->graph->AddFilter(f.get(), L"Capture")))
            continue;
        if (connectFilters(d->graph.get(), d->tunerFilter.get(), f.get())) {
            log("Capture filter: " + readProperty(m.get(), L"FriendlyName"));
            last = f;
            break;
        }
        d->graph->RemoveFilter(f.get());
    }

    // ---- Tee, sample grabber and null renderer ----
    auto tee = createInstance<IBaseFilter>(kCLSID_InfTee);
    if (!tee || FAILED(d->graph->AddFilter(tee.get(), L"Tee")) || !connectFilters(d->graph.get(), last.get(), tee.get()))
        return fail("Cannot connect the infinite tee to the tuner/capture filter");

    d->grabberFilter = createInstance<IBaseFilter>(kCLSID_SampleGrabber);
    d->grabber = d->grabberFilter.as<ISampleGrabberTbs>(kIID_ISampleGrabber);
    if (!d->grabber)
        return fail("Sample Grabber filter is not available (qedit.dll)");
    AM_MEDIA_TYPE mt;
    std::memset(&mt, 0, sizeof(mt));
    mt.majortype = kMEDIATYPE_Stream;
    mt.subtype = kMEDIASUBTYPE_MPEG2_TRANSPORT;
    d->grabber->SetMediaType(&mt);
    d->grabber->SetOneShot(FALSE);
    d->grabber->SetBufferSamples(FALSE);
    d->grabber->SetCallback(&d->callback, 1);
    if (FAILED(d->graph->AddFilter(d->grabberFilter.get(), L"Grabber")))
        return fail("Cannot add the sample grabber");
    if (!connectFilters(d->graph.get(), tee.get(), d->grabberFilter.get())) {
        // Accept whatever transport stream subtype the capture filter offers
        std::memset(&mt, 0, sizeof(mt));
        mt.majortype = kMEDIATYPE_Stream;
        d->grabber->SetMediaType(&mt);
        if (!connectFilters(d->graph.get(), tee.get(), d->grabberFilter.get()))
            return fail("Cannot connect the sample grabber");
    }
    auto nullRenderer = createInstance<IBaseFilter>(kCLSID_NullRenderer);
    if (!nullRenderer || FAILED(d->graph->AddFilter(nullRenderer.get(), L"Null"))
        || !connectFilters(d->graph.get(), d->grabberFilter.get(), nullRenderer.get()))
        return fail("Cannot connect the null renderer");

    // ---- Demux + TIF (best effort; some network providers do not tune without it) ----
    if (auto demux = createInstance<IBaseFilter>(kCLSID_MPEG2Demultiplexer)) {
        if (SUCCEEDED(d->graph->AddFilter(demux.get(), L"Demux")) && connectFilters(d->graph.get(), tee.get(), demux.get())) {
            for (auto &m : monikers(kKSCATEGORY_BDA_TRANSPORT_INFORMATION)) {
                if (readProperty(m.get(), L"FriendlyName").find("Transport Information") == std::string::npos)
                    continue;
                auto tif = bindFilter(m.get());
                if (tif && SUCCEEDED(d->graph->AddFilter(tif.get(), L"TIF"))) {
                    if (!connectFilters(d->graph.get(), demux.get(), tif.get()))
                        d->graph->RemoveFilter(tif.get());
                }
                break;
            }
        }
    }

    // ---- Signal statistics from the tuner topology ----
    if (auto topo = d->tunerFilter.as<IBDA_Topology>()) {
        ULONG count = 0;
        ULONG types[32];
        if (SUCCEEDED(topo->GetNodeTypes(&count, 32, types))) {
            for (ULONG i = 0; i < count; ++i) {
                ComPtr<IUnknown> node;
                if (SUCCEEDED(topo->GetControlNode(0, 1, types[i], node.put())) && node) {
                    if (auto st = node.as<IBDA_SignalStatistics>())
                        d->statistics.push_back(st);
                }
            }
        }
    }

    // ---- TBS extensions ----
    for (auto &pin : pins(d->tunerFilter.get(), PINDIR_INPUT, false)) {
        auto props = pin.as<IKsPropertySet>();
        DWORD support = 0;
        if (props && SUCCEEDED(props->QuerySupported(kTbsPropSet, kTbsPropAccess, &support)) && (support & KSPROPERTY_SUPPORT_SET)) {
            d->tbsProps = props;
            d->tbsAccess = true;
            support = 0;
            d->tbsNbc = SUCCEEDED(props->QuerySupported(kTbsPropSet, kTbsPropNbcParams, &support)) && (support & KSPROPERTY_SUPPORT_SET);
            support = 0;
            d->tbsStreamId = SUCCEEDED(props->QuerySupported(kTbsPropSet, kTbsPropStreamId, &support)) && (support & KSPROPERTY_SUPPORT_SET);
            break;
        }
    }
    log(d->tbsAccess ? util::format("TBS extensions found (DVB-S2 params: %s, stream ID: %s)", d->tbsNbc ? "yes" : "no",
                                    d->tbsStreamId ? "yes" : "no")
                     : std::string("TBS extensions not found: using standard BDA only"));

    if (!tune(tuning, error)) {
        close();
        return false;
    }
    return true;
}

bool WinBdaDevice::tune(const TuningParams &t, std::string *error)
{
    if (!d->tuner) {
        if (error)
            *error = "Tuner not open";
        return false;
    }

    // ---- TBS: LNB power, DiSEqC, DVB-S2 parameters, stream ID ----
    bool rawDiseqcSent = false;
    if (d->tbsAccess) {
        TbsAccessParams p;
        std::memset(&p, 0, sizeof(p));
        p.accessMode = 0;
        p.lnbPower = t.polarization == Polarization::Off ? 0u : (t.polarization == Polarization::HorizontalLeft18V ? 1u : 2u);
        if (!d->tbsSet(kTbsPropAccess, &p, sizeof(p)))
            log("TBS: setting LNB power failed");

        if (t.diseqcPort >= 0) {
            std::memset(&p, 0, sizeof(p));
            p.accessMode = 1;
            p.diseqcTx[0] = 0xE0;
            p.diseqcTx[1] = 0x10;
            p.diseqcTx[2] = 0x38;
            p.diseqcTx[3] = static_cast<uint8_t>(0xF0 | ((t.diseqcPort & 0x3) << 2)
                                                 | (t.polarization == Polarization::HorizontalLeft18V ? 0x02 : 0x00)
                                                 | (t.toneOn() ? 0x01 : 0x00));
            p.diseqcTxLength = 4;
            rawDiseqcSent = d->tbsSet(kTbsPropAccess, &p, sizeof(p));
            log(rawDiseqcSent ? util::format("TBS: DiSEqC committed port %c sent", 'A' + t.diseqcPort)
                              : std::string("TBS: DiSEqC command failed, falling back to the BDA LNB source"));
            util::sleepMs(20);
        }
    }
    if (d->tbsNbc) {
        TbsNbcParams nbc;
        nbc.rolloff = 0xFF;
        nbc.pilot = 0;
        nbc.dvbType = t.deliverySystem == DeliverySystem::DvbS ? 1u : 2u;
        nbc.fecRate = BDA_BCC_RATE_NOT_SET;
        nbc.modulation = bdaModulation(t);
        if (!d->tbsSet(kTbsPropNbcParams, &nbc, sizeof(nbc)))
            log("TBS: setting DVB-S2 parameters failed");
    }
    if (d->tbsStreamId) {
        // Same packing as the Linux driver; ~0 clears the filter.
        ULONG id = t.dvbStreamId();
        if (t.inputStreamId < 0 && t.goldCode() != 0)
            id = 0xFF | ((t.goldCode() & 0x3FFFF) << 8) | (1u << 26);
        if (!d->tbsSet(kTbsPropStreamId, &id, sizeof(id)))
            log("TBS: setting the stream ID failed");
        else if (t.inputStreamId >= 0 || t.goldCode() != 0)
            log(util::format("TBS: stream ID 0x%08lx", static_cast<unsigned long>(id)));
    } else if (t.inputStreamId >= 0 || t.goldCode() != 0) {
        log("WARNING: this driver exposes no stream ID control; ISI/PLS ignored");
    }

    // ---- Standard BDA tune request ----
    const BdaLnb lnb = bdaLnb(t);
    if (auto space = d->tuningSpace.as<IDVBSTuningSpace>()) {
        space->put_LowOscillator(lnb.low);
        space->put_HighOscillator(lnb.high);
        space->put_LNBSwitch(lnb.sw);
    }

    ComPtr<ITuneRequest> request;
    HRESULT hr = d->tuningSpace->CreateTuneRequest(request.put());
    if (FAILED(hr) || !request) {
        if (error)
            *error = "CreateTuneRequest failed: " + hrString(hr);
        return false;
    }
    if (auto dvb = request.as<IDVBTuneRequest>()) {
        dvb->put_ONID(-1);
        dvb->put_TSID(-1);
        dvb->put_SID(-1);
    }

    auto locator = createInstance<IDVBSLocator>(kCLSID_DVBSLocator);
    if (!locator) {
        if (error)
            *error = "Cannot create a DVB-S locator";
        return false;
    }
    const long freqKHz = t.lnbType == LnbType::Direct ? static_cast<long>(t.intermediateFrequencyKHz())
                                                       : std::lround(t.frequencyMHz * 1000.0);
    locator->put_CarrierFrequency(freqKHz);
    locator->put_SymbolRate(std::lround(t.symbolRateKsps));
    locator->put_SignalPolarisation(t.polarization == Polarization::HorizontalLeft18V ? BDA_POLARISATION_LINEAR_H
                                    : t.polarization == Polarization::VerticalRight13V ? BDA_POLARISATION_LINEAR_V
                                                                                        : BDA_POLARISATION_NOT_SET);
    locator->put_Modulation(bdaModulation(t));
    locator->put_InnerFEC(BDA_FEC_METHOD_NOT_SET);
    locator->put_InnerFECRate(BDA_BCC_RATE_NOT_SET);
    if (auto loc2 = locator.as<IDVBSLocator2>()) {
        loc2->put_LocalOscillatorOverrideLow(lnb.low);
        loc2->put_LocalOscillatorOverrideHigh(lnb.high);
        loc2->put_LocalLNBSwitchOverride(lnb.sw);
        loc2->put_LocalSpectralInversionOverride(BDA_SPECTRAL_INVERSION_AUTOMATIC);
        loc2->put_DiseqLNBSource(t.diseqcPort >= 0 && !rawDiseqcSent
                                     ? static_cast<LNB_Source>(BDA_LNB_SOURCE_A + t.diseqcPort)
                                     : BDA_LNB_SOURCE_NOT_SET);
        loc2->put_SignalRollOff(BDA_ROLL_OFF_NOT_SET);
        loc2->put_SignalPilot(BDA_PILOT_NOT_SET);
    }
    request->put_Locator(locator.get());

    hr = d->tuner->put_TuneRequest(request.get());
    if (FAILED(hr)) {
        if (error)
            *error = "put_TuneRequest failed: " + hrString(hr);
        return false;
    }

    // Explicit tone override after the network provider has applied the band
    if (d->tbsAccess && t.tone != ToneMode::Auto) {
        TbsAccessParams p;
        std::memset(&p, 0, sizeof(p));
        p.accessMode = 2;
        p.tone = t.toneOn() ? 1u : 0u;
        d->tbsSet(kTbsPropAccess, &p, sizeof(p));
    }

    log(util::format("Tuning %s", t.describe().c_str()));
    return true;
}

bool WinBdaDevice::startStream(std::string *error)
{
    if (!d->control) {
        if (error)
            *error = "Graph not built";
        return false;
    }
    HRESULT hr = d->control->Run();
    if (hr == S_FALSE) {
        OAFilterState state;
        hr = d->control->GetState(3000, &state);
    }
    if (FAILED(hr)) {
        if (error)
            *error = "Cannot run the BDA graph: " + hrString(hr);
        return false;
    }
    return true;
}

long WinBdaDevice::read(uint8_t *buf, size_t maxLen, int timeoutMs)
{
    std::unique_lock<std::mutex> lock(m_mutex);
    if (m_currentPos >= m_current.size()) {
        if (m_queue.empty() && !m_cv.wait_for(lock, std::chrono::milliseconds(timeoutMs), [this] { return !m_queue.empty(); }))
            return 0;
        m_current = std::move(m_queue.front());
        m_queue.pop_front();
        m_queuedBytes -= m_current.size();
        m_currentPos = 0;
    }
    const size_t n = std::min(maxLen, m_current.size() - m_currentPos);
    std::memcpy(buf, m_current.data() + m_currentPos, n);
    m_currentPos += n;
    return static_cast<long>(n);
}

FrontendStatus WinBdaDevice::readStatus()
{
    FrontendStatus st;
    st.deviceName = m_name;
    if (!d->tuner)
        return st;
    st.valid = true;

    bool locked = false;
    bool present = false;
    long quality = -1;
    long strength = 0;
    bool haveStrength = false;
    for (auto &s : d->statistics) {
        BOOLEAN b = FALSE;
        if (SUCCEEDED(s->get_SignalLocked(&b)) && b)
            locked = true;
        b = FALSE;
        if (SUCCEEDED(s->get_SignalPresent(&b)) && b)
            present = true;
        long q = 0;
        if (SUCCEEDED(s->get_SignalQuality(&q)) && q > quality)
            quality = q;
        long str = 0;
        if (!haveStrength && SUCCEEDED(s->get_SignalStrength(&str)) && str != 0) {
            strength = str;
            haveStrength = true;
        }
    }
    if (!haveStrength) {
        long str = 0;
        if (SUCCEEDED(d->tuner->get_SignalStrength(&str)) && str != 0) {
            strength = str;
            haveStrength = true;
        }
    }

    if (present || locked)
        st.flags |= FrontendStatus::HasSignal | FrontendStatus::HasCarrier;
    if (locked)
        st.flags |= FrontendStatus::HasViterbi | FrontendStatus::HasSync | FrontendStatus::HasLock;
    if (haveStrength) {
        // BDA reports strength in mdB; drivers differ, so accept dBm*1000 or a percentage
        if (strength < 0 && strength > -150000) {
            st.hasSignalDbm = true;
            st.signalDbm = strength / 1000.0;
        } else if (strength > 0 && strength <= 100) {
            st.hasSignalPercent = true;
            st.signalPercent = static_cast<double>(strength);
        }
    }
    if (quality >= 0) {
        st.hasCnrPercent = true;
        st.cnrPercent = static_cast<double>(std::min<long>(quality, 100));
    }
    return st;
}

void WinBdaDevice::close()
{
    if (d->control)
        d->control->Stop();
    if (d->grabber)
        d->grabber->SetCallback(nullptr, 1);
    if (d->tbsAccess) {
        TbsAccessParams p;
        std::memset(&p, 0, sizeof(p));
        p.accessMode = 0;
        p.lnbPower = 0;
        d->tbsSet(kTbsPropAccess, &p, sizeof(p));
    }
    d->statistics.clear();
    d->tbsProps.reset();
    d->tbsAccess = d->tbsNbc = d->tbsStreamId = false;
    d->grabber.reset();
    d->grabberFilter.reset();
    d->tunerFilter.reset();
    d->tuner.reset();
    d->provider.reset();
    d->tuningSpace.reset();
    d->control.reset();
    d->graph.reset();
    d->com.reset();

    std::lock_guard<std::mutex> lock(m_mutex);
    m_queue.clear();
    m_queuedBytes = 0;
    m_current.clear();
    m_currentPos = 0;
}

} // namespace tbs
