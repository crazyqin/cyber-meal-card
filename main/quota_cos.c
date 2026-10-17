#include "quota_cos.h"
#include <stdio.h>
#include <string.h>

static bool sign(const quota_config_t *c, const char *path, const char *query, const char *params, int64_t now,
                    quota_cos_hash_fn hash, char *url, size_t url_cap,
                    char *auth, size_t auth_cap) {
    if (!quota_cos_configured(c) || now < 1700000000 || !hash) return false;
    char host[192], message[2304], key_time[48];
    char sign_key[41], digest[41], signature[41];
    int n = snprintf(host, sizeof(host), "%s.cos.%s.myqcloud.com", c->cos_bucket, c->cos_region);
    if (n < 0 || n >= (int)sizeof(host)) return false;
    n = snprintf(url, url_cap, "https://%s%s%s%s", host, path, query[0] ? "?" : "", query);
    if (n < 0 || (size_t)n >= url_cap) return false;
    /* Bucket/region/path are constrained to URI-safe ASCII by config parsing.
     * Only host is signed; the optional STS token travels as a separate header.
     * Tencent COS permits selecting the signed headers. Query is pre-encoded
     * and sorted; the exact same bytes go on the wire and into the signature. */
    n = snprintf(message, sizeof(message), "get\n%s\n%s\nhost=%s\n", path, query, host);
    if (n < 0 || n >= (int)sizeof(message) || !hash(NULL, 0, message, (size_t)n, digest)) return false;
    snprintf(key_time, sizeof(key_time), "%lld;%lld", (long long)(now - 60), (long long)(now + 300));
    if (!hash(c->cos_secret_key, strlen(c->cos_secret_key), key_time, strlen(key_time), sign_key)) return false;
    n = snprintf(message, sizeof(message), "sha1\n%s\n%s\n", key_time, digest);
    bool ok = n > 0 && n < (int)sizeof(message) && hash(sign_key, 40, message, (size_t)n, signature);
    /* Discard the derived signing key as soon as it is no longer needed. */
    volatile char *wipe = sign_key;
    for (unsigned i = 0; i < sizeof(sign_key); i++) wipe[i] = 0;
    if (!ok) return false;
    n = snprintf(auth, auth_cap, "q-sign-algorithm=sha1&q-ak=%s&q-sign-time=%s&q-key-time=%s&q-header-list=host&q-url-param-list=%s&q-signature=%s",
                 c->cos_secret_id, key_time, key_time, params, signature);
    return n > 0 && (size_t)n < auth_cap;
}
static bool encode(const char *s, char *out, size_t cap) {
    static const char digits[] = "0123456789ABCDEF";
    size_t n = 0;
    for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
        bool safe = (*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
            (*p >= '0' && *p <= '9') || strchr("-_.~", *p);
        if (n + (safe ? 1 : 3) >= cap) return false;
        if (safe) out[n++] = *p;
        else { out[n++] = '%'; out[n++] = digits[*p >> 4]; out[n++] = digits[*p & 15]; }
    }
    out[n] = 0;
    return true;
}
bool quota_cos_sign_object(const quota_config_t *c, const char *collector, int64_t now,
                          quota_cos_hash_fn hash, char *url, size_t url_cap, char *auth, size_t auth_cap) {
    if (!quota_collector_valid(collector)) return false;
    char path[288];
    int n = snprintf(path, sizeof(path), "/%s%s%s/latest.json", c->cos_prefix, c->cos_prefix[0] ? "/" : "", collector);
    return n > 0 && n < (int)sizeof(path) && sign(c, path, "", "", now, hash, url, url_cap, auth, auth_cap);
}
bool quota_cos_sign_list(const quota_config_t *c, const char *marker, int64_t now,
                        quota_cos_hash_fn hash, char *url, size_t url_cap, char *auth, size_t auth_cap) {
    char prefix[132], prefix_encoded[400], marker_encoded[1537], query[2048];
    snprintf(prefix, sizeof(prefix), "%s%s", c->cos_prefix, c->cos_prefix[0] ? "/" : "");
    if (!marker || !encode(prefix, prefix_encoded, sizeof(prefix_encoded)) || !encode(marker, marker_encoded, sizeof(marker_encoded))) return false;
    int n = snprintf(query, sizeof(query), "delimiter=%%2F&marker=%s&max-keys=%d&prefix=%s", marker_encoded, QUOTA_COS_LIST_SIZE, prefix_encoded);
    return n > 0 && n < (int)sizeof(query) && sign(c, "/", query, "delimiter;marker;max-keys;prefix", now, hash, url, url_cap, auth, auth_cap);
}
bool quota_cos_sign_tokenhub(const quota_config_t *c, int64_t now,
                           quota_cos_hash_fn hash, char *url, size_t url_cap, char *auth, size_t auth_cap) {
    /* A root object under the existing prefix, not a Codex collector folder. */
    char path[160];
    int n = snprintf(path, sizeof(path), "/%s%stokenhub.json", c->cos_prefix, c->cos_prefix[0] ? "/" : "");
    return n > 0 && n < (int)sizeof(path) && sign(c, path, "", "", now, hash, url, url_cap, auth, auth_cap);
}
