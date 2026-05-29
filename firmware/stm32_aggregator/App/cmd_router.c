#include "cmd_router.h"
#include "proto.h"
#include "pal.h"

#include <stdio.h>
#include <string.h>

#if MKII_STM32_UNIT == 1
const char *const branch_names[N_BRANCH_UARTS] = {
    "W24", "W5G", "BLE", "DOT", "SEN"
};
int cmd_router_target_for_leaf(const char *leaf_id) {
    if (!leaf_id) return -1;
    if (leaf_id[0] == 'W' && leaf_id[1] >= '1' && leaf_id[1] <= '4' && leaf_id[2] == '\0') {
        return BRANCH_IDX_W24;
    }
    if (strncmp(leaf_id, "W5_", 3) == 0) return BRANCH_IDX_W5G;
    if (strncmp(leaf_id, "BLE-", 4) == 0 || strncmp(leaf_id, "BT-", 3) == 0) {
        return BRANCH_IDX_BLE;
    }
    if (strncmp(leaf_id, "DOT-", 4) == 0) return BRANCH_IDX_DOT154;
    if (strcmp(leaf_id, "SEN") == 0) return BRANCH_IDX_SEN;
    return -1;
}
#else
const char *const branch_names[N_BRANCH_UARTS] = {
    "MTC", "VHF", "UHF", "FPV"
};
int cmd_router_target_for_leaf(const char *leaf_id) {
    if (!leaf_id) return -1;
    if (strncmp(leaf_id, "MESH-", 5) == 0 || strncmp(leaf_id, "MCORE-", 6) == 0) {
        return BRANCH_IDX_MTC;
    }
    if (strncmp(leaf_id, "VHF-", 4) == 0) return BRANCH_IDX_VHF;
    if (strncmp(leaf_id, "UHF-", 4) == 0) return BRANCH_IDX_UHF;
    if (strncmp(leaf_id, "FPV-", 4) == 0) return BRANCH_IDX_FPV;
    return -1;
}
#endif

bool cmd_router_handle_usb_line(const char *line, size_t len) {
    if (!line || len < 5) return false;
    char copy[MAX_LINE_LEN + 1];
    if (len > MAX_LINE_LEN) return false;
    memcpy(copy, line, len);
    copy[len] = '\0';

    char *star = NULL;
    for (size_t i = 1; i < len; i++) {
        if (copy[i] == '*') { star = copy + i; break; }
    }
    if (!star) return false;
    *star = '\0';

    char *fields[16];
    int n = proto_split_body(copy + 1, fields, 16);
    if (n < 1) return false;

    const char *type = fields[0];
    if (strcmp(type, "RC") == 0) {
        if (n < 3) return false;
        int target = cmd_router_target_for_leaf(fields[1]);
        if (target < 0) return false;
        size_t pre_star = (size_t)(star - copy);
        char relayed[MAX_LINE_LEN + 2];
        if (len + 2 > sizeof(relayed)) return false;
        memcpy(relayed, line, len);
        if (len < sizeof(relayed) - 1) {
            relayed[len]     = '\n';
            relayed[len + 1] = '\0';
        }
        (void)pre_star;
        pal_branch_tx_write_str((uint8_t)target, relayed);
        return true;
    } else if (strcmp(type, "RQ") == 0) {
        if (n < 2) return false;
        for (uint8_t b = 0; b < N_BRANCH_UARTS; b++) {
            if (strcmp(fields[1], branch_names[b]) == 0) {
                char line2[64];
                snprintf(line2, sizeof(line2), "$RQ,%s", branch_names[b]);
                if (!proto_finalize_line(line2, sizeof(line2))) return false;
                pal_branch_tx_write_str(b, line2);
                return true;
            }
        }
        return false;
    }
    return false;
}
