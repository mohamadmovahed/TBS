#include "tbsdvb/tbsdvb_c.h"

#include "tbsdvb/Receiver.h"

#include <algorithm>
#include <cstring>
#include <mutex>

using namespace tbs;

struct tbs_receiver
{
    Receiver rx;
    tbs_log_cb logCb = nullptr;
    void *logUser = nullptr;
    tbs_state_cb stateCb = nullptr;
    void *stateUser = nullptr;
    tbs_data_cb dataCb = nullptr;
    void *dataUser = nullptr;
};

namespace {

void copyString(char *dst, size_t size, const std::string &src)
{
    if (!size)
        return;
    const size_t n = std::min(size - 1, src.size());
    std::memcpy(dst, src.data(), n);
    dst[n] = 0;
}

std::string str(const char *s, size_t max)
{
    return std::string(s, strnlen(s, max));
}

TuningParams toTuning(const tbs_config &c)
{
    TuningParams t;
    t.frequencyMHz = c.frequency_mhz;
    t.symbolRateKsps = c.symbol_rate_ksps;
    t.deliverySystem = static_cast<DeliverySystem>(c.delivery_system);
    t.lnbType = static_cast<LnbType>(c.lnb_type);
    t.lnbLo1MHz = c.lnb_lo1_mhz;
    t.lnbLo2MHz = c.lnb_lo2_mhz;
    t.lnbSwitchMHz = c.lnb_switch_mhz;
    t.polarization = static_cast<Polarization>(c.polarization);
    t.tone = static_cast<ToneMode>(c.tone);
    t.diseqcPort = c.diseqc_port;
    t.inputStreamId = c.input_stream_id;
    t.plsMode = static_cast<PlsMode>(c.pls_mode);
    t.plsCode = c.pls_code;
    return t;
}

ReceiverConfig toConfig(const tbs_config &c)
{
    ReceiverConfig r;
    r.device.kind = static_cast<DeviceKind>(c.device_kind);
    r.device.adapter = c.adapter;
    r.device.frontend = c.frontend;
    r.device.demux = c.demux;
    r.device.bdaIndex = c.bda_index;
    r.device.bdaDevicePath = str(c.bda_device_path, sizeof(c.bda_device_path));
    r.device.replayFile = str(c.replay_file, sizeof(c.replay_file));
    r.device.replayRateMbps = c.replay_rate_mbps;
    r.device.replayLoop = c.replay_loop != 0;
    r.tuning = toTuning(c);
    r.capture = static_cast<CaptureMode>(c.capture_mode);
    r.file.enabled = c.file_enabled != 0;
    r.file.path = str(c.file_path, sizeof(c.file_path));
    r.file.timestampSuffix = c.file_timestamp_suffix != 0;
    r.file.bbFormat = static_cast<BbFileFormat>(c.bb_file_format);
    r.zmq.enabled = c.zmq_enabled != 0;
    r.zmq.endpoint = str(c.zmq_endpoint, sizeof(c.zmq_endpoint));
    r.zmq.socket = static_cast<ZmqSocketKind>(c.zmq_socket);
    r.zmq.bind = c.zmq_bind != 0;
    r.zmq.topic = str(c.zmq_topic, sizeof(c.zmq_topic));
    r.zmq.metadataHeader = c.zmq_metadata_header != 0;
    r.zmq.tsPacketsPerMessage = c.zmq_ts_packets_per_message;
    r.zmq.sendHwm = c.zmq_send_hwm;
    return r;
}

} // namespace

extern "C" {

const char *tbs_version(void)
{
    return tbs::version();
}

void tbs_config_init(tbs_config *cfg)
{
    if (!cfg)
        return;
    std::memset(cfg, 0, sizeof(*cfg));
    const ReceiverConfig d;
    cfg->device_kind = static_cast<int>(d.device.kind);
    cfg->replay_loop = d.device.replayLoop;
    cfg->frequency_mhz = d.tuning.frequencyMHz;
    cfg->symbol_rate_ksps = d.tuning.symbolRateKsps;
    cfg->delivery_system = static_cast<int>(d.tuning.deliverySystem);
    cfg->lnb_type = static_cast<int>(d.tuning.lnbType);
    cfg->lnb_lo1_mhz = d.tuning.lnbLo1MHz;
    cfg->lnb_lo2_mhz = d.tuning.lnbLo2MHz;
    cfg->lnb_switch_mhz = d.tuning.lnbSwitchMHz;
    cfg->polarization = static_cast<int>(d.tuning.polarization);
    cfg->tone = static_cast<int>(d.tuning.tone);
    cfg->diseqc_port = d.tuning.diseqcPort;
    cfg->input_stream_id = d.tuning.inputStreamId;
    cfg->pls_mode = static_cast<int>(d.tuning.plsMode);
    cfg->pls_code = d.tuning.plsCode;
    cfg->capture_mode = static_cast<int>(d.capture);
    cfg->file_timestamp_suffix = d.file.timestampSuffix;
    copyString(cfg->zmq_endpoint, sizeof(cfg->zmq_endpoint), d.zmq.endpoint);
    cfg->zmq_bind = d.zmq.bind;
    cfg->zmq_ts_packets_per_message = d.zmq.tsPacketsPerMessage;
    cfg->zmq_send_hwm = d.zmq.sendHwm;
}

int tbs_enumerate_devices(tbs_device_info *out, int max, int include_replay)
{
    const auto list = Devices::enumerate(include_replay != 0);
    for (int i = 0; out && i < max && i < static_cast<int>(list.size()); ++i) {
        const DeviceInfo &d = list[static_cast<size_t>(i)];
        out[i].kind = static_cast<int>(d.kind);
        out[i].adapter = d.adapter;
        out[i].frontend = d.frontend;
        out[i].index = d.index;
        copyString(out[i].name, sizeof(out[i].name), d.name);
        copyString(out[i].path, sizeof(out[i].path), d.path);
    }
    return static_cast<int>(list.size());
}

tbs_receiver *tbs_receiver_create(void)
{
    return new tbs_receiver;
}

void tbs_receiver_destroy(tbs_receiver *rx)
{
    delete rx;
}

void tbs_receiver_set_log_callback(tbs_receiver *rx, tbs_log_cb cb, void *user)
{
    rx->logCb = cb;
    rx->logUser = user;
}

void tbs_receiver_set_state_callback(tbs_receiver *rx, tbs_state_cb cb, void *user)
{
    rx->stateCb = cb;
    rx->stateUser = user;
}

void tbs_receiver_set_data_callback(tbs_receiver *rx, tbs_data_cb cb, void *user)
{
    rx->dataCb = cb;
    rx->dataUser = user;
}

int tbs_receiver_start(tbs_receiver *rx, const tbs_config *cfg)
{
    if (!rx || !cfg)
        return -1;
    Receiver::Callbacks cb;
    if (rx->logCb)
        cb.onLog = [rx](const std::string &m) { rx->logCb(rx->logUser, m.c_str()); };
    if (rx->stateCb)
        cb.onStateChanged = [rx](ReceiverState s, const std::string &e) { rx->stateCb(rx->stateUser, static_cast<int>(s), e.c_str()); };
    if (rx->dataCb) {
        if (cfg->capture_mode == TBS_CAPTURE_BBFRAME)
            cb.onBbFrame = [rx](const uint8_t *f, size_t n, const BbHeader &) { rx->dataCb(rx->dataUser, TBS_DATA_BBFRAME, f, n); };
        else
            cb.onTsPackets = [rx](const uint8_t *p, size_t n) { rx->dataCb(rx->dataUser, TBS_DATA_TS, p, n * 188); };
    }
    rx->rx.setCallbacks(cb);
    return rx->rx.start(toConfig(*cfg)) ? 0 : -1;
}

void tbs_receiver_stop(tbs_receiver *rx)
{
    if (rx)
        rx->rx.stop();
}

int tbs_receiver_state(tbs_receiver *rx)
{
    return rx ? static_cast<int>(rx->rx.state()) : TBS_STATE_STOPPED;
}

size_t tbs_receiver_last_error(tbs_receiver *rx, char *buf, size_t size)
{
    const std::string e = rx ? rx->rx.lastError() : std::string();
    if (buf)
        copyString(buf, size, e);
    return e.size();
}

int tbs_receiver_retune(tbs_receiver *rx, const tbs_config *cfg)
{
    if (!rx || !cfg)
        return -1;
    rx->rx.retune(toTuning(*cfg));
    return 0;
}

int tbs_receiver_get_status(tbs_receiver *rx, tbs_status *out)
{
    if (!rx || !out)
        return -1;
    const FrontendStatus s = rx->rx.status();
    std::memset(out, 0, sizeof(*out));
    out->valid = s.valid;
    out->flags = s.flags;
    out->locked = s.locked();
    out->has_signal_dbm = s.hasSignalDbm;
    out->signal_dbm = s.signalDbm;
    out->has_signal_pct = s.hasSignalPercent;
    out->signal_pct = s.signalPercent;
    out->has_cnr_db = s.hasCnrDb;
    out->cnr_db = s.cnrDb;
    out->has_cnr_pct = s.hasCnrPercent;
    out->cnr_pct = s.cnrPercent;
    out->has_ber = s.hasBer;
    out->ber = s.ber;
    out->has_error_blocks = s.hasErrorBlocks;
    out->error_blocks = s.errorBlocks;
    out->has_temperature = s.hasTemperature;
    out->temperature_c = s.temperatureC;
    out->delivery_system = s.deliverySystem;
    out->modulation = s.modulation;
    out->fec = s.fec;
    out->rolloff = s.rolloff;
    out->pilot = s.pilot;
    out->frequency_khz = s.frequencyKHz;
    out->symbol_rate = s.symbolRate;
    out->stream_id = s.streamId;
    return 0;
}

int tbs_receiver_get_stats(tbs_receiver *rx, tbs_stats *out)
{
    if (!rx || !out)
        return -1;
    const StreamStats s = rx->rx.stats();
    std::memset(out, 0, sizeof(*out));
    out->elapsed_sec = s.elapsedSec;
    out->bytes_in = s.bytesIn;
    out->input_mbps = s.inputMbps;
    out->driver_overflows = s.driverOverflows;
    out->ts_packets = s.tsPackets;
    out->sync_losses = s.syncLosses;
    out->cc_errors = s.ccErrors;
    out->tei_errors = s.teiErrors;
    out->bb_frames = s.bbFrames;
    out->bb_frames_per_sec = s.bbFramesPerSec;
    out->bb_payload_mbps = s.bbPayloadMbps;
    out->bb_crc_errors = s.bbCrcErrors;
    out->bb_sequence_errors = s.bbSequenceErrors;
    out->bb_length_errors = s.bbLengthErrors;
    out->has_bb_header = s.hasBbHeader;
    out->bb_matype1 = s.lastBbHeader.matype1;
    out->bb_matype2 = s.lastBbHeader.matype2;
    out->bb_upl = s.lastBbHeader.upl;
    out->bb_dfl = s.lastBbHeader.dfl;
    out->bb_syncd = s.lastBbHeader.syncd;
    out->file_bytes = s.fileBytes;
    out->zmq_messages = s.zmqMessages;
    out->zmq_dropped = s.zmqDropped;
    return 0;
}

} // extern "C"
