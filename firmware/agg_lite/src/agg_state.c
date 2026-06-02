#include "agg_state.h"

#include "hardware/sync.h"

static volatile bool g_init_leaves = false;

void agg_request_leaf_init(void) {
    g_init_leaves = true;
    __dmb();
}

bool agg_leaf_init_requested(void) {
    bool v = g_init_leaves;
    __dmb();
    return v;
}
