#include "sd_log.h"
#include "sd_spi_fatfs.h"

#include "pico/stdlib.h"
#include "pico/time.h"

#include <string.h>
#include <stdio.h>

#define SD_FLUSH_INTERVAL_MS  1000
#define SD_DRAIN_CHUNK        512

/* Byte ring (single-producer/single-consumer, both on Core 1). */
static uint8_t  g_ring[SD_RING_BYTES];
static uint32_t g_head;      /* write position */
static uint32_t g_tail;      /* drain position */
static uint16_t g_high_water;

static bool     g_mounted;
static bool     g_open;
static bool     g_named;
static char     g_session[24];
static uint64_t g_bytes;
static uint32_t g_drops;
static uint32_t g_errors;
static uint32_t g_last_flush_ms;
static uint32_t g_last_mount_try_ms;

/* Modular ring with one slot reserved to disambiguate full/empty. */
static uint32_t ring_count(void) {
    return (g_head + SD_RING_BYTES - g_tail) % SD_RING_BYTES;
}
static uint32_t ring_free(void) {
    return SD_RING_BYTES - 1 - ring_count();
}

static void open_session_dir(const char *id) {
    char dir[40];
    char path[64];
    snprintf(dir, sizeof(dir), "/%s", id);
    sd_be_mkdir_p(dir);
    snprintf(path, sizeof(path), "/%s/records.log", id);
    g_open = sd_be_open_append(path);
}

void sd_log_init(void) {
    g_head = g_tail = 0;
    g_high_water = 0;
    g_mounted = false;
    g_open = false;
    g_named = false;
    g_session[0] = '\0';
    g_bytes = 0;
    g_drops = 0;
    g_errors = 0;
    g_last_flush_ms = to_ms_since_boot(get_absolute_time());
    g_last_mount_try_ms = g_last_flush_ms;

    sd_be_init();
    if (!sd_be_present()) return;
    if (!sd_be_mount()) return;

    g_mounted = true;
    strncpy(g_session, "pending", sizeof(g_session) - 1);
    g_session[sizeof(g_session) - 1] = '\0';
    open_session_dir(g_session);
}

bool sd_log_mounted(void) { return g_mounted; }
bool sd_log_has_session(void) { return g_named; }

void sd_log_set_session(const char *session_id) {
    if (!session_id || !g_mounted) return;

    /* Rename the pending bucket so already-logged records carry over (§8.1). */
    char from[40], to[40];
    snprintf(from, sizeof(from), "/%s", g_session);
    snprintf(to, sizeof(to), "/%s", session_id);

    sd_be_close();
    g_open = false;

    if (strcmp(g_session, session_id) != 0) {
        if (!sd_be_rename(from, to)) {
            /* Rename can fail if the target exists; fall back to a fresh dir. */
            sd_be_mkdir_p(to);
        }
    }
    strncpy(g_session, session_id, sizeof(g_session) - 1);
    g_session[sizeof(g_session) - 1] = '\0';
    g_named = true;

    char path[64];
    snprintf(path, sizeof(path), "/%s/records.log", g_session);
    g_open = sd_be_open_append(path);
}

void sd_log_enqueue(const char *line, size_t len) {
    if (!line || len == 0) return;
    if (ring_free() < len) {
        g_drops++;          /* burst exceeded the ring; surfaced in $LA.err_count */
        return;
    }
    for (size_t i = 0; i < len; i++) {
        g_ring[g_head] = (uint8_t)line[i];
        g_head = (g_head + 1) % SD_RING_BYTES;
    }
    uint32_t used = ring_count();
    if (used > g_high_water) g_high_water = (uint16_t)(used > 0xFFFF ? 0xFFFF : used);
}

static void try_remount(uint32_t now) {
    if (g_mounted) return;
    if (now - g_last_mount_try_ms < SD_MOUNT_RETRY_MS) return;
    g_last_mount_try_ms = now;
    if (!sd_be_present()) return;
    if (!sd_be_mount()) return;
    g_mounted = true;
    /* Resume into whatever session we are on (pending or fix-stamped). */
    open_session_dir(g_session[0] ? g_session : "pending");
    if (!g_session[0]) {
        strncpy(g_session, "pending", sizeof(g_session) - 1);
    }
}

void sd_log_tick(void) {
    uint32_t now = to_ms_since_boot(get_absolute_time());

    if (!g_mounted) { try_remount(now); return; }
    if (!g_open) return;

    /* Drain the ring in block-sized contiguous chunks. */
    uint32_t drained_any = 0;
    while (ring_count() > 0) {
        uint32_t contig = (g_head >= g_tail) ? (g_head - g_tail)
                                             : (SD_RING_BYTES - g_tail);
        if (contig > SD_DRAIN_CHUNK) contig = SD_DRAIN_CHUNK;
        int wrote = sd_be_write(&g_ring[g_tail], contig);
        if (wrote < 0) {
            /* FR_DISK_ERR / hot-remove: reopen and retry next tick (§8.2). */
            g_errors++;
            sd_be_close();
            g_open = false;
            char path[64];
            snprintf(path, sizeof(path), "/%s/records.log",
                     g_session[0] ? g_session : "pending");
            g_open = sd_be_open_append(path);
            if (!g_open) { g_mounted = sd_be_ok(); }
            break;
        }
        g_tail = (g_tail + (uint32_t)wrote) % SD_RING_BYTES;
        g_bytes += (uint32_t)wrote;
        drained_any += (uint32_t)wrote;
        if ((uint32_t)wrote < contig) break;   /* backend backpressure */
    }

    if (drained_any && (now - g_last_flush_ms >= SD_FLUSH_INTERVAL_MS)) {
        sd_be_sync();
        g_last_flush_ms = now;
    }
    g_mounted = sd_be_ok() || g_mounted;   /* stay mounted unless backend cleared */
}

uint32_t sd_log_kb_written(void)   { return (uint32_t)(g_bytes / 1024ULL); }
uint32_t sd_log_drop_count(void)   { return g_drops; }
uint32_t sd_log_error_count(void)  { return g_errors; }
uint16_t sd_log_ring_high_water(void) { return g_high_water; }
