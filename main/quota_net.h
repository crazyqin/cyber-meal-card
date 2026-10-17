#pragma once
#include "quota_model.h"
typedef struct {
    bool wifi;
    bool syncing;
    bool setup;
    bool clock_ready;
    bool config_failed;
    bool connecting;
    bool builtin_cos;
    unsigned cos_missing;
    int sync_wait_seconds;
    int save_state; /* 0 idle, 1 pending, 2 saved, -1 Wi-Fi, -2 storage */
    int cos_http;
    char ap_name[32];
    char ap_password[20];
} quota_connection_t;
void quota_net_start(void);
void quota_net_read(quota_selection_t *selection, quota_view_t *view, quota_connection_t *connection);
void quota_net_refresh(void);
void quota_net_setup(void);
void quota_net_connect(void);
