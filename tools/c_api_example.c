/* Minimal use of the tbsdvb C API: replay (or tune) and count data units.
 *
 *   tbs-c-example replay.ts [bb]
 */
#include "tbsdvb/tbsdvb_c.h"

#include <stdio.h>
#include <string.h>

#if defined(_WIN32)
#include <windows.h>
static void sleep_ms(int ms) { Sleep((DWORD)ms); }
#else
#include <time.h>
static void sleep_ms(int ms)
{
    struct timespec ts = {ms / 1000, (long)(ms % 1000) * 1000000L};
    nanosleep(&ts, NULL);
}
#endif

static unsigned long long g_units = 0;
static unsigned long long g_bytes = 0;

static void on_data(void *user, int kind, const uint8_t *data, size_t len)
{
    (void)user;
    (void)kind;
    (void)data;
    ++g_units;
    g_bytes += len;
}

static void on_log(void *user, const char *msg)
{
    (void)user;
    fprintf(stderr, "log: %s\n", msg);
}

int main(int argc, char **argv)
{
    tbs_config cfg;
    tbs_device_info devices[16];
    int n, i;
    tbs_receiver *rx;
    tbs_stats stats;
    char err[256];

    printf("tbsdvb %s\n", tbs_version());
    n = tbs_enumerate_devices(devices, 16, 0);
    for (i = 0; i < n && i < 16; ++i)
        printf("device %d: %s (%s)\n", i, devices[i].name, devices[i].path);

    tbs_config_init(&cfg);
    if (argc > 1) {
        cfg.device_kind = TBS_DEVICE_FILE_REPLAY;
        strncpy(cfg.replay_file, argv[1], sizeof(cfg.replay_file) - 1);
        cfg.replay_loop = 0;
    }
    if (argc > 2 && strcmp(argv[2], "bb") == 0)
        cfg.capture_mode = TBS_CAPTURE_BBFRAME;

    rx = tbs_receiver_create();
    tbs_receiver_set_log_callback(rx, on_log, NULL);
    tbs_receiver_set_data_callback(rx, on_data, NULL);
    if (tbs_receiver_start(rx, &cfg) != 0) {
        fprintf(stderr, "start failed\n");
        return 1;
    }
    for (i = 0; i < 300 && tbs_receiver_state(rx) != TBS_STATE_STOPPED; ++i)
        sleep_ms(20);
    tbs_receiver_get_stats(rx, &stats);
    tbs_receiver_stop(rx);

    if (tbs_receiver_last_error(rx, err, sizeof(err)))
        fprintf(stderr, "error: %s\n", err);
    printf("units %llu, bytes %llu, ts packets %llu, bbframes %llu\n", g_units, g_bytes,
           (unsigned long long)stats.ts_packets, (unsigned long long)stats.bb_frames);
    tbs_receiver_destroy(rx);
    return 0;
}
