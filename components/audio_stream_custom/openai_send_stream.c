#include "openai_send_stream.h"
#include "esp_websocket_client.h"
#include <string.h>
#include "esp_log.h"
#include "audio_element.h"
#include "audio_mem.h"
#include "audio_common.h"
#include "audio_error.h"
#include "mbedtls/base64.h"
#include "esp_debug_helpers.h"

static const char *TAG = "ZZ_OPENAI_SEND_STREAM";


// Function to convert PCM16 binary to base64 using mbedtls_base64_encode
char *pcm16_to_base64_mbedtls(const uint8_t *pcm16_buffer, size_t pcm16_len, size_t *out_base64_len) {
    if (!pcm16_buffer || pcm16_len == 0) {
        ESP_LOGE(TAG, "Invalid PCM16 buffer or length");
        return NULL;
    }
    ESP_LOGI(TAG, "PCM16 buffer length: %d", pcm16_len);

    // Calculate the required output buffer size for Base64 encoding
    size_t base64_len = 4 * ((pcm16_len + 2)); // Base64 requires ~4/3 of the input size
    char *base64_output = malloc(base64_len + 1); // Include space for null terminator
    if (!base64_output) {
        ESP_LOGE(TAG, "Failed to allocate memory for base64 encoding");
        return NULL;
    }

    // Perform Base64 encoding
    size_t encoded_len = 0;
    int ret = mbedtls_base64_encode((unsigned char *) base64_output, base64_len, &encoded_len, pcm16_buffer, pcm16_len);
    if (ret != 0) {
        ESP_LOGE(TAG, "Base64 encoding failed with error code: %d", ret);
        ESP_LOGE(TAG, "Buffer size required for encoding: %d", encoded_len);
        ESP_LOGE(TAG, "Buffer size allocated: %d", base64_len);
        free(base64_output);
        return NULL;
    }

    base64_output[encoded_len] = '\0'; // Null-terminate the string
    *out_base64_len = encoded_len;
    return base64_output;
}


// Function to send base64-encoded PCM16 audio via WebSocket
void send_audio_over_websocket(esp_websocket_client_handle_t client, const uint8_t *pcm16_buffer, size_t pcm16_len) {

    // print client and connected status

    if (!client || !esp_websocket_client_is_connected(client)) {
        ESP_LOGE(TAG, "WebSocket client not connected");
        return;
    }

    size_t base64_len = 0;
    char *base64_audio = pcm16_to_base64_mbedtls(pcm16_buffer, pcm16_len, &base64_len);
    if (!base64_audio) {
        ESP_LOGE(TAG, "Failed to convert PCM16 to base64");
        return;
    }

    // Create JSON payload
    char *json_payload = malloc(base64_len + 100); // Adjust buffer size as needed
    if (!json_payload) {
        ESP_LOGE(TAG, "Failed to allocate memory for JSON payload");
        free(base64_audio);
        return;
    }

    snprintf(json_payload, base64_len + 100,
             "{\"type\": \"input_audio_buffer.append\", \"audio\": \"%s\"}",
             base64_audio);

    // Send JSON payload via WebSocket
    int length = esp_websocket_client_send_text(client, json_payload, strlen(json_payload), portMAX_DELAY);
    ESP_LOGI(TAG, "Data sent: %d", length);

    free(base64_audio);
    free(json_payload);
}


esp_err_t openai_send_stream_process(audio_element_handle_t self, char *in_buffer, int in_len, void *ctx) {


    openai_send_stream_t_cfg *openai_send = (openai_send_stream_t_cfg *) audio_element_getdata(self);
    if (!openai_send) {
        ESP_LOGE(TAG, "No data found for openai_send_stream");
        return AEL_IO_ABORT;
    }

    if (openai_send->client == NULL) {
        ESP_LOGW(TAG, "No WebSocket client connected");
        return AEL_IO_ABORT;
    }

    if (audio_element_is_stopping(self) == true) {
        ESP_LOGW(TAG, "No output due to stopping");
        return AEL_IO_ABORT;
    }

    // check if the frame_to_openai_length is set


    int buffer_size=openai_send->frame_to_openai_length;
    if (in_len>buffer_size) {
        buffer_size=in_len;
    }

    int r_size = audio_element_input(self, in_buffer, buffer_size);
    ESP_LOGI(TAG, "Pushing to Websocket finally: %d", r_size);

    send_audio_over_websocket(openai_send->client, (uint8_t*)  in_buffer, r_size);

    ESP_LOGI(TAG, "Data sent to OpenAI with length: %d", r_size);
    audio_element_output(self, in_buffer, r_size);

    return r_size;
}


// open and close functions
esp_err_t openai_send_stream_open(audio_element_handle_t self) {
    ESP_LOGI(TAG, "openai_send_stream_open");
    return ESP_OK;
}

esp_err_t openai_send_stream_close(audio_element_handle_t self) {
    ESP_LOGI(TAG, "openai_send_stream_close");
    ESP_LOGI("CALLSTACK", "Printing call stack:");
    esp_backtrace_print(100); // 100 is the depth of the call stack to print
    return ESP_OK;
}

esp_err_t openai_send_stream_destroy(audio_element_handle_t self) {
    openai_send_stream_t_cfg *openai_send = (openai_send_stream_t_cfg *) audio_element_getdata(self);
    audio_free(openai_send);
    return ESP_OK;
}

audio_element_handle_t openai_send_stream_init(openai_send_stream_t_cfg *config) {
    openai_send_stream_t_cfg *openai_send_cfg = (openai_send_stream_t_cfg *) audio_calloc(1, sizeof(openai_send_stream_t_cfg));
    AUDIO_MEM_CHECK(TAG, openai_send_cfg, return NULL);

    openai_send_cfg->frame_to_openai_length = config->frame_to_openai_length;
    ESP_LOGI(TAG, "Frame to OpenAI length: %d", openai_send_cfg->frame_to_openai_length);

    if (config->client == NULL) {
        ESP_LOGE(TAG, "No WebSocket client provided");
        audio_free(openai_send_cfg);
        return NULL;
    }

    openai_send_cfg->client = config->client;

    audio_element_cfg_t cfg = DEFAULT_AUDIO_ELEMENT_CONFIG();
    cfg.task_stack = config->task_stack;
    cfg.open = openai_send_stream_open;
    cfg.close = openai_send_stream_close;
    cfg.process = openai_send_stream_process;
    cfg.destroy = openai_send_stream_destroy;


    audio_element_handle_t el = audio_element_init(&cfg);
    AUDIO_MEM_CHECK(TAG, el, {
        audio_free(openai_send_cfg);
        return NULL;
    });
//    audio_element_set_input_timeout(el, DEFAULT_MAX_WAIT_TIME;
    audio_element_setdata(el, openai_send_cfg);

    return el;
}