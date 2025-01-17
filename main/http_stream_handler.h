//
// Created by development on 11.01.25.
//

#ifndef RECORD_RAW_HTTP_HTTP_STREAMER_H
#define RECORD_RAW_HTTP_HTTP_STREAMER_H

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

#if (ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(4, 1, 0))
#include "esp_netif.h"
#else
#include "tcpip_adapter.h"
#endif

#include "esp_netif.h"

esp_err_t _http_stream_event_handle(http_stream_event_msg_t *msg);

#endif //RECORD_RAW_HTTP_HTTP_STREAMER_H
