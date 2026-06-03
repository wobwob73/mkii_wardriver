#include "sd_log.h"
#include "pal.h"

#include <stdio.h>
#include <string.h>

static bool     g_mounted;
static char     g_session[24];
static char     g_branch_path[96];
static char     g_gps_path[96];
static char     g_events_path[96];
static void *   g_branch_fh;
static void *   g_gps_fh;
static void *   g_events_fh;
static uint32_t g_bytes;
static uint32_t g_last_flush_ms;

void sd_log_init(void) {
    g_mounted = false;
    g_session[0] = '\0';
    g_branch_fh = NULL;
    g_gps_fh = NULL;
    g_events_fh = NULL;
    g_bytes = 0;
    g_last_flush_ms = 0;

    if (!pal_sd_present()) return;
    if (pal_sd_mount() != 0) return;

    g_mounted = true;
    sd_log_set_session("pending");
}

bool sd_log_ok(void) {
    return g_mounted;
}

static void close_files(void) {
    if (g_branch_fh) { pal_sd_close(g_branch_fh); g_branch_fh = NULL; }
    if (g_gps_fh) { pal_sd_close(g_gps_fh); g_gps_fh = NULL; }
    if (g_events_fh) { pal_sd_close(g_events_fh); g_events_fh = NULL; }
}

void sd_log_set_session(const char *session_id) {
    if (!session_id) return;
    if (!g_mounted) return;

    strncpy(g_session, session_id, sizeof(g_session) - 1);
    g_session[sizeof(g_session) - 1] = '\0';

    close_files();

    char dir[64];
    snprintf(dir, sizeof(dir), "/MKII/%s/%s", UNIT_ID_STR, g_session);
    pal_sd_mkdir_p(dir);

    snprintf(g_branch_path, sizeof(g_branch_path), "%s/branch.log", dir);
    snprintf(g_gps_path, sizeof(g_gps_path), "%s/gps.log", dir);
    snprintf(g_events_path, sizeof(g_events_path), "%s/events.log", dir);

    pal_sd_open_append(g_branch_path, &g_branch_fh);
    pal_sd_open_append(g_gps_path, &g_gps_fh);
    pal_sd_open_append(g_events_path, &g_events_fh);
}

int sd_log_write_line(const char *line, size_t len) {
    if (!g_mounted || !g_branch_fh) return -1;
    int n = pal_sd_write(g_branch_fh, (const uint8_t *)line, len);
    if (n > 0) g_bytes += (uint32_t)n;
    return n;
}

int sd_log_write_gps_snapshot(const char *line, size_t len) {
    if (!g_mounted || !g_gps_fh) return -1;
    int n = pal_sd_write(g_gps_fh, (const uint8_t *)line, len);
    if (n > 0) g_bytes += (uint32_t)n;
    return n;
}

int sd_log_write_event(const char *line, size_t len) {
    if (!g_mounted || !g_events_fh) return -1;
    int n = pal_sd_write(g_events_fh, (const uint8_t *)line, len);
    if (n > 0) g_bytes += (uint32_t)n;
    return n;
}

void sd_log_tick(void) {
    if (!g_mounted) return;
    uint32_t now = pal_time_ms();
    if (now - g_last_flush_ms >= SD_FLUSH_INTERVAL_MS) {
        if (g_branch_fh) pal_sd_flush(g_branch_fh);
        if (g_gps_fh) pal_sd_flush(g_gps_fh);
        if (g_events_fh) pal_sd_flush(g_events_fh);
        g_last_flush_ms = now;
    }
}

uint32_t sd_log_bytes_written(void) { return g_bytes; }
