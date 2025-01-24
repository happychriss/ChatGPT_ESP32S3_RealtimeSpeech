//
// Created by development on 20.01.25.
//

#ifndef OPENAI_SEND_STREAM_H
#define OPENAI_SEND_STREAM_H

#include "audio_error.h"
#include "audio_element.h"
#include "audio_common.h"
#include "esp_websocket_client.h"

typedef struct {
    int frame_to_openai_length;
    esp_websocket_client_handle_t client;
    int task_stack;
    TickType_t input_wait_time;

} openai_send_stream_t_cfg;

audio_element_handle_t openai_send_stream_init(openai_send_stream_t_cfg *config);

#endif //OPENAI_SEND_STREAM_H
