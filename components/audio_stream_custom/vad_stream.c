#include <string.h>
#include "esp_log.h"
#include "audio_element.h"
#include "audio_mem.h"
#include "audio_common.h"
#include "audio_error.h"

#include "esp_vad.h"
#include "vad_stream.h"

static const char *TAG = "VAD_STREAM";

// Example events
#define VAD_STREAM_EVENT_SPEECH_START  (1)
#define VAD_STREAM_EVENT_SPEECH_STOP   (2)

// Configuration: 16 kHz, 30 ms => 480 samples per frame
#define VAD_SAMPLE_RATE_HZ    16000
#define VAD_FRAME_LENGTH_MS       30
#define VAD_BUFFER_LENGTH (VAD_FRAME_LENGTH_MS * VAD_SAMPLE_RATE_HZ / 1000)
#define VAD_MODE           VAD_MODE_4  // or VAD_MODE_3, etc.


typedef struct {
    vad_handle_t vad_handle;
    bool in_speech;
    uint8_t *frame_buffer; // Accumulate one frame here
    int filled_bytes; // Current byte offset in frame_buffer
} vad_stream_t;

/**
 * @brief Called once when the element is opened
 */

/*vad_handle_t vad_inst = vad_create(VAD_MODE_4);
int16_t *vad_buff = (int16_t *)malloc(VAD_BUFFER_LENGTH * sizeof(short));
if (vad_buff == NULL) {
    ESP_LOGE(TAG, "Memory allocation failed!");
    goto abort_speech_detection;
}*/

/*// Feed samples to the VAD process and get the result
vad_state_t vad_state = vad_process(vad_inst, vad_buff, VAD_SAMPLE_RATE_HZ, VAD_FRAME_LENGTH_MS);
if (vad_state == VAD_SPEECH) {
    ESP_LOGI(TAG, "Speech detected");
}*/

static esp_err_t _vad_open(audio_element_handle_t self, void *ctx) {
    vad_stream_t *vad = (vad_stream_t *) audio_element_getdata(self);
    if (!vad) {
        ESP_LOGE(TAG, "No VAD data context!");
        return ESP_FAIL;
    }

    // Create VAD instance
    vad->vad_handle = vad_create(VAD_MODE);
    vad->in_speech = false;
    vad->filled_bytes = 0;

    // Allocate space for one frame (16-bit samples)
    vad->frame_buffer = (int16_t *) malloc(VAD_BUFFER_LENGTH * sizeof(short));
    AUDIO_MEM_CHECK(TAG, vad->frame_buffer, return ESP_FAIL);

    ESP_LOGI(TAG, "VAD open done (mode=%d, frame=%d samples)", VAD_MODE, VAD_BUFFER_LENGTH);
    return ESP_OK;
}

/**
 * @brief Called once when the element is closed
 */
static esp_err_t _vad_close(audio_element_handle_t self, void *ctx) {
    vad_stream_t *vad = (vad_stream_t *) audio_element_getdata(self);
    if (vad) {
        if (vad->vad_handle) {
            // Newer ESP-ADF uses vad_destroy()
            vad_destroy(vad->vad_handle);
            vad->vad_handle = NULL;
        }
        if (vad->frame_buffer) {
            audio_free(vad->frame_buffer);
            vad->frame_buffer = NULL;
        }
    }
    ESP_LOGI(TAG, "VAD close done");
    return ESP_OK;
}

/**
 * @brief Called repeatedly to process incoming audio
 */
static int _vad_process(audio_element_handle_t self, char *in_buffer, int in_len, void *ctx) {
    int r_size = audio_element_input(self, in_buffer, in_len);
    if (audio_element_is_stopping(self) == true) {
        ESP_LOGW(TAG, "No output due to stopping");
        return AEL_IO_ABORT;
    }
    int w_size = 0;
    if (r_size > 0) {
        vad_stream_t *vad = (vad_stream_t *) audio_element_getdata(self);
        if (!vad || in_len <= 0) {
            return AEL_IO_OK;
        }

        int processed = 0;
        int frame_bytes = VAD_BUFFER_LENGTH * sizeof(short);

        // print ring buffer size and first 10 samples
        /*
        ESP_LOGI(TAG, "IN: VAD process: in_len=%d,  filled_bytes=%d", in_len, vad->filled_bytes);
        for (int i = 0; i < 10; i++) {
            ESP_LOGI(TAG, "  sample[%d]=%d", i, in_buffer[i]);
        }
        */

        while (processed < in_len) {
            int to_copy = frame_bytes - vad->filled_bytes;
            if (to_copy > (in_len - processed)) {
                to_copy = in_len - processed;
            }

            // Accumulate chunk into the frame buffer
            memcpy(vad->frame_buffer + vad->filled_bytes, in_buffer + processed, to_copy);
            vad->filled_bytes += to_copy;
            processed += to_copy;

            // print status of internal buffer
//            ESP_LOGI(TAG, "  processed=%d, to_copy=%d, filled_bytes=%d", processed, to_copy, vad->filled_bytes);

            // If we have exactly one full frame, run VAD
            if (vad->filled_bytes == frame_bytes) {
                // Evaluate VAD
                vad_state_t vstate = vad_process(vad->vad_handle,
                                                 (int16_t *) vad->frame_buffer,
                                                 VAD_SAMPLE_RATE_HZ,
                                                 VAD_FRAME_LENGTH_MS);
                bool speech_now = (vstate == VAD_SPEECH);

                if (speech_now && !vad->in_speech) {
                    vad->in_speech = true;
                    audio_element_report_status(self, VAD_STREAM_EVENT_SPEECH_START);
//                    ESP_LOGI(TAG, "  **************** VAD process: speech detected ****************");
                } else if (!speech_now && vad->in_speech) {
                    vad->in_speech = false;
                    audio_element_report_status(self, VAD_STREAM_EVENT_SPEECH_STOP);
                }

                // Reset for next frame
                vad->filled_bytes = 0;
                // ESP_LOGI(TAG, "  VAD process: forwarding, len processed=%d", r_size);
            }
        }

        if (vad->in_speech) {
            return audio_element_output(self, in_buffer, r_size);  // Forward data
        } else {
            // Consume the input without forwarding to avoid buffer overflow
            ESP_LOGD(TAG, "Silence detected, consuming input without forwarding.");
            return r_size;  // Simulate successful processing
        }

    }
    return w_size;
}

/**
 * @brief Initialize the VAD stream element.
 *
 * The key difference in new ESP-ADF:
 * we set config->open, config->process, config->close before calling audio_element_init().
 */
audio_element_handle_t vad_stream_init(audio_element_cfg_t *config) {
    // 1) Validate or fill in the function pointers
    if (!config->open) {
        config->open = _vad_open;
    }
    if (!config->process) {
        config->process = _vad_process;
    }
    if (!config->close) {
        config->close = _vad_close;
    }

    // Optionally, set a user tag if not provided
    if (!config->tag) {
        config->tag = "vad_stream";
    }

    // 2) Create the audio element
    audio_element_handle_t el = audio_element_init(config);
    AUDIO_MEM_CHECK(TAG, el, return NULL);

    // 3) Allocate our internal context
    vad_stream_t *vad = audio_calloc(1, sizeof(vad_stream_t));
    AUDIO_MEM_CHECK(TAG, vad, {
                    audio_element_deinit(el);
                    return NULL;
                    });

    // 4) Attach context to the element
    audio_element_setdata(el, vad);

    ESP_LOGI(TAG, "VAD element created");
    return el;
}
