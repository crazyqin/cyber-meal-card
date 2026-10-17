#include "quota_collection.h"
#include "quota_parse.h"
#include <stdio.h>
#include <stdlib.h>

int main(void) {
    char data[24576];
    size_t length = fread(data, 1, sizeof(data), stdin);
    if (length == sizeof(data)) return 2;
    quota_snapshot_t *snapshot = calloc(1, sizeof(*snapshot));
    cJSON *root = quota_parse_document(data, length);
    quota_status_t result = root && snapshot ? quota_collection_apply(snapshot, root, "contract-pc") : QUOTA_INVALID;
    if (result == QUOTA_OK) printf("%d\n", snapshot->codex_count);
    cJSON_Delete(root);
    free(snapshot);
    return result == QUOTA_OK ? 0 : 1;
}
