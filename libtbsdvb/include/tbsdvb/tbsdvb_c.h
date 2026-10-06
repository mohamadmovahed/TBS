/* C interface to the tbsdvb library (usable from C, Python ctypes, C#, ...). */
#ifndef TBSDVB_C_H
#define TBSDVB_C_H

#include "export.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct tbs_receiver tbs_receiver;

enum { TBS_DEVICE_LINUX_DVB = 0, TBS_DEVICE_FILE_REPLAY = 1, TBS_DEVICE_WINDOWS_BDA = 2 };
enum { TBS_DELSYS_DVBS = 0, TBS_DELSYS_DVBS2 = 1 };
enum { TBS_LNB_DIRECT = 0, TBS_LNB_UNIVERSAL = 1, TBS_LNB_SINGLE = 2 };
enum { TBS_POL_OFF = 0, TBS_POL_V_13V = 1, TBS_POL_H_18V = 2 };
enum { TBS_TONE_AUTO = 0, TBS_TONE_OFF = 1, TBS_TONE_ON = 2 };
enum { TBS_PLS_ROOT = 0, TBS_PLS_GOLD = 1, TBS_PLS_COMBO = 2 };
enum { TBS_CAPTURE_TS = 0, TBS_CAPTURE_BBFRAME = 1 };
enum { TBS_BBFILE_RAW = 0, TBS_BBFILE_B8 = 1, TBS_BBFILE_PSEUDO_TS = 2 };
enum { TBS_ZMQ_PUB = 0, TBS_ZMQ_PUSH = 1 };
enum { TBS_STATE_STOPPED = 0, TBS_STATE_STARTING = 1, TBS_STATE_RUNNING = 2, TBS_STATE_STOPPING = 3 };
enum { TBS_DATA_TS = 0, TBS_DATA_BBFRAME = 1 };

typedef struct tbs_config
{
    /* device */
    int device_kind;
    int adapter, frontend, demux;  /* Linux DVB */
    int bda_index;                 /* Windows BDA (used when bda_device_path is empty) */
    char bda_device_path[512];
    char replay_file[1024];
    double replay_rate_mbps;
    int replay_loop;

    /* tuning */
    double frequency_mhz;
    double symbol_rate_ksps;
    int delivery_system;
    int lnb_type;
    double lnb_lo1_mhz, lnb_lo2_mhz, lnb_switch_mhz;
    int polarization;
    int tone;
    int diseqc_port;               /* -1 none, 0..3 */
    int input_stream_id;           /* -1 none */
    int pls_mode;
    uint32_t pls_code;

    /* capture and outputs */
    int capture_mode;
    int file_enabled;
    char file_path[1024];
    int file_timestamp_suffix;
    int bb_file_format;
    int zmq_enabled;
    char zmq_endpoint[256];
    int zmq_socket;
    int zmq_bind;
    char zmq_topic[64];
    int zmq_metadata_header;
    int zmq_ts_packets_per_message;
    int zmq_send_hwm;
} tbs_config;

typedef struct tbs_status
{
    int valid;
    uint32_t flags;      /* FE_HAS_* bits: 0x01 signal, 0x02 carrier, 0x04 FEC, 0x08 sync, 0x10 lock */
    int locked;
    int has_signal_dbm;  double signal_dbm;
    int has_signal_pct;  double signal_pct;
    int has_cnr_db;      double cnr_db;
    int has_cnr_pct;     double cnr_pct;
    int has_ber;         double ber;
    int has_error_blocks; uint64_t error_blocks;
    int has_temperature; double temperature_c;
    int delivery_system, modulation, fec, rolloff, pilot; /* linux-dvb enum values, -1 unknown */
    uint32_t frequency_khz, symbol_rate;
    int64_t stream_id;
} tbs_status;

typedef struct tbs_stats
{
    double elapsed_sec;
    uint64_t bytes_in;
    double input_mbps;
    uint64_t driver_overflows;
    uint64_t ts_packets, sync_losses, cc_errors, tei_errors;
    uint64_t bb_frames;
    double bb_frames_per_sec, bb_payload_mbps;
    uint64_t bb_crc_errors, bb_sequence_errors, bb_length_errors;
    int has_bb_header;
    uint8_t bb_matype1, bb_matype2;
    uint16_t bb_upl, bb_dfl, bb_syncd;
    uint64_t file_bytes, zmq_messages, zmq_dropped;
} tbs_stats;

typedef struct tbs_device_info
{
    int kind;
    int adapter, frontend, index;
    char name[256];
    char path[512];
} tbs_device_info;

/* Callbacks run on the receiver thread and must not block. */
typedef void (*tbs_log_cb)(void *user, const char *message);
typedef void (*tbs_state_cb)(void *user, int state, const char *error);
/* kind = TBS_DATA_TS: data holds len/188 aligned TS packets; TBS_DATA_BBFRAME: one BBFrame */
typedef void (*tbs_data_cb)(void *user, int kind, const uint8_t *data, size_t len);

TBSDVB_API const char *tbs_version(void);

TBSDVB_API void tbs_config_init(tbs_config *cfg);

/* Fills up to max entries; returns the number of devices found (may exceed max). */
TBSDVB_API int tbs_enumerate_devices(tbs_device_info *out, int max, int include_replay);

TBSDVB_API tbs_receiver *tbs_receiver_create(void);
TBSDVB_API void tbs_receiver_destroy(tbs_receiver *rx);

/* Set callbacks before tbs_receiver_start(). */
TBSDVB_API void tbs_receiver_set_log_callback(tbs_receiver *rx, tbs_log_cb cb, void *user);
TBSDVB_API void tbs_receiver_set_state_callback(tbs_receiver *rx, tbs_state_cb cb, void *user);
TBSDVB_API void tbs_receiver_set_data_callback(tbs_receiver *rx, tbs_data_cb cb, void *user);

/* Returns 0 if the capture thread was started. Open errors arrive through the state callback. */
TBSDVB_API int tbs_receiver_start(tbs_receiver *rx, const tbs_config *cfg);
TBSDVB_API void tbs_receiver_stop(tbs_receiver *rx);
TBSDVB_API int tbs_receiver_state(tbs_receiver *rx);
/* Copies the last error message; returns its length. */
TBSDVB_API size_t tbs_receiver_last_error(tbs_receiver *rx, char *buf, size_t size);
/* Retunes a running receiver using the tuning fields of cfg. */
TBSDVB_API int tbs_receiver_retune(tbs_receiver *rx, const tbs_config *cfg);
TBSDVB_API int tbs_receiver_get_status(tbs_receiver *rx, tbs_status *out);
TBSDVB_API int tbs_receiver_get_stats(tbs_receiver *rx, tbs_stats *out);

#ifdef __cplusplus
}
#endif

#endif /* TBSDVB_C_H */
