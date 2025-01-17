//
// Created by development on 11.01.25.
//

#ifndef RECORD_RAW_HTTP_I2S_ICS43434_INIT_H
#define RECORD_RAW_HTTP_I2S_ICS43434_INIT_H

#include "esp_netif.h"
#include "audio_idf_version.h"
#include "input_key_service.h"
#include "filter_resample.h"
#include "periph_wifi.h"
#include "wav_encoder.h"
#include "i2s_stream.h"
#include "http_stream.h"
#include "audio_common.h"
#include "audio_event_iface.h"
#include "audio_pipeline.h"
#include "audio_element.h"
#include "sdkconfig.h"
#include "esp_http_client.h"
#include "nvs_flash.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include "freertos/task.h"
#include "freertos/FreeRTOS.h"
#include <string.h>


#define DEMO_EXIT_BIT (BIT0)
#define DEMO_EXIT_BIT (BIT0)
#define BSP_I2S_SCLK          (GPIO_NUM_17) // serial clock
#define BSP_I2S_MCLK          (GPIO_NUM_2)
#define BSP_I2S_LCLK          (GPIO_NUM_47)// left right clock
#define BSP_I2S_DOUT          (GPIO_NUM_15) // To Codec ES8311
#define BSP_I2S_DSIN          (GPIO_NUM_16) // From ICS43434
#define BSP_POWER_AMP_IO      (GPIO_NUM_46)
#define BSP_I2S_GPIO_CFG       \
    {                          \
        .mclk = I2S_GPIO_UNUSED,   \
        .bclk = BSP_I2S_SCLK,  \
        .ws = BSP_I2S_LCLK,    \
        .dout = I2S_GPIO_UNUSED,  \
        .din = BSP_I2S_DSIN,   \
        .invert_flags = {      \
            .mclk_inv = false, \
            .bclk_inv = false, \
            .ws_inv = false,   \
        }                      \
}
#define I2S_STD_PHILIPS_SINGLE_SLOT_DEFAULT_CONFIG(bits_per_sample, mono_or_stereo) { \
    .data_bit_width = bits_per_sample, \
    .slot_bit_width = I2S_SLOT_BIT_WIDTH_AUTO, \
    .slot_mode = mono_or_stereo, \
    .slot_mask = I2S_STD_SLOT_LEFT, \
    .ws_width = bits_per_sample, \
    .ws_pol = false, \
    .bit_shift = true, \
    .left_align = true, \
    .big_endian = false, \
    .bit_order_lsb = false \
}
#define BSP_I2S_LEFT_MONO_CFG(_sample_rate)                                                         \
    {                                                                                                 \
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(_sample_rate),                                          \
        .slot_cfg = I2S_STD_PHILIPS_SINGLE_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_MONO),   \
        .gpio_cfg = (i2s_std_gpio_config_t)BSP_I2S_GPIO_CFG,                                          \
          }
#endif //RECORD_RAW_HTTP_I2S_ICS43434_INIT_H
