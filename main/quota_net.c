#include "quota_net.h"
#include "quota_parse.h"
#include "quota_config.h"
#include "quota_cos.h"
#include "quota_discovery.h"
#include "quota_collection.h"
#include "quota_setup_access.h"
#ifdef QUOTA_PRIVATE_COS_DEFAULTS
#include "quota_private_cos.h"
#endif
#include "mbedtls/md.h"
#include "esp_crt_bundle.h"
#include "esp_event.h"
#include "esp_http_client.h"
#include "esp_http_server.h"
#include "esp_netif.h"
#include "esp_random.h"
#include "esp_sntp.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "lwip/sockets.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

#define RESPONSE_CAP 24576
#define CONFIG_CAP 14336
#define REFRESH_BIT 1
#define SETUP_BIT 2
#define CONNECT_BIT 4
#define FIRMWARE_TAG "20261017-joined-titles"
typedef struct { uint32_t version, generation; quota_snapshot_t snapshot; } quota_cache_t;
static quota_config_t s_config;
static quota_snapshot_t s_snapshot;
static quota_connection_t s_connection;
static SemaphoreHandle_t s_mutex;
static QueueHandle_t s_config_queue;
static TaskHandle_t s_worker;
static httpd_handle_t s_server;
static volatile bool s_wifi;
static volatile bool s_config_pending;
static volatile bool s_setup_touched;
static esp_netif_t *s_ap_netif;
static int64_t s_sync_deadline;
static int s_fetch_cursor;
static quota_status_t failure(int code);
extern const unsigned char setup_html_start[] asm("_binary_setup_html_start");
extern const unsigned char setup_html_end[] asm("_binary_setup_html_end");
extern const unsigned char backup_js_start[] asm("_binary_backup_js_start");
extern const unsigned char backup_js_end[] asm("_binary_backup_js_end");
extern const unsigned char qr_js_start[] asm("_binary_qrcodegen_v1_8_0_es6_js_start");
extern const unsigned char qr_js_end[] asm("_binary_qrcodegen_v1_8_0_es6_js_end");

static esp_err_t save_blob(const char *key, const void *blob, size_t size) {
    nvs_handle_t h;
    esp_err_t err = nvs_open("quota", NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    err = nvs_set_blob(h, key, blob, size);
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    return err;
}
static bool load_blob(const char *key, void *blob, size_t size) {
    nvs_handle_t h;
    if (nvs_open("quota", NVS_READONLY, &h) != ESP_OK) return false;
    size_t actual = size;
    esp_err_t err = nvs_get_blob(h, key, blob, &actual);
    nvs_close(h);
    return err == ESP_OK && actual == size;
}
void quota_net_read(quota_selection_t *selection, quota_view_t *view, quota_connection_t *connection) {
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    quota_select(&s_snapshot, selection, view);
    *connection = s_connection;
    connection->wifi = s_wifi;
    connection->clock_ready = time(NULL) > 1700000000;
    connection->cos_missing = quota_cos_missing(&s_config);
    xSemaphoreGive(s_mutex);
}
void quota_net_refresh(void) { if (s_worker) xTaskNotify(s_worker, REFRESH_BIT, eSetBits); }
void quota_net_setup(void) { if (s_worker) xTaskNotify(s_worker, SETUP_BIT, eSetBits); }
void quota_net_connect(void) { if (s_worker) xTaskNotify(s_worker, CONNECT_BIT, eSetBits); }
static void connection_state(bool setup, bool syncing) {
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_connection.setup = setup;
    s_connection.syncing = syncing;
    xSemaphoreGive(s_mutex);
}
static void wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data) {
    (void)arg; (void)data;
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) s_wifi = false;
    if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) s_wifi = true;
}
static bool connect_station(const quota_config_t *cfg) {
    s_wifi = false;
    bool keep_ap = s_connection.setup;
    esp_wifi_disconnect();
    if (!keep_ap) esp_wifi_stop();
    if (!cfg->ssid[0]) return false;
    wifi_config_t wifi = {0};
    memcpy(wifi.sta.ssid, cfg->ssid, strlen(cfg->ssid));
    memcpy(wifi.sta.password, cfg->password, strlen(cfg->password));
    wifi.sta.pmf_cfg.capable = true;
    if (esp_wifi_set_mode(keep_ap ? WIFI_MODE_APSTA : WIFI_MODE_STA) != ESP_OK ||
        esp_wifi_set_config(WIFI_IF_STA, &wifi) != ESP_OK ||
        (!keep_ap && esp_wifi_start() != ESP_OK) || esp_wifi_connect() != ESP_OK) return false;
    for (int i = 0; i < 200 && !s_wifi; i++) vTaskDelay(pdMS_TO_TICKS(100));
    return s_wifi;
}
typedef struct { char *buffer; size_t length; bool overflow; } response_t;
static bool sync_interrupted(void) {
    return !s_wifi || s_config_pending ||
        (s_sync_deadline && esp_timer_get_time() / 1000 >= s_sync_deadline) ||
        (ulTaskNotifyValueClear(NULL, 0) & (SETUP_BIT | CONNECT_BIT));
}
static esp_err_t http_event(esp_http_client_event_t *event) {
    response_t *r = event->user_data;
    if (sync_interrupted()) return ESP_FAIL;
    if (event->event_id != HTTP_EVENT_ON_DATA) return ESP_OK;
    if (event->data_len < 0 || r->length + (size_t)event->data_len >= RESPONSE_CAP) {
        r->overflow = true;
        return ESP_FAIL;
    }
    memcpy(r->buffer + r->length, event->data, event->data_len);
    r->length += event->data_len;
    r->buffer[r->length] = 0;
    return ESP_OK;
}
/* One TLS connection at a time. Redirects are disabled so credentials stay on
 * the hard-coded provider origin. Response bodies never enter logs or NVS. */
static char *request_raw(const char *url, const char *authorization, const char *token, int *status, size_t *length) {
    *status = -1;
    *length = 0;
    if (sync_interrupted()) return NULL;
    response_t response = {.buffer = calloc(1, RESPONSE_CAP)};
    if (!response.buffer) return NULL;
    esp_http_client_config_t cfg = {
        .url = url, .timeout_ms = 5000, .crt_bundle_attach = esp_crt_bundle_attach,
        .event_handler = http_event, .user_data = &response,
        .disable_auto_redirect = true, .buffer_size = 1024, .buffer_size_tx = 1024,
        .user_agent = "AI-Passport-Subscriptions/1.0",
    };
    if (s_sync_deadline) {
        int64_t remaining = s_sync_deadline - esp_timer_get_time() / 1000;
        if (remaining < 1) { free(response.buffer); return NULL; }
        if (remaining < cfg.timeout_ms) cfg.timeout_ms = (int)remaining;
    }
    /* A JWT can exceed the default 1 KiB header buffer; a header must fit as a
     * whole in esp_http_client's transmit buffer. */
    size_t header_size = strlen(url) + (authorization ? strlen(authorization) : 0) + (token ? strlen(token) : 0) + 512;
    if (header_size > (size_t)cfg.buffer_size_tx) cfg.buffer_size_tx = header_size;
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) { free(response.buffer); return NULL; }
    if (authorization && esp_http_client_set_header(client, "Authorization", authorization) != ESP_OK) goto done;
    if (token && token[0] && esp_http_client_set_header(client, "x-cos-security-token", token) != ESP_OK) goto done;
    esp_http_client_set_header(client, "Accept", "application/json");
    esp_http_client_set_header(client, "Cache-Control", "no-cache");
    if (esp_http_client_perform(client) == ESP_OK && !response.overflow)
        *status = esp_http_client_get_status_code(client);
done:
    esp_http_client_cleanup(client);
    if (*status != 200) { free(response.buffer); return NULL; }
    *length = response.length;
    return response.buffer;
}
static cJSON *request(const char *url, const char *authorization, const char *token, int *status) {
    size_t length;
    char *data = request_raw(url, authorization, token, status, &length);
    cJSON *json = data ? quota_parse_document(data, length) : NULL;
    free(data);
    return json;
}
static bool cos_hash(const char *key, size_t key_len, const char *message, size_t length, char hex[41]) {
    const mbedtls_md_info_t *sha1 = mbedtls_md_info_from_type(MBEDTLS_MD_SHA1);
    unsigned char digest[20];
    int err = key ? mbedtls_md_hmac(sha1, (const unsigned char *)key, key_len,
                                   (const unsigned char *)message, length, digest) :
                    mbedtls_md(sha1, (const unsigned char *)message, length, digest);
    if (err) return false;
    static const char digits[] = "0123456789abcdef";
    for (int i = 0; i < 20; i++) { hex[i * 2] = digits[digest[i] >> 4]; hex[i * 2 + 1] = digits[digest[i] & 15]; }
    hex[40] = 0;
    return true;
}
static quota_status_t failure(int code) {
    if (code == 401) return QUOTA_AUTH;
    if (code == 403) return QUOTA_BLOCKED;
    if (code == 404) return QUOTA_UNSUPPORTED;
    if (code == 429) return QUOTA_THROTTLED;
    if (code == 200) return QUOTA_INVALID;
    return QUOTA_NETWORK;
}
static void codex_unavailable(quota_status_t status) {
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_snapshot.discovery_status = status;
    for (int i = 0; i < s_snapshot.codex_count; i++) s_snapshot.providers[QUOTA_CODEX + i].status = status;
    xSemaphoreGive(s_mutex);
}
static void fetch_codex(void) {
    if (!quota_cos_configured(&s_config)) { codex_unavailable(QUOTA_UNCONFIGURED); return; }
    quota_snapshot_t *next = malloc(sizeof(*next));
    quota_cos_page_t *page = malloc(sizeof(*page));
    char (*collectors)[65] = calloc(QUOTA_MAX_CODEX, 65);
    if (!next || !page || !collectors) {
        free(next); free(page); free(collectors); codex_unavailable(QUOTA_STORAGE); return;
    }
    s_sync_deadline = esp_timer_get_time() / 1000 + 45000;
    xSemaphoreTake(s_mutex, portMAX_DELAY); *next = s_snapshot; xSemaphoreGive(s_mutex);
    next->discovery_limited = false;
    next->discovery_status = QUOTA_OK;
    int count = 0;
    bool complete = false;
    char marker[512] = "", url[2304], auth[704];
    for (int requests = 0; requests < 128; requests++) {
        if (sync_interrupted()) { next->discovery_status = QUOTA_NETWORK; break; }
        int status = -1; size_t length = 0;
        char *xml = NULL;
        if (quota_cos_sign_list(&s_config, marker, time(NULL), cos_hash, url, sizeof(url), auth, sizeof(auth)))
            xml = request_raw(url, auth, s_config.cos_token, &status, &length);
        xSemaphoreTake(s_mutex, portMAX_DELAY); s_connection.cos_http = status; xSemaphoreGive(s_mutex);
        bool ok = xml && quota_cos_parse_list(xml, length, s_config.cos_prefix, page);
        free(xml);
        if (!ok) { next->discovery_status = status == 403 ? QUOTA_AUTH : failure(status); break; }
        for (int i = 0; i < page->count; i++) {
            if (count == QUOTA_MAX_CODEX) { next->discovery_limited = true; break; }
            bool duplicate = false;
            for (int j = 0; j < count; j++) if (!strcmp(collectors[j], page->collectors[i])) duplicate = true;
            if (duplicate) { next->discovery_status = QUOTA_INVALID; break; }
            snprintf(collectors[count++], 65, "%s", page->collectors[i]);
        }
        if (next->discovery_limited || next->discovery_status != QUOTA_OK) break;
        if (!page->truncated) { complete = true; break; }
        if (strcmp(page->next_marker, marker) <= 0) { next->discovery_status = QUOTA_INVALID; break; }
        snprintf(marker, sizeof(marker), "%s", page->next_marker);
    }
    if (!complete && next->discovery_status == QUOTA_OK) next->discovery_limited = true;
    if (complete) quota_codex_prune(next, (const char (*)[65])collectors, count);
    /* Retained rows that could not be visited must never appear freshly read. */
    for (int i = 0; i < next->codex_count; i++)
        next->providers[QUOTA_CODEX + i].status = next->discovery_status == QUOTA_OK ? QUOTA_NETWORK : next->discovery_status;
    int start = count ? s_fetch_cursor % count : 0;
    int visited = 0;
    for (; visited < count; visited++) {
        int i = (start + visited) % count;
        if (sync_interrupted()) { next->discovery_status = QUOTA_NETWORK; break; }
        int status = -1; cJSON *json = NULL;
        if (quota_cos_sign_object(&s_config, collectors[i], time(NULL), cos_hash, url, sizeof(url), auth, sizeof(auth)))
            json = request(url, auth, s_config.cos_token, &status);
        if (status == 404) {
            quota_codex_remove_collector(next, collectors[i]);
            cJSON_Delete(json);
            continue; /* Historical-only directories have no current accounts. */
        }
        quota_status_t result = json ? quota_collection_apply(next, json, collectors[i]) :
            status == 403 ? QUOTA_AUTH : failure(status);
        cJSON_Delete(json);
        if (result != QUOTA_OK) {
            quota_codex_fail_collector(next, collectors[i], result);
            if (result == QUOTA_LIMIT) next->discovery_limited = true;
            else if (next->discovery_status == QUOTA_OK) next->discovery_status = result;
        }
        /* Publish complete machine reports promptly, keeping each report atomic. */
        quota_codex_sort(next);
        xSemaphoreTake(s_mutex, portMAX_DELAY); s_snapshot = *next; xSemaphoreGive(s_mutex);
    }
    /* Resume with unvisited machines next cycle when the time budget expires. */
    s_fetch_cursor = count ? (start + visited) % count : 0;
    quota_codex_sort(next);
    xSemaphoreTake(s_mutex, portMAX_DELAY); s_snapshot = *next; xSemaphoreGive(s_mutex);
    free(next); free(page); free(collectors);
    s_sync_deadline = 0;
}
static esp_err_t setup_page(httpd_req_t *req) {
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_set_hdr(req, "Content-Security-Policy", "default-src 'self'; script-src 'self' 'unsafe-inline'; style-src 'unsafe-inline'; connect-src 'self'; img-src 'self' data: blob:; frame-ancestors 'none'");
    return httpd_resp_send(req, (const char *)setup_html_start, setup_html_end - setup_html_start - 1);
}
static esp_err_t send_json(httpd_req_t *req, cJSON *json) {
    char *data = json ? cJSON_PrintUnformatted(json) : NULL;
    cJSON_Delete(json);
    if (!data) return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_set_hdr(req, "Pragma", "no-cache");
    esp_err_t result = httpd_resp_sendstr(req, data);
    /* Export buffers contain credentials. Clear the temporary serialized copy. */
    volatile char *wipe = data; size_t n = strlen(data);
    for (size_t i = 0; i < n; i++) wipe[i] = 0;
    free(data);
    return result;
}
static esp_err_t setup_script(httpd_req_t *req) {
    bool qr = !strcmp(req->uri, "/qr.js");
    const unsigned char *start = qr ? qr_js_start : backup_js_start;
    const unsigned char *end = qr ? qr_js_end : backup_js_end;
    httpd_resp_set_type(req, "application/javascript; charset=utf-8");
    return httpd_resp_send(req, (const char *)start, end - start);
}
static cJSON *receive_json(httpd_req_t *req, int maximum) {
    char type[64];
    if (httpd_req_get_hdr_value_str(req, "Content-Type", type, sizeof(type)) != ESP_OK ||
        strncmp(type, "application/json", 16) || (type[16] && type[16] != ';') ||
        req->content_len <= 0 || req->content_len >= maximum) return NULL;
    char *data = calloc(1, req->content_len + 1);
    if (!data) return NULL;
    int received = 0;
    while (received < req->content_len) {
        int n = httpd_req_recv(req, data + received, req->content_len - received);
        if (n <= 0) { free(data); return NULL; }
        received += n;
    }
    cJSON *root = quota_parse_document(data, received);
    volatile char *wipe = data;
    for (int i = 0; i < received; i++) wipe[i] = 0;
    free(data);
    return root;
}
static esp_err_t setup_status(httpd_req_t *req) {
    cJSON *json = cJSON_CreateObject();
    if (!json) return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    cJSON_AddStringToObject(json, "firmware", FIRMWARE_TAG);
    cJSON_AddBoolToObject(json, "wifi", s_wifi);
    cJSON_AddBoolToObject(json, "clock", time(NULL) > 1700000000);
    cJSON_AddBoolToObject(json, "connecting", s_connection.connecting);
    cJSON_AddBoolToObject(json, "syncing", s_connection.syncing);
    cJSON_AddNumberToObject(json, "wait", s_connection.sync_wait_seconds);
    cJSON_AddNumberToObject(json, "save", s_connection.save_state);
    cJSON_AddBoolToObject(json, "wifi_configured", s_config.ssid[0]);
    cJSON_AddBoolToObject(json, "collector_only", true);
    cJSON_AddNumberToObject(json, "cos_missing", quota_cos_missing(&s_config));
    cJSON_AddBoolToObject(json, "builtin_cos", s_connection.builtin_cos);
    cJSON_AddNumberToObject(json, "codex_accounts", s_snapshot.codex_count);
    cJSON_AddNumberToObject(json, "codex_status", s_snapshot.discovery_status);
    cJSON_AddNumberToObject(json, "cos_http", s_connection.cos_http);
    xSemaphoreGive(s_mutex);
    return send_json(req, json);
}
static esp_err_t setup_action(httpd_req_t *req) {
    cJSON *root = receive_json(req, 128);
    const cJSON *action = cJSON_GetObjectItemCaseSensitive(root, "action");
    bool connect = cJSON_IsString(action) && !strcmp(action->valuestring, "connect");
    bool sync = cJSON_IsString(action) && !strcmp(action->valuestring, "sync");
    cJSON_Delete(root);
    if (!connect && !sync) return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Unknown action");
    if (s_config_pending) { httpd_resp_set_status(req, "409 Conflict"); return httpd_resp_sendstr(req, "Save in progress"); }
    s_setup_touched = true;
    if (connect) quota_net_connect(); else quota_net_refresh();
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, "{\"accepted\":true}");
}
static esp_err_t setup_export(httpd_req_t *req) {
    cJSON *request = receive_json(req, 128);
    if (!request) return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "JSON required");
    cJSON_Delete(request);
    if (s_config_pending) { httpd_resp_set_status(req, "409 Conflict"); return httpd_resp_sendstr(req, "Save in progress"); }
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    cJSON *backup = quota_config_export(&s_config);
    xSemaphoreGive(s_mutex);
    s_setup_touched = true;
    return send_json(req, backup);
}
static esp_err_t config_busy(httpd_req_t *req) {
    httpd_resp_set_status(req, "503 Service Unavailable");
    return httpd_resp_sendstr(req, "Already connecting");
}
static esp_err_t configure(httpd_req_t *req) {
    if (s_config_pending) return config_busy(req);
    cJSON *json = receive_json(req, CONFIG_CAP);
    if (!json) return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON configuration");
    quota_config_t *candidate = malloc(sizeof(*candidate));
    if (!candidate) {
        cJSON_Delete(json); free(candidate);
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
    }
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    bool ok = quota_config_prepare(json, &s_config, candidate);
    xSemaphoreGive(s_mutex);
    cJSON_Delete(json);
    if (!ok) { free(candidate); return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid configuration"); }
    candidate->version = QUOTA_CONFIG_VERSION;
    if (xQueueSend(s_config_queue, &candidate, 0) != pdTRUE) {
        free(candidate);
        return config_busy(req);
    }
    s_config_pending = true;
    s_setup_touched = true;
    xSemaphoreTake(s_mutex, portMAX_DELAY); s_connection.save_state = 1; xSemaphoreGive(s_mutex);
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, "{\"accepted\":true}");
}
static esp_err_t accept_setup_client(httpd_handle_t server, int socket) {
    (void)server;
    struct sockaddr_storage local = {0};
    socklen_t length = sizeof(local);
    esp_netif_ip_info_t ap;
    /* AP+STA permits testing Wi-Fi while keeping setup open. Serve no setup,
     * status or secret-export requests through the home-network interface. */
    /* IDF's dual-stack server returns AF_INET6 + ::ffff:192.168.4.1 even
     * for IPv4 clients. A sockaddr_in buffer/family check rejects every one. */
    if (getsockname(socket, (struct sockaddr *)&local, &length) ||
        !s_ap_netif || esp_netif_get_ip_info(s_ap_netif, &ap) != ESP_OK ||
        !quota_setup_address_matches(&local, length, ap.ip.addr))
        return ESP_FAIL;
    return ESP_OK;
}
static void start_setup(void) {
    if (s_server) return;
    s_config_pending = false;
    esp_wifi_stop();
    s_wifi = false;
    uint32_t random = esp_random();
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    snprintf(s_connection.ap_name, sizeof(s_connection.ap_name), "Passport-Quota-%04X", (unsigned)(random & 65535));
    snprintf(s_connection.ap_password, sizeof(s_connection.ap_password), "%08lX%04lX", (unsigned long)esp_random(), (unsigned long)(esp_random() & 65535));
    wifi_config_t ap = {0};
    strcpy((char *)ap.ap.ssid, s_connection.ap_name);
    strcpy((char *)ap.ap.password, s_connection.ap_password);
    ap.ap.ssid_len = strlen(s_connection.ap_name);
    ap.ap.max_connection = 1;
    ap.ap.authmode = WIFI_AUTH_WPA2_PSK;
    xSemaphoreGive(s_mutex);
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap));
    ESP_ERROR_CHECK(esp_wifi_start());
    httpd_config_t http = HTTPD_DEFAULT_CONFIG();
    http.stack_size = 6144;
    /* HTML, two local scripts and status polling can overlap on mobile browsers.
     * Four clients + three HTTPD internal sockets fit LWIP_MAX_SOCKETS=10. */
    http.max_open_sockets = 4;
    http.lru_purge_enable = true;
    http.open_fn = accept_setup_client;
    if (httpd_start(&s_server, &http) == ESP_OK) {
        httpd_uri_t get = {.uri = "/", .method = HTTP_GET, .handler = setup_page};
        httpd_uri_t post = {.uri = "/config", .method = HTTP_POST, .handler = configure};
        httpd_register_uri_handler(s_server, &get);
        httpd_register_uri_handler(s_server, &post);
        httpd_uri_t status = {.uri = "/status", .method = HTTP_GET, .handler = setup_status};
        httpd_uri_t action = {.uri = "/action", .method = HTTP_POST, .handler = setup_action};
        httpd_uri_t export = {.uri = "/export", .method = HTTP_POST, .handler = setup_export};
        httpd_uri_t backup = {.uri = "/backup.js", .method = HTTP_GET, .handler = setup_script};
        httpd_uri_t qr = {.uri = "/qr.js", .method = HTTP_GET, .handler = setup_script};
        httpd_register_uri_handler(s_server, &status);
        httpd_register_uri_handler(s_server, &action);
        httpd_register_uri_handler(s_server, &export);
        httpd_register_uri_handler(s_server, &backup);
        httpd_register_uri_handler(s_server, &qr);
    }
    connection_state(true, false);
}
static void stop_setup(void) {
    if (s_server) { httpd_stop(s_server); s_server = NULL; }
    esp_wifi_set_mode(WIFI_MODE_STA);
    connection_state(false, false);
}
static void worker(void *arg) {
    (void)arg;
    /* The new struct is larger. Read the old blob into a separate buffer so
     * changing sizeof(config) does not silently discard deployed Wi-Fi keys. */
    void *stored = calloc(1, sizeof(s_config));
    nvs_handle_t handle;
    if (stored && nvs_open("quota", NVS_READONLY, &handle) == ESP_OK) {
        size_t length = sizeof(s_config);
        if (nvs_get_blob(handle, "config", stored, &length) == ESP_OK)
            quota_config_load(stored, length, &s_config);
        nvs_close(handle);
    }
#ifdef QUOTA_PRIVATE_COS_DEFAULTS
    quota_config_apply_cos_defaults(&s_config, &quota_private_cos_defaults);
    quota_config_apply_wifi_defaults(&s_config, &quota_private_cos_defaults);
    s_connection.builtin_cos = true;
#endif
    free(stored);
    quota_cache_t *cache = calloc(1, sizeof(*cache));
    if (cache && load_blob("cache", cache, sizeof(*cache)) && cache->version == 7 && cache->generation == s_config.generation) {
        if (cache->snapshot.codex_count < 0 || cache->snapshot.codex_count > QUOTA_MAX_CODEX) cache->snapshot.codex_count = 0;
        for (int i = 0; i < QUOTA_PROVIDERS; i++) {
            quota_provider_t *p = &cache->snapshot.providers[i];
            bool invalid_points = p->kind == QUOTA_POINTS && (p->window_count || p->balance_count ||
                !memchr(p->points.balance, 0, sizeof(p->points.balance)) ||
                !memchr(p->points.spent_today, 0, sizeof(p->points.spent_today)) ||
                !memchr(p->points.added_today, 0, sizeof(p->points.added_today)));
            if (invalid_points || p->window_count < 0 || p->window_count > QUOTA_WINDOWS || p->balance_count < 0 || p->balance_count > QUOTA_BALANCES ||
                !memchr(p->source, 0, sizeof(p->source)) || !memchr(p->account_id, 0, sizeof(p->account_id)) || !memchr(p->label, 0, sizeof(p->label)) || !memchr(p->account, 0, sizeof(p->account)) || !memchr(p->collector, 0, sizeof(p->collector)) || !memchr(p->plan, 0, sizeof(p->plan))) memset(p, 0, sizeof(*p));
            else p->status = QUOTA_NETWORK; /* Cached data is explicitly stale on boot. */
        }
        xSemaphoreTake(s_mutex, portMAX_DELAY);
        s_snapshot = cache->snapshot;
        xSemaphoreGive(s_mutex);
    }
    free(cache);
    s_snapshot.unified_mode = true;
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();
    s_ap_netif = esp_netif_create_default_wifi_ap();
    wifi_init_config_t wifi = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&wifi));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event, NULL));
    setenv("TZ", "CST-8", 1); tzset();
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "ntp.aliyun.com");
    esp_sntp_setservername(1, "pool.ntp.org");
    esp_sntp_init();
    int64_t next_poll = 0, last_poll = -60000, setup_at = 0, last_cache = -3600000;
    bool manual_pending = false;
    if (!s_config.ssid[0]) { start_setup(); setup_at = esp_timer_get_time() / 1000; }
    for (;;) {
        uint32_t bits = 0;
        xTaskNotifyWait(0, UINT32_MAX, &bits, pdMS_TO_TICKS(250));
        int64_t now = esp_timer_get_time() / 1000;
        if (bits & REFRESH_BIT) manual_pending = true;
        if (s_setup_touched) { s_setup_touched = false; setup_at = now; }
        if (bits & SETUP_BIT) {
            if (s_connection.setup) { stop_setup(); next_poll = 0; }
            else { start_setup(); setup_at = now; }
        }
        quota_config_t *candidate = NULL;
        if (xQueueReceive(s_config_queue, &candidate, 0) == pdTRUE) {
            vTaskDelay(pdMS_TO_TICKS(500)); /* Let the HTTP acknowledgement leave. */
            candidate->generation = esp_random() | 1;
            bool changed_wifi = strcmp(candidate->ssid, s_config.ssid) || strcmp(candidate->password, s_config.password);
            xSemaphoreTake(s_mutex, portMAX_DELAY); s_connection.connecting = changed_wifi; xSemaphoreGive(s_mutex);
            bool wifi_ok = !changed_wifi || connect_station(candidate);
            bool saved = wifi_ok && save_blob("config", candidate, sizeof(*candidate)) == ESP_OK;
            if (saved) {
                xSemaphoreTake(s_mutex, portMAX_DELAY);
                s_config = *candidate;
                memset(&s_snapshot, 0, sizeof(s_snapshot));
                s_snapshot.unified_mode = true;
                s_connection.config_failed = false;
                s_connection.save_state = 2;
                xSemaphoreGive(s_mutex);
                next_poll = 0;
                manual_pending = true;
            } else {
                xSemaphoreTake(s_mutex, portMAX_DELAY);
                s_connection.config_failed = true;
                s_connection.save_state = wifi_ok ? -2 : -1;
                xSemaphoreGive(s_mutex);
                if (!s_server) start_setup();
                setup_at = now;
            }
            xSemaphoreTake(s_mutex, portMAX_DELAY); s_connection.connecting = false; xSemaphoreGive(s_mutex);
            s_config_pending = false;
            free(candidate);
        }
        if (bits & CONNECT_BIT) {
            xSemaphoreTake(s_mutex, portMAX_DELAY); s_connection.connecting = true; xSemaphoreGive(s_mutex);
            if (!s_config.ssid[0] && !s_server) { start_setup(); setup_at = now; }
            bool connected = connect_station(&s_config);
            xSemaphoreTake(s_mutex, portMAX_DELAY);
            s_connection.connecting = false;
            s_connection.config_failed = !connected;
            xSemaphoreGive(s_mutex);
            now = esp_timer_get_time() / 1000;
        }
        if (s_connection.setup) {
            if (s_config.ssid[0] && now - setup_at > 300000) { stop_setup(); next_poll = 0; }
            else if (!manual_pending) continue;
        }
        if (!manual_pending && now < next_poll) continue;
        int wait_seconds = quota_sync_wait_seconds(now, last_poll, manual_pending);
        if (wait_seconds) {
            xSemaphoreTake(s_mutex, portMAX_DELAY);
            s_connection.sync_wait_seconds = wait_seconds;
            xSemaphoreGive(s_mutex);
            continue; /* Retain a manual request until it can run. */
        }
        manual_pending = false;
        xSemaphoreTake(s_mutex, portMAX_DELAY); s_connection.sync_wait_seconds = 0; xSemaphoreGive(s_mutex);
        last_poll = now;
        connection_state(s_server != NULL, true);
        if (!s_wifi) {
            xSemaphoreTake(s_mutex, portMAX_DELAY); s_connection.connecting = true; xSemaphoreGive(s_mutex);
            connect_station(&s_config);
            xSemaphoreTake(s_mutex, portMAX_DELAY); s_connection.connecting = false; xSemaphoreGive(s_mutex);
        }
        for (int wait = 0; wait < 100 && s_wifi && time(NULL) < 1700000000; wait++) vTaskDelay(pdMS_TO_TICKS(100));
        if (s_wifi && time(NULL) > 1700000000) {
            fetch_codex();
            if (now - last_cache >= 3600000) { /* At most one cache write/hour. */
                cache = calloc(1, sizeof(*cache));
                if (cache) {
                    cache->version = 7;
                    cache->generation = s_config.generation;
                    xSemaphoreTake(s_mutex, portMAX_DELAY); cache->snapshot = s_snapshot; xSemaphoreGive(s_mutex);
                    if (save_blob("cache", cache, sizeof(*cache)) == ESP_OK) last_cache = now;
                    free(cache);
                }
            }
        } else {
            codex_unavailable(quota_cos_configured(&s_config) ? QUOTA_NETWORK : QUOTA_UNCONFIGURED);
        }
        connection_state(s_server != NULL, false);
        next_poll = esp_timer_get_time() / 1000 + 300000;
    }
}
void quota_net_start(void) {
    s_mutex = xSemaphoreCreateMutex();
    s_config_queue = xQueueCreate(1, sizeof(quota_config_t *));
    configASSERT(s_mutex && s_config_queue);
    BaseType_t started = xTaskCreate(worker, "quota_network", 16384, NULL, 4, &s_worker);
    configASSERT(started == pdPASS);
}
