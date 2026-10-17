#pragma once
#include <stdbool.h>
#include <stdint.h>

#define QUOTA_MAX_CODEX 32
#define QUOTA_BASE_PROVIDERS 3
#define QUOTA_PROVIDERS (QUOTA_BASE_PROVIDERS + QUOTA_MAX_CODEX)
#define QUOTA_WINDOWS 3
#define QUOTA_BALANCES 2
typedef enum { QUOTA_CLINE, QUOTA_DEEPSEEK, QUOTA_TOKENHUB, QUOTA_CODEX, QUOTA_GENERIC, QUOTA_POINTS } quota_provider_id_t;
typedef enum {
    QUOTA_UNCONFIGURED, QUOTA_OK, QUOTA_AUTH, QUOTA_NETWORK,
    QUOTA_UNSUPPORTED, QUOTA_THROTTLED, QUOTA_INVALID, QUOTA_STORAGE,
    QUOTA_BLOCKED, QUOTA_COLLECTOR, QUOTA_ACCOUNT_MISSING, QUOTA_EMPTY, QUOTA_LIMIT
} quota_status_t;
typedef struct {
    int minutes;
    float remaining;
    int64_t resets_at;
} quota_window_t;
typedef struct {
    char currency[4];
    char total[24];
    char granted[24];
    char topped_up[24];
} quota_balance_t;
typedef struct {
    char balance[24], spent_today[24], added_today[24];
} quota_points_t;
typedef struct {
    quota_status_t status;
    int64_t updated_at;
    char plan[32];
    char account[65];
    char collector[65];
    /* V2 collector identities are independent of display alias and computer. */
    char source[33], account_id[65], label[24];
    quota_provider_id_t kind;
    int reset_count; /* -1 means unavailable, never an invented zero. */
    int64_t latest_reset_expires_at, next_reset_expires_at;
    int window_count;
    quota_window_t windows[QUOTA_WINDOWS];
    int balance_count;
    /* Mutually exclusive display shapes share storage on the no-PSRAM board. */
    union {
        quota_balance_t balances[QUOTA_BALANCES];
        quota_points_t points;
    };
    bool available;
} quota_provider_t;
typedef struct {
    quota_provider_t providers[QUOTA_PROVIDERS];
    int codex_count;
    quota_status_t discovery_status;
    bool discovery_limited;
    bool unified_mode;
} quota_snapshot_t;
typedef struct { int index; char collector[65], account[65]; } quota_selection_t;
typedef struct {
    quota_provider_t provider;
    int index, count;
    quota_status_t discovery_status;
    bool discovery_limited;
} quota_view_t;

bool quota_remaining(double used, float *remaining);
int quota_next_provider(int current, int direction, int count);
bool quota_rotation_due(int64_t now_us, int64_t rotated_us, int64_t hold_until_us);
void quota_select(const quota_snapshot_t *snapshot, quota_selection_t *selection, quota_view_t *view);
int quota_sync_wait_seconds(int64_t now_ms, int64_t last_ms, bool manual);
bool quota_is_stale(const quota_provider_t *provider, int64_t now);
void quota_percent_label(float remaining, char *out, unsigned size);
void quota_window_label(int minutes, char *out, unsigned size);
void quota_reset_label(int64_t resets_at, int64_t now, char *out, unsigned size);
void quota_window_detail_label(const quota_window_t *window, int64_t now, char *out, unsigned size);
