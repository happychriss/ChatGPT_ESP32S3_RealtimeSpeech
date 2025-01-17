//
// Created by development on 13.01.25.
//

#ifndef VAD_STREAM_H
#define VAD_STREAM_H

#include "audio_element.h"

#ifdef __cplusplus
extern "C" {
#endif

    /**
     * Custom event codes for VAD.
     * You can rename these if needed.
     */
#define VAD_STREAM_EVENT_SPEECH_START  (1)
#define VAD_STREAM_EVENT_SPEECH_STOP   (2)

    /**
     * @brief Create a VAD audio element
     *
     * @param config  Standard audio_element_cfg_t
     *
     * @return Handle to the new vad_stream element
     */
    audio_element_handle_t vad_stream_init(audio_element_cfg_t *config);

#ifdef __cplusplus
}
#endif

#endif /* VAD_STREAM_H */
