/* Record WAV file to SD Card

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include "audio_config.h"

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
#include "mbedtls/base64.h"

#include "esp_http_client.h"
#include "sdkconfig.h"
#include "audio_element.h"
#include "audio_pipeline.h"
#include "audio_event_iface.h"
#include "audio_common.h"
#include "http_stream.h"
#include "i2s_stream.h"
#include "raw_stream.h"
#include "wav_encoder.h"
#include "cJSON.h"


#include "periph_wifi.h"
#include "filter_resample.h"
#include "input_key_service.h"
#include "audio_idf_version.h"
#include "esp_websocket_client.h"
#if (ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(4, 1, 0))

#include "esp_netif.h"

#else
#include "tcpip_adapter.h"
#endif

// Custom include
#include <esp_spiffs.h>

#include "i2s_ics43434_init.h"
#include "http_stream_handler.h"
#include "vad_stream.h"
#include "filter_resample.h"
#include "openai_send_stream.h"


static const char *TAG = "MAIN";


#define OUT_RINGBUF_SIZE (20 * 1024)

#define DEMO_EXIT_BIT (BIT0)


/* Audio */

/* Can be used for i2s_std_gpio_config_t and/or i2s_std_config_t initialization */

static EventGroupHandle_t EXIT_FLAG;
static i2s_chan_handle_t i2s_tx_chan = NULL;
static i2s_chan_handle_t i2s_rx_chan = NULL;
static audio_pipeline_handle_t pipeline_rec, pipeline_speaker;
static audio_element_handle_t i2s_stream_reader, http_stream_writer,  raw_stream_writer,
        i2s_stream_writer, vad_stream_processor , filter_processor, openai_send_processor;




#define VAD_SAMPLE_RATE_HZ AUDIO_SAMPLE_RATE
#define VAD_FRAME_LENGTH_MS 30
#define VAD_BUFFER_LENGTH (VAD_FRAME_LENGTH_MS * VAD_SAMPLE_RATE_HZ / 1000)

#define SPEECH_DETECTED_BIT BIT0
static EventGroupHandle_t vad_event_group;

static bool websocket_connected = false;


bool init_audio_pipeline(esp_websocket_client_handle_t client) {
    // Initialize Streams ****************************************************************************************


    ESP_LOGI(TAG, "[2.1] Create i2s stream to read audio data from codec chip");

    // i2s Stream
    i2s_stream_cfg_t i2s_cfg = {
        .type = AUDIO_STREAM_READER,
        .std_cfg = BSP_I2S_LEFT_MONO_CFG(AUDIO_SAMPLE_RATE),
        .chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER),
        .task_stack = I2S_STREAM_TASK_STACK,
        .task_core = I2S_STREAM_TASK_CORE,
        .task_prio = I2S_STREAM_TASK_PRIO,
    };
    i2s_cfg.type = AUDIO_STREAM_READER;
    i2s_cfg.out_rb_size = OUT_RINGBUF_SIZE;
    i2s_stream_reader = i2s_stream_init(&i2s_cfg);

    i2s_cfg.type = AUDIO_STREAM_WRITER;
    i2s_stream_writer = i2s_stream_init(&i2s_cfg);


    // http streamn
    ESP_LOGI(TAG, "[2.2] Create http stream to post data to server");
    http_stream_cfg_t http_cfg = HTTP_STREAM_CFG_DEFAULT();
    http_cfg.type = AUDIO_STREAM_WRITER;
    http_cfg.event_handle = _http_stream_event_handle;
    http_stream_writer = http_stream_init(&http_cfg);
    audio_element_set_uri(http_stream_writer, CONFIG_SERVER_URI);

    // vad stream
    ESP_LOGI(TAG, "[2.3] Create vad stream ");
    audio_element_cfg_t vad_cfg = DEFAULT_AUDIO_ELEMENT_CONFIG();
    vad_cfg.task_stack = 8192; // or 6144, 8192, etc.
    // adjust ring buffer size
    vad_cfg.out_rb_size = OUT_RINGBUF_SIZE;

    vad_stream_processor = vad_stream_init(&vad_cfg);

    // raw streams
    ESP_LOGI(TAG, "[2.3] Create raw stream ");
    raw_stream_cfg_t raw_cfg_writer = RAW_STREAM_CFG_DEFAULT();
    raw_cfg_writer.out_rb_size = 8 * 1024;
    raw_cfg_writer.type = AUDIO_STREAM_WRITER;
    raw_stream_writer = raw_stream_init(&raw_cfg_writer);

    // open_ai stream (sending)
    openai_send_stream_t_cfg openai_send_stream_cfg;
    openai_send_stream_cfg.client = client;
    openai_send_stream_cfg.task_stack = 8192;
//    openai_send_stream_cfg.input_wait_time=300 / portTICK_RATE_MS;
   openai_send_stream_cfg.input_wait_time=  portMAX_DELAY;

    // buffer length for 100 ms
    openai_send_stream_cfg.frame_to_openai_length = (16000 * 2 * 100) / 1000; // 16 kHz, 16 bit, 100 ms
    openai_send_processor=openai_send_stream_init(&openai_send_stream_cfg);


    // filter resample
    rsp_filter_cfg_t rsp_cfg_down = DEFAULT_RESAMPLE_FILTER_CONFIG();
    rsp_cfg_down.src_ch = 1;
    rsp_cfg_down.dest_ch = 1;
    rsp_cfg_down.src_rate = AUDIO_SAMPLE_RATE;
    rsp_cfg_down.dest_rate = 24000;
    audio_element_handle_t filter_processor_upsample = rsp_filter_init(&rsp_cfg_down);

    rsp_filter_cfg_t rsp_cfg_up = DEFAULT_RESAMPLE_FILTER_CONFIG();
    rsp_cfg_up.src_ch = 1;
    rsp_cfg_up.dest_ch = 1;
    rsp_cfg_up.src_rate = 24000;
    rsp_cfg_up.dest_rate = AUDIO_SAMPLE_RATE;
    audio_element_handle_t filter_processor_down = rsp_filter_init(&rsp_cfg_down);


    // Initialize and run Pipeline ****************************************************************************************
    ESP_LOGI(TAG, "[ 3.0 ] Create audio pipelines for recording");
    audio_pipeline_cfg_t pipeline_cfg = DEFAULT_AUDIO_PIPELINE_CONFIG();
    pipeline_rec = audio_pipeline_init(&pipeline_cfg);
    mem_assert(pipeline_rec);
    audio_pipeline_register(pipeline_rec, i2s_stream_reader, "i2s_rec");
    audio_pipeline_register(pipeline_rec, vad_stream_processor, "vad_rec");
//    audio_pipeline_register(pipeline_rec, http_stream_writer, "http");
    audio_pipeline_register(pipeline_rec, filter_processor_upsample, "filter_24000");
    audio_pipeline_register(pipeline_rec, openai_send_processor, "openai_send_rec");


    ESP_LOGI(TAG, "[ 3.1 ] Link elements together [codec_chip]-->i2s_stream-->vad-->openai_send_rec");
    const char *link_tag[4] = {"i2s_rec", "vad_rec", "filter_24000" ,"openai_send_rec"};
    audio_pipeline_link(pipeline_rec, &link_tag[0], 4);

    ESP_LOGI(TAG, "[ 3.2 ] Create audio pipelines for speaker");
    pipeline_speaker = audio_pipeline_init(&pipeline_cfg);
    mem_assert(pipeline_speaker);
    audio_pipeline_register(pipeline_speaker, raw_stream_writer, "raw_speaker");
    audio_pipeline_register(pipeline_speaker, filter_processor_down, "filter_16000_speaker");
    audio_pipeline_register(pipeline_speaker, i2s_stream_writer, "i2s_speaker");

    ESP_LOGI(TAG, "[ 3.3 ] Link elements together [raw-->i2s_stream]");
    char *link_tag2[3] = {"raw_speaker", "filter_16000_speaker", "i2s_speaker"};
    audio_pipeline_link(pipeline_speaker, &link_tag2[0], 3);

    ESP_LOGI(TAG, "[ 3.5 ] Start audio_pipelines");
    audio_pipeline_run(pipeline_rec);
    audio_pipeline_run(pipeline_speaker);

    return false;
}

static void log_error_if_nonzero(const char *message, int error_code) {
    if (error_code != 0) {
        ESP_LOGE(TAG, "Last  error %s: 0x%x", message, error_code);
    }
}


void send_session_update(esp_websocket_client_handle_t client) {
    if (!client || !esp_websocket_client_is_connected(client)) {
        ESP_LOGE(TAG, "WebSocket client not connected");
        return;
    }

    cJSON *root = cJSON_CreateObject();
    //    cJSON_AddStringToObject(root, "event_id", "event_123");
    cJSON_AddStringToObject(root, "type", "session.update");

    cJSON *session = cJSON_CreateObject();
    cJSON_AddItemToObject(root, "session", session);

    cJSON *modalities = cJSON_CreateStringArray((const char *[]){"text", "audio"}, 2);
    cJSON_AddItemToObject(session, "modalities", modalities);

    cJSON_AddStringToObject(session, "instructions", "Wiederhole den genannten text wordgenau..");
    cJSON_AddStringToObject(session, "voice", "sage");
    cJSON_AddStringToObject(session, "input_audio_format", "pcm16");
    cJSON_AddStringToObject(session, "output_audio_format", "pcm16");

    cJSON *input_audio_transcription = cJSON_CreateObject();
    cJSON_AddStringToObject(input_audio_transcription, "model", "whisper-1");
    cJSON_AddItemToObject(session, "input_audio_transcription", input_audio_transcription);

//turn_detection should be null
    cJSON *turn_detection = cJSON_CreateNull();
    cJSON_AddItemToObject(session, "turn_detection", turn_detection);

    /*
    cJSON *turn_detection = cJSON_CreateObject();
    cJSON_AddStringToObject(turn_detection, "type", "server_vad");
    cJSON_AddNumberToObject(turn_detection, "threshold", 0.5);
    cJSON_AddNumberToObject(turn_detection, "prefix_padding_ms", 300);
    cJSON_AddNumberToObject(turn_detection, "silence_duration_ms", 500);
    cJSON_AddBoolToObject(turn_detection, "create_response", true);
    cJSON_AddItemToObject(session, "turn_detection", turn_detection);
    */

    /*
    cJSON *tools = cJSON_CreateArray();
    cJSON *tool = cJSON_CreateObject();
    cJSON_AddStringToObject(tool, "type", "function");
    cJSON_AddStringToObject(tool, "name", "get_weather");
    cJSON_AddStringToObject(tool, "description", "Get the current weather...");

    cJSON *parameters = cJSON_CreateObject();
    cJSON_AddStringToObject(parameters, "type", "object");

    cJSON *properties = cJSON_CreateObject();
    cJSON_AddItemToObject(parameters, "properties", properties);

    cJSON *location = cJSON_CreateObject();
    cJSON_AddStringToObject(location, "type", "string");
    cJSON_AddItemToObject(properties, "location", location);

    cJSON *required = cJSON_CreateStringArray((const char *[]){"location"}, 1);
    cJSON_AddItemToObject(parameters, "required", required);

    cJSON_AddItemToObject(tool, "parameters", parameters);
    cJSON_AddItemToArray(tools, tool);
    cJSON_AddItemToObject(session, "tools", tools);
    */

    /*cJSON_AddStringToObject(session, "tool_choice", "auto"); */
    cJSON_AddNumberToObject(session, "temperature", 0.6); // WAS 0.8
    cJSON_AddStringToObject(session, "max_response_output_tokens", "inf");

    char *json_payload = cJSON_PrintUnformatted(root);
    if (json_payload) {
        int length = esp_websocket_client_send_text(client, json_payload, strlen(json_payload), portMAX_DELAY);
        ESP_LOGI(TAG, "Session update sent: %d bytes", length);
        free(json_payload);
    } else {
        ESP_LOGE(TAG, "Failed to create JSON payload");
    }

    cJSON_Delete(root);
}

static void websocket_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data) {
    esp_websocket_event_data_t *data = (esp_websocket_event_data_t *) event_data;
    switch (event_id) {
        case WEBSOCKET_EVENT_BEGIN:
            ESP_LOGI(TAG, "WEBSOCKET_EVENT_BEGIN");
            break;
        case WEBSOCKET_EVENT_CONNECTED:
            ESP_LOGI(TAG, "WEBSOCKET_EVENT_CONNECTED");
            websocket_connected = true;
            break;
        case WEBSOCKET_EVENT_DISCONNECTED:
            ESP_LOGI(TAG, "WEBSOCKET_EVENT_DISCONNECTED");
            websocket_connected = false;
            log_error_if_nonzero("HTTP status code", data->error_handle.esp_ws_handshake_status_code);
            if (data->error_handle.error_type == WEBSOCKET_ERROR_TYPE_TCP_TRANSPORT) {
                log_error_if_nonzero("reported from esp-tls", data->error_handle.esp_tls_last_esp_err);
                log_error_if_nonzero("reported from tls stack", data->error_handle.esp_tls_stack_err);
                log_error_if_nonzero("captured as transport's socket errno",
                                     data->error_handle.esp_transport_sock_errno);
            }
            break;

        // Process the received data *******************************
        case WEBSOCKET_EVENT_DATA: {
            TAG = "WEBSOCKET_EVENT_DATA";
            static char *message_buffer = NULL;
            static size_t message_length = 0;

            ESP_LOGI(TAG, "WEBSOCKET_EVENT_DATA");
            ESP_LOGI(TAG, "Received opcode=%d", data->op_code);

            // Allocate or reallocate buffer to store the message
            if (data->payload_offset == 0) {
                if (message_buffer != NULL) {
                    free(message_buffer);
                }
                message_buffer = malloc(data->payload_len + 1);
                message_length = 0;
            } else {
                message_buffer = realloc(message_buffer, message_length + data->data_len + 1);
                // Check if reallocation was successful
                if (message_buffer == NULL) {
                    ESP_LOGE(TAG, "Failed to reallocate message buffer");
                    break;
                }
            }

            // Copy received data to the buffer
            memcpy(message_buffer + message_length, data->data_ptr, data->data_len);
            message_length += data->data_len;
            message_buffer[message_length] = '\0';

            // Check if the message is complete
            if (data->payload_offset + data->data_len == data->payload_len) {
                // Process the complete message
                cJSON *json = cJSON_Parse(message_buffer);

                if (json == NULL) {
                    ESP_LOGE(TAG, "Failed to parse JSON");
                } else {
                    // Handle the JSON message

                    // print the json if message is longer than 80 bytes
                    if (message_length > 500) {
                        ESP_LOGI(TAG, "JSON message: %.*s", 400, cJSON_Print(json));
                    } else {
                        ESP_LOGI(TAG, "JSON message: %s", cJSON_Print(json));
                    }

                    cJSON *type = cJSON_GetObjectItem(json, "type");
                    if (cJSON_IsString(type) && (strcmp(type->valuestring, "response.audio.delta") == 0)) {
                        cJSON *audio = cJSON_GetObjectItem(json, "delta");
                        if (cJSON_IsString(audio)) {
                            size_t decoded_len = 0;
                            size_t audio_len = strlen(audio->valuestring);
                            uint8_t *decoded_audio = malloc(audio_len);
                            if (decoded_audio == NULL) {
                                ESP_LOGE(TAG, "Failed to allocate memory for decoded audio");
                                cJSON_Delete(json);
                                break;
                            }

                            int ret = mbedtls_base64_decode(decoded_audio, audio_len, &decoded_len,
                                                            (const unsigned char *) audio->valuestring, audio_len);
                            if (ret != 0) {
                                ESP_LOGE(TAG, "Base64 decoding failed with error code: %d", ret);
                                free(decoded_audio);
                                cJSON_Delete(json);
                                break;
                            }

                            ESP_LOGI(TAG, "Decoded audio length sending to Audio stream: %d", decoded_len);
                            // print first 10 bytes of decoded audio
                            ESP_LOGI(TAG, "Decoded audio: %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x",
                                     decoded_audio[0], decoded_audio[1], decoded_audio[2], decoded_audio[3],
                                     decoded_audio[4], decoded_audio[5], decoded_audio[6], decoded_audio[7],
                                     decoded_audio[8], decoded_audio[9]);

                            raw_stream_write(raw_stream_writer, (char *) decoded_audio, decoded_len);
                            free(decoded_audio);
                        }
                    }

                    if (cJSON_IsString(type) && (strcmp(type->valuestring, "response.audio_transcript.delta") == 0)) {
                        //print text stored in delta tag
                        cJSON *delta = cJSON_GetObjectItem(json, "delta");
                        if (cJSON_IsString(delta)) {
                            ESP_LOGI(TAG, "!!!!!!!!!!! Transcript: %s", delta->valuestring);
                        } else {
                            ESP_LOGE(TAG, "Failed to get transcript from JSON");
                        }
                    }
                    cJSON_Delete(json);
                }
            } else {
                ESP_LOGI(TAG, "Partial message received: %s", message_buffer);
            }

            // Free the buffer after processing
            free(message_buffer);
            message_buffer = NULL;
            message_length = 0;
        }
        break;
        case WEBSOCKET_EVENT_ERROR:
            ESP_LOGI(TAG, "WEBSOCKET_EVENT_ERROR");
            log_error_if_nonzero("HTTP status code", data->error_handle.esp_ws_handshake_status_code);
            if (data->error_handle.error_type == WEBSOCKET_ERROR_TYPE_TCP_TRANSPORT) {
                log_error_if_nonzero("reported from esp-tls", data->error_handle.esp_tls_last_esp_err);
                log_error_if_nonzero("reported from tls stack", data->error_handle.esp_tls_stack_err);
                log_error_if_nonzero("captured as transport's socket errno",
                                     data->error_handle.esp_transport_sock_errno);
            }
            break;
        case WEBSOCKET_EVENT_FINISH:
            ESP_LOGI(TAG, "WEBSOCKET_EVENT_FINISH");
            break;
    }
}


esp_websocket_client_handle_t init_openai_app_start(void) {

    esp_websocket_client_handle_t client;

    // Mount SPIFFS before using it
    FILE *f = fopen("/spiffs/openai.pem", "r");
    if (f == NULL) {
        ESP_LOGE(TAG, "Failed to open file for reading");
        return ESP_FAIL;
    }

    // Read the PEM file content
    fseek(f, 0, SEEK_END);
    long pem_size = ftell(f);
    fseek(f, 0, SEEK_SET);

    char *pem_data = malloc(pem_size + 1);
    fread(pem_data, 1, pem_size, f);
    fclose(f);
    pem_data[pem_size] = '\0';

    ESP_LOGI(TAG, "Read PEM file: %s", pem_data);

    // to get the certificate use firefox and dowload to *root* certificate chain from the server
    /*
     *             .headers = {
                    {"Authorization", "Bearer " OPENAI_API_KEY},
                    {"OpenAI-Beta", "realtime=v1"},
            },
     */


// #define DEUBUG_OPENAI_COM
#if defined(DEUBUG_OPENAI_COM)

    esp_websocket_client_config_t websocket_cfg = {
        .uri="ws://192.168.1.100:8765",
        .headers =
        "Authorization: Bearer " CONFIG_OPENAI_API_KEY "\r\n"
        "OpenAI-Beta: realtime=v1\r\n",
        .transport = WEBSOCKET_TRANSPORT_OVER_TCP,
        .buffer_size = 1024 * 35, // Set the buffer size to 1024 bytes

    };
    ESP_LOGI(TAG, "!! DEBUG Connecting to %s...", websocket_cfg.uri);

#else
    esp_websocket_client_config_t websocket_cfg = {
        .uri = CONFIG_OPENAI_REALTIME_WEBSOCKET_URI,
        .headers =
        "Authorization: Bearer " CONFIG_OPENAI_API_KEY "\r\n"
        "OpenAI-Beta: realtime=v1\r\n",
        .transport = WEBSOCKET_TRANSPORT_OVER_SSL,
        .cert_pem = pem_data,
        .buffer_size = 1024 * 35, // Set the buffer size to 1024 bytes

    };
    ESP_LOGI(TAG, "Connecting to %s...", websocket_cfg.uri);

#endif

    client = esp_websocket_client_init(&websocket_cfg);
    esp_websocket_register_events(client, WEBSOCKET_EVENT_ANY, websocket_event_handler, (void *) client);
    esp_websocket_client_start(client);
    // Wait for the connection to be established
    while (!websocket_connected) {
        vTaskDelay(100 / portTICK_PERIOD_MS);
    }
    printf("Client: %p\n", client);
    printf("Connected in open_api_start: %d\n", esp_websocket_client_is_connected(client));

    return client;
}



void board_init(void) {
    ESP_ERROR_CHECK(esp_netif_init());

    ESP_LOGI(TAG, "Initializing SPIFFS");

    esp_vfs_spiffs_conf_t conf = {
        .base_path = "/spiffs",
        .partition_label = NULL,
        .max_files = 5,
        .format_if_mount_failed = true
    };
    esp_err_t ret = esp_vfs_spiffs_register(&conf);

    if (ret != ESP_OK) {
        if (ret == ESP_FAIL) {
            ESP_LOGE(TAG, "Failed to mount or format filesystem");
        } else if (ret == ESP_ERR_NOT_FOUND) {
            ESP_LOGE(TAG, "Failed to find SPIFFS partition");
        } else {
            ESP_LOGE(TAG, "Failed to initialize SPIFFS (%s)", esp_err_to_name(ret));
        }
        return;
    }

    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }

    EXIT_FLAG = xEventGroupCreate();

    ESP_LOGI(TAG, "[ 1.1 ] Initialize Peripheral & Connect to wifi network");
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
}

void app_main(void) {

    bool http_only = false;
    static esp_websocket_client_handle_t client = NULL;

    // Initialize Board  ****************************************************************************************

    ESP_LOGI(TAG, "*** Welcome ***");
    esp_log_level_set("*", ESP_LOG_DEBUG);
    //    esp_log_level_set("*", ESP_LOG_VERBOSE);
    ESP_LOGI(TAG, "[ 1.0 ] Setup peripherals and spiffs");

    board_init();

    // Init OpenAI Websocket  ****************************************************************************************
    ESP_LOGI(TAG, "[ 1.2 ] Initialize OpenAI Websocket");
    if (!http_only) {
        client=init_openai_app_start();
        //printf("Connected-1: %d\n", esp_websocket_client_is_connected(client));
        ESP_LOGI(TAG, "[ 1.3 ] Update Session Object");
        send_session_update(client);
    }
    // Audio Pipeline  ****************************************************************************************

    ESP_LOGI(TAG, "[ 2.0 ] Init Audio Pipeline");
    init_audio_pipeline(client);

    // Event Interface ****************************************************************************************
    audio_event_iface_cfg_t evt_cfg = AUDIO_EVENT_IFACE_DEFAULT_CFG();
    audio_event_iface_handle_t evt = audio_event_iface_init(&evt_cfg);
    audio_pipeline_set_listener(pipeline_rec, evt);
    audio_pipeline_set_listener(pipeline_speaker, evt);


    // Loop ****************************************************************************************

    bool stop_loop_after_one_iteration = false;
    bool stop_loop_now = false;

    ESP_LOGI(TAG, "[ 3.0 ] Main Loop ******************************************************************************");

    while (true) {
        audio_event_iface_msg_t msg;
        if (audio_event_iface_listen(evt, &msg, portMAX_DELAY) == ESP_OK) {
            if (msg.source_type == AUDIO_ELEMENT_TYPE_ELEMENT &&
                msg.cmd == AEL_MSG_CMD_REPORT_STATUS) {
                switch ((int) msg.data) {
                    case VAD_STREAM_EVENT_SPEECH_START:
                        ESP_LOGI(TAG, "========================= Speech Detected =================");
                        break;

                    case VAD_STREAM_EVENT_SPEECH_STOP:
                        ESP_LOGI(TAG, "========================= Speech Stopped ==================");

                    if (!stop_loop_now) {
                        if (!http_only) {
                            // Commit buffer
                            ESP_LOGI(TAG, "******* Committing buffer******");
                            const char *commit_payload = "{\"type\": \"input_audio_buffer.commit\"}";
                            esp_websocket_client_send_text(client, commit_payload, strlen(commit_payload),
                                                           portMAX_DELAY);

                            // Request response
                            const char *response_payload = "{\"type\": \"response.create\"}";
                            esp_websocket_client_send_text(client, response_payload, strlen(response_payload),
                                                           portMAX_DELAY);

                            if (stop_loop_after_one_iteration) {
                                stop_loop_now = true;
                            }
                        }
                    } else {
                        ESP_LOGI(TAG, "Stopping loop");
                    }
                        break;
                    default:
                        break;
                }
            }
        }
        // Send audio chunks over WebSocket

    }

    ESP_LOGI(TAG, "[ 8 ] Stop audio_pipeline and release all resources");
    audio_pipeline_stop(pipeline_rec);
    audio_pipeline_wait_for_stop(pipeline_rec);
    audio_pipeline_terminate(pipeline_rec);

    audio_pipeline_stop(pipeline_speaker);
    audio_pipeline_wait_for_stop(pipeline_speaker);
    audio_pipeline_terminate(pipeline_speaker);


    // unregister all elements
    audio_pipeline_unregister(pipeline_rec, i2s_stream_reader);
    audio_pipeline_unregister(pipeline_rec, vad_stream_processor);
    audio_pipeline_unregister(pipeline_rec, http_stream_writer);
    audio_pipeline_unregister(pipeline_rec, openai_send_processor);
    audio_pipeline_unregister(pipeline_speaker, i2s_stream_writer);


    /* Terminate the pipeline before removing the listener */
    audio_pipeline_remove_listener(pipeline_rec);
    audio_pipeline_remove_listener(pipeline_speaker);

    /* Release all resources */
    audio_pipeline_deinit(pipeline_rec);
    audio_pipeline_deinit(pipeline_speaker);
    audio_element_deinit(http_stream_writer);
    audio_element_deinit(i2s_stream_reader);
    audio_element_deinit(vad_stream_processor);
    audio_element_deinit(openai_send_processor);
    audio_element_deinit(i2s_stream_writer);
}
