#include "quota_device.h"
#include "bsp_audio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "nvs.h"
#include <stdatomic.h>

static QueueHandle_t s_audio_queue;
static SemaphoreHandle_t s_audio_stopped;
static atomic_bool s_audio_failed;
void quota_device_load(quota_preferences_t *prefs) {
    quota_preferences_default(prefs);
    nvs_handle_t h;
    if (nvs_open("quota_device", NVS_READONLY, &h) != ESP_OK) return;
    quota_preferences_t stored;
    size_t size = sizeof(stored);
    if (nvs_get_blob(h, "preferences", &stored, &size) == ESP_OK)
        quota_preferences_load(&stored, size, prefs);
    nvs_close(h);
}
bool quota_device_save(const quota_preferences_t *prefs) {
    if (!quota_preferences_valid(prefs)) return false;
    nvs_handle_t h;
    if (nvs_open("quota_device", NVS_READWRITE, &h) != ESP_OK) return false;
    esp_err_t result = nvs_set_blob(h, "preferences", prefs, sizeof(*prefs));
    if (result == ESP_OK) result = nvs_commit(h);
    nvs_close(h);
    return result == ESP_OK;
}
static void audio_worker(void *arg) {
    (void)arg;
    /* A short sine with an envelope avoids abrupt sample edges. No recording. */
    static const int16_t wave[16] = {0,2296,4243,5543,6000,5543,4243,2296,0,-2296,-4243,-5543,-6000,-5543,-4243,-2296};
    int16_t pcm[160];
    uint8_t volume;
    for (;;) {
        if (xQueueReceive(s_audio_queue, &volume, portMAX_DELAY) != pdTRUE) continue;
        esp_err_t result = ESP_OK;
        if (volume) {
            result = bsp_audio_init();
            if (result == ESP_OK) result = bsp_audio_wake();
            if (result == ESP_OK) result = bsp_audio_set_format(16000, 16, 1);
            if (result == ESP_OK) bsp_audio_set_volume(volume);
            for (int block=0; block<8 && result==ESP_OK; block++) {
                if (uxQueueMessagesWaiting(s_audio_queue)) break;
                for (int i=0; i<160; i++) {
                    int sample=block*160+i;
                    int envelope = sample < 160 ? sample : sample >= 1120 ? 1279-sample : 160;
                    pcm[i] = wave[sample%16] * envelope / 160;
                }
                result = bsp_audio_write(pcm, sizeof(pcm));
            }
            /* Writes enqueue DMA data. Let the final samples reach the speaker. */
            for (int i=0; i<10 && result==ESP_OK && !uxQueueMessagesWaiting(s_audio_queue); i++)
                vTaskDelay(pdMS_TO_TICKS(10));
        }
        /* The single worker owns wake/format/write/suspend, including cancellation. */
        esp_err_t sleep_result = bsp_audio_sleep();
        atomic_store(&s_audio_failed, result != ESP_OK || sleep_result != ESP_OK);
        if (!volume) xSemaphoreGive(s_audio_stopped);
    }
}
void quota_device_preview_volume(uint8_t volume) {
    if (!s_audio_queue) {
        s_audio_stopped = xSemaphoreCreateBinary();
        if (!s_audio_stopped) { atomic_store(&s_audio_failed, true); return; }
        s_audio_queue = xQueueCreate(1, sizeof(uint8_t));
        if (!s_audio_queue) { vSemaphoreDelete(s_audio_stopped); s_audio_stopped=NULL; atomic_store(&s_audio_failed, true); return; }
        if (xTaskCreate(audio_worker, "quota_tone", 4096, NULL, 3, NULL) != pdPASS) {
            vQueueDelete(s_audio_queue); s_audio_queue = NULL;
            vSemaphoreDelete(s_audio_stopped); s_audio_stopped=NULL;
            atomic_store(&s_audio_failed, true); return;
        }
    }
    atomic_store(&s_audio_failed, false);
    xQueueOverwrite(s_audio_queue, &volume);
}
bool quota_device_stop_preview(void) {
    if (!s_audio_queue) return true;
    xSemaphoreTake(s_audio_stopped, 0);
    uint8_t mute=0; xQueueOverwrite(s_audio_queue, &mute);
    return xSemaphoreTake(s_audio_stopped, pdMS_TO_TICKS(1000)) == pdTRUE;
}
bool quota_device_audio_failed(void) { return atomic_load(&s_audio_failed); }
