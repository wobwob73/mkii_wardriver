#include "config.h"

#include <string.h>

static BleLeafConfig g_cfg;

bool valid_ble_id(const char *s) {
    if (!s) return false;
    /* "BLE-" + one digit (BLE-1, BLE-2, ...). */
    if (strncmp(s, "BLE-", 4) != 0) return false;
    if (s[4] < '1' || s[4] > '9') return false;
    if (s[5] != '\0') return false;
    return true;
}

void cfg_init(void) {
    memset(&g_cfg, 0, sizeof(g_cfg));
    g_cfg.adopted = false;
    strcpy(g_cfg.id_str, "BLE-?");
    g_cfg.mode = BLE_MODE_SCAN;
    g_cfg.phy_mask = 3;          /* 1M + Coded */
    g_cfg.window_ms = 1000;
    g_cfg.interval_ms = 1000;
}

bool cfg_adopt(const char *leaf_id, int mode, int phy_mask,
               int window_ms, int interval_ms) {
    if (!valid_ble_id(leaf_id)) return false;
    /* This binary implements BLE scan only; BT Classic (mode 1) is a separate
     * firmware (leaf_bt_classic) and is rejected here. */
    if (mode != BLE_MODE_SCAN) return false;
    g_cfg.adopted = true;
    strncpy(g_cfg.id_str, leaf_id, sizeof(g_cfg.id_str) - 1);
    g_cfg.id_str[sizeof(g_cfg.id_str) - 1] = '\0';
    g_cfg.mode = (uint8_t)mode;
    g_cfg.phy_mask = (phy_mask == 0) ? 3 : (uint8_t)(phy_mask & 0x03);
    g_cfg.window_ms = (window_ms <= 0) ? 1000 : (uint16_t)window_ms;
    g_cfg.interval_ms = (interval_ms <= 0) ? 1000 : (uint16_t)interval_ms;
    return true;
}

void cfg_apply_defaults(void) {
    /* Standalone fallback (blebt §6): BLE-? mode 0 with default params. */
    g_cfg.adopted = true;
    strcpy(g_cfg.id_str, "BLE-?");
    g_cfg.mode = BLE_MODE_SCAN;
    g_cfg.phy_mask = 3;
    g_cfg.window_ms = 1000;
    g_cfg.interval_ms = 1000;
}

bool cfg_adopted(void)        { return g_cfg.adopted; }
const char *cfg_id_str(void)  { return g_cfg.id_str; }
uint8_t cfg_mode(void)        { return g_cfg.mode; }
uint8_t cfg_phy_mask(void)    { return g_cfg.phy_mask; }
uint16_t cfg_window_ms(void)  { return g_cfg.window_ms; }
uint16_t cfg_interval_ms(void){ return g_cfg.interval_ms; }
