#pragma once
#include "quota_discovery.h"
quota_status_t quota_collection_apply(quota_snapshot_t *snapshot, const cJSON *root, const char *collector);
