/* Record WAV file to SD Card

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "nvs_flash.h"

#include "esp_http_client.h"
#include "sdkconfig.h"
#include "audio_element.h"
#include "audio_pipeline.h"
#include "audio_event_iface.h"
#include "audio_common.h"
#include "http_stream.h"
#include "i2s_stream.h"
#include "wav_encoder.h"

#include "periph_wifi.h"
#include "filter_resample.h"
#include "input_key_service.h"
#include "audio_idf_version.h"
#include "esp_vad.h"
#include "wav_decoder.h"

#if (ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(4, 1, 0))

#include "esp_netif.h"

#else
#include "tcpip_adapter.h"
#endif

// Custom include
#include "i2s_ics43434_init.h"
#include "http_stream_handler.h"
#include "vad_stream.h"


static const char *TAG = "MAIN";

#define OUT_RINGBUF_SIZE (20 * 1024)

#define DEMO_EXIT_BIT (BIT0)

/* Audio */

/* Can be used for i2s_std_gpio_config_t and/or i2s_std_config_t initialization */

static EventGroupHandle_t EXIT_FLAG;
static i2s_chan_handle_t i2s_tx_chan = NULL;
static i2s_chan_handle_t i2s_rx_chan = NULL;

#define VAD_SAMPLE_RATE_HZ 16000
#define VAD_FRAME_LENGTH_MS 30
#define VAD_BUFFER_LENGTH (VAD_FRAME_LENGTH_MS * VAD_SAMPLE_RATE_HZ / 1000)

#define SPEECH_DETECTED_BIT BIT0
static EventGroupHandle_t vad_event_group;


void app_main(void) {
    audio_pipeline_handle_t pipeline;

    audio_element_handle_t i2s_stream_reader,  http_stream_writer;

    ESP_LOGI(TAG, "*** Welcome ***");
    esp_log_level_set("*", ESP_LOG_DEBUG);
    esp_log_level_set("*", ESP_LOG_VERBOSE);

    EXIT_FLAG = xEventGroupCreate();

    // Initialize Board  ****************************************************************************************

    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES) {
        // NVS partition was truncated and needs to be erased
        // Retry nvs_flash_init
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }

    ESP_ERROR_CHECK(esp_netif_init());

    ESP_LOGI(TAG, "[ 1 ] Initialize Peripheral & Connect to wifi network");
    // Initialize peripherals management
    esp_periph_config_t periph_cfg = DEFAULT_ESP_PERIPH_SET_CONFIG();
    esp_periph_set_handle_t set = esp_periph_set_init(&periph_cfg);

    periph_wifi_cfg_t wifi_cfg = {
        .wifi_config.sta.ssid = CONFIG_WIFI_SSID,
        .wifi_config.sta.password = CONFIG_WIFI_PASSWORD,
    };
    esp_periph_handle_t wifi_handle = periph_wifi_init(&wifi_cfg);

    // Start wifi & button peripheral
    esp_periph_start(set, wifi_handle);
    periph_wifi_wait_for_connected(wifi_handle, portMAX_DELAY);

    // Initialize Streams ****************************************************************************************
    // task init runs it in background

    ESP_LOGI(TAG, "[2.1] Create i2s stream to read audio data from codec chip");

    i2s_stream_cfg_t i2s_cfg = {
        .type = AUDIO_STREAM_READER,
        .std_cfg = BSP_I2S_LEFT_MONO_CFG(16000),
        .chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER),
        .task_stack = I2S_STREAM_TASK_STACK,
        .task_core = I2S_STREAM_TASK_CORE,
        .task_prio = I2S_STREAM_TASK_PRIO,
    };
    i2s_cfg.out_rb_size = OUT_RINGBUF_SIZE;
    i2s_stream_reader = i2s_stream_init(&i2s_cfg);

    ESP_LOGI(TAG, "[2.2] Create http stream to post data to server");
    http_stream_cfg_t http_cfg = HTTP_STREAM_CFG_DEFAULT();
    http_cfg.type = AUDIO_STREAM_WRITER;
    http_cfg.event_handle = _http_stream_event_handle;
    http_stream_writer = http_stream_init(&http_cfg);
    audio_element_set_uri(http_stream_writer, CONFIG_SERVER_URI);

    ESP_LOGI(TAG, "[2.3] Create vad stream ");
    audio_element_cfg_t vad_cfg = DEFAULT_AUDIO_ELEMENT_CONFIG();
    vad_cfg.task_stack = 8192; // or 6144, 8192, etc.
    // adjust ring buffer size
    vad_cfg.out_rb_size = OUT_RINGBUF_SIZE;

    audio_element_handle_t vad_stream_processor = vad_stream_init(&vad_cfg);

    // Initialize and run Pipeline ****************************************************************************************
    ESP_LOGI(TAG, "[ 3.0 ] Create audio pipeline for recording");
    audio_pipeline_cfg_t pipeline_cfg = DEFAULT_AUDIO_PIPELINE_CONFIG();
    pipeline = audio_pipeline_init(&pipeline_cfg);
    mem_assert(pipeline_http);

    ESP_LOGI(TAG, "[ 3.3 ] Register all elements to HTTP pipeline");
    audio_pipeline_register(pipeline, i2s_stream_reader, "i2s");
    audio_pipeline_register(pipeline, vad_stream_processor,        "vad");
    audio_pipeline_register(pipeline, http_stream_writer, "http");

    ESP_LOGI(TAG, "[ 3.4 ] Link elements together [codec_chip]-->i2s_stream-->vad-->http-->[http_server]");
    const char *link_tag[3] = {"i2s", "vad","http" };
    audio_pipeline_link(pipeline, &link_tag[0], 3);

    ESP_LOGI(TAG, "[ 3.5 ] Start audio_pipeline");
    audio_pipeline_run(pipeline);

    // Event Interface ****************************************************************************************
    audio_event_iface_cfg_t evt_cfg = AUDIO_EVENT_IFACE_DEFAULT_CFG();
    audio_event_iface_handle_t evt = audio_event_iface_init(&evt_cfg);
    audio_pipeline_set_listener(pipeline, evt);


    // Loop ****************************************************************************************
    while (true) {
        audio_event_iface_msg_t msg;
        if (audio_event_iface_listen(evt, &msg, portMAX_DELAY) == ESP_OK) {
            if (msg.source_type == AUDIO_ELEMENT_TYPE_ELEMENT &&
                msg.cmd == AEL_MSG_CMD_REPORT_STATUS) {

                switch ((int)msg.data) {
                    case VAD_STREAM_EVENT_SPEECH_START:
                        ESP_LOGI(TAG, "=== Speech Detected ===");
                    break;
                    case VAD_STREAM_EVENT_SPEECH_STOP:
                        ESP_LOGI(TAG, "=== Speech Stopped ===");
                    break;
                    default:
                        break;
                }
                }
        }
    }



    ESP_LOGI(TAG, "[ 8 ] Stop audio_pipeline and release all resources");
    audio_pipeline_stop(pipeline);
    audio_pipeline_wait_for_stop(pipeline);
    audio_pipeline_terminate(pipeline);

    audio_pipeline_unregister(pipeline, http_stream_writer);
    audio_pipeline_unregister(pipeline, i2s_stream_reader);


    /* Terminate the pipeline before removing the listener */
    audio_pipeline_remove_listener(pipeline);

    /* Release all resources */
    audio_pipeline_deinit(pipeline);
    audio_element_deinit(http_stream_writer);
    audio_element_deinit(i2s_stream_reader);
    audio_element_deinit(vad_stream_processor);
}
