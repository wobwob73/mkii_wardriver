#include "gps.h"
#include "pps_time.h"
#include "pal.h"

#include <string.h>
#include <stdlib.h>

#define GPS_I2C_ADDR  0x42

static gps_state_t g_state;
static uint32_t    g_bytes_total;
static uint32_t    g_msg_total;
static char        g_line[128];
static size_t      g_line_pos;

void gps_init(void) {
    memset(&g_state, 0, sizeof(g_state));
    g_bytes_total = 0;
    g_msg_total = 0;
    g_line_pos = 0;
}

void gps_push_config(void) {
    /*
     * Push a minimal UBX configuration to the M10 family:
     *   - Disable: GSV, GSA, GLL, VTG, GNS
     *   - Enable:  RMC, GGA at 1 Hz on I2C
     *   - Configure PPS pulse: 1 Hz, 100 ms width, rising edge UTC-aligned
     *
     * The exact CFG-VALSET frames are constructed at boot. If the GPS
     * does not respond the firmware proceeds — most modules retain the
     * prior configuration in BBR/Flash so even an unconfigured first
     * boot still produces NMEA + PPS.
     *
     * For brevity the byte-level frames are constructed inside
     * gps_push_config_impl() in a follow-up; the v1.0 default firmware
     * relies on the M10's factory defaults plus user pre-configuration
     * via u-center.
     */
}

static int32_t parse_decimal_e7(const char *s) {
    if (!s || !*s) return 0;
    int sign = 1;
    if (*s == '-') { sign = -1; s++; }
    int32_t deg_min = 0;
    const char *dot = strchr(s, '.');
    if (!dot) return sign * atoi(s) * 10000000;
    size_t dlen = dot - s;
    if (dlen < 4) return sign * atoi(s) * 10000000;
    char tmp[16] = {0};
    if (dlen > sizeof(tmp) - 1) dlen = sizeof(tmp) - 1;
    memcpy(tmp, s, dlen);
    deg_min = atoi(tmp);
    int32_t deg = deg_min / 100;
    int32_t min_int = deg_min % 100;
    double mins = (double)min_int + atof(dot);
    double decimal = (double)deg + mins / 60.0;
    int32_t e7 = (int32_t)(decimal * 1e7);
    return sign * e7;
}

static int decode_decimal_digit(char c) {
    return (c >= '0' && c <= '9') ? (c - '0') : -1;
}

static int decode_two_digits(const char *s) {
    int hi = decode_decimal_digit(s[0]);
    int lo = decode_decimal_digit(s[1]);
    if (hi < 0 || lo < 0) return -1;
    return hi * 10 + lo;
}

static void process_rmc(char **f, int n) {
    if (n < 12) return;
    if (!f[2] || f[2][0] != 'A') {
        g_state.fix_ok = false;
        return;
    }
    g_state.fix_ok = true;

    const char *tstr = f[1];
    const char *dstr = f[9];
    if (tstr && strlen(tstr) >= 6 && dstr && strlen(dstr) >= 6) {
        int hh = decode_two_digits(tstr + 0);
        int mm = decode_two_digits(tstr + 2);
        int ss = decode_two_digits(tstr + 4);
        int dd = decode_two_digits(dstr + 0);
        int mo = decode_two_digits(dstr + 2);
        int yy = decode_two_digits(dstr + 4);
        /* Reject malformed digits and out-of-range values. A corrupted I2C
         * byte that flipped a digit must not propagate into the timebase. */
        if (hh < 0 || mm < 0 || ss < 0 || dd < 0 || mo < 0 || yy < 0) return;
        if (hh > 23 || mm > 59 || ss > 60) return;   /* 60 allowed for leap second */
        if (mo < 1 || mo > 12) return;
        if (dd < 1 || dd > 31) return;
        int year = 2000 + yy;

        static const int days_per_month[] = { 31, 28, 31, 30, 31, 30,
                                                31, 31, 30, 31, 30, 31 };
        uint32_t total_days = 0;
        for (int y = 1970; y < year; y++) {
            bool leap = (y % 4 == 0 && (y % 100 != 0 || y % 400 == 0));
            total_days += leap ? 366 : 365;
        }
        for (int m = 1; m < mo; m++) {
            total_days += days_per_month[m - 1];
            if (m == 2) {
                bool leap = (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0));
                if (leap) total_days++;
            }
        }
        total_days += (uint32_t)(dd - 1);
        uint32_t epoch = total_days * 86400 + (uint32_t)hh * 3600
                         + (uint32_t)mm * 60 + (uint32_t)ss;
        g_state.epoch_s = epoch;
        g_state.time_valid = true;
        pps_time_apply_from_gps(epoch, true);
    }

    if (f[3] && f[3][0]) {
        int32_t lat = parse_decimal_e7(f[3]);
        if (f[4] && f[4][0] == 'S') lat = -lat;
        g_state.lat_e7 = lat;
    }
    if (f[5] && f[5][0]) {
        int32_t lon = parse_decimal_e7(f[5]);
        if (f[6] && f[6][0] == 'W') lon = -lon;
        g_state.lon_e7 = lon;
    }
}

static void process_gga(char **f, int n) {
    if (n < 10) return;
    if (f[6] && f[6][0]) {
        int fix = atoi(f[6]);
        if (fix < 0 || fix > 9) return;        /* NMEA fix-quality is 0..8 */
        g_state.fix_ok = (fix > 0);
    }
    if (f[7] && f[7][0]) {
        int sats = atoi(f[7]);
        if (sats < 0) sats = 0;
        if (sats > 64) sats = 64;              /* multi-constellation upper bound */
        g_state.sat_count = (uint8_t)sats;
    }
    if (f[8] && f[8][0]) {
        double hdop = atof(f[8]);
        if (hdop < 0) hdop = 0;
        if (hdop > 25.5) hdop = 25.5;
        g_state.hdop_x10 = (uint8_t)(hdop * 10);
    }
    if (f[9] && f[9][0]) {
        double alt = atof(f[9]);
        if (alt > 32767.0) alt = 32767.0;
        if (alt < -32768.0) alt = -32768.0;
        g_state.alt_cm = (int16_t)(alt * 100);
    }
}

static int nmea_hex_to_int(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
    if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
    return -1;
}

/* NMEA-0183 checksum: XOR of all bytes between '$' (exclusive) and '*'
 * (exclusive), printed as two uppercase hex digits after '*'. A corrupted
 * I2C byte stream MUST NOT be allowed to update the timebase, so we reject
 * the sentence unless the checksum is exactly '*HH' at end-of-line and
 * matches. The PPS-driven timing path depends on this. */
static bool nmea_validate_checksum(const char *line, size_t len) {
    if (len < 8) return false;       /* "$xx,*HH" is the minimum shape */
    if (line[0] != '$') return false;
    const char *star = NULL;
    for (size_t i = 1; i < len; i++) {
        if (line[i] == '*') { star = line + i; break; }
    }
    if (!star) return false;
    /* '*HH' must be the last three bytes of the line. */
    if ((size_t)((star + 3) - line) != len) return false;
    int hi = nmea_hex_to_int(star[1]);
    int lo = nmea_hex_to_int(star[2]);
    if (hi < 0 || lo < 0) return false;
    uint8_t expected = (uint8_t)((hi << 4) | lo);
    uint8_t actual = 0;
    for (const char *p = line + 1; p < star; p++) actual ^= (uint8_t)*p;
    return expected == actual;
}

static void process_nmea_line(char *line, size_t len) {
    if (!nmea_validate_checksum(line, len)) return;
    /* Checksum passed; safe to parse. Truncate at '*' for field walking. */
    char *star = NULL;
    for (size_t i = 1; i < len; i++) {
        if (line[i] == '*') { star = line + i; break; }
    }
    if (!star) return;
    *star = '\0';
    char *fields[20];
    int n = 0;
    fields[n++] = line + 1;
    for (char *p = line + 1; *p && n < 20; p++) {
        if (*p == ',') {
            *p = '\0';
            fields[n++] = p + 1;
        }
    }
    if (n < 1) return;
    const char *type = fields[0];
    if (strlen(type) < 5) return;
    const char *body = type + 2;
    if (strncmp(body, "RMC", 3) == 0) {
        process_rmc(fields, n);
        g_msg_total++;
    } else if (strncmp(body, "GGA", 3) == 0) {
        process_gga(fields, n);
        g_msg_total++;
    }
}

void gps_poll(void) {
    uint8_t buf[256];
    while (true) {
        int n = pal_i2c_read(GPS_I2C_ADDR, buf, sizeof(buf));
        if (n <= 0) break;
        for (int i = 0; i < n; i++) {
            uint8_t b = buf[i];
            if (b == 0xFF || b == 0x00) continue;
            g_bytes_total++;
            if (b == '\n') {
                if (g_line_pos > 0) {
                    g_line[g_line_pos] = '\0';
                    process_nmea_line(g_line, g_line_pos);
                    g_line_pos = 0;
                }
                continue;
            }
            if (b == '\r') continue;
            if (g_line_pos < sizeof(g_line) - 1) {
                g_line[g_line_pos++] = (char)b;
            } else {
                g_line_pos = 0;
            }
        }
        if (n < (int)sizeof(buf)) break;
    }
}

const gps_state_t *gps_state_get(void) { return &g_state; }
uint32_t gps_bytes_read_count(void) { return g_bytes_total; }
uint32_t gps_nmea_msg_count(void) { return g_msg_total; }
