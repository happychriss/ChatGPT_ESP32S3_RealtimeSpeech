set(COMPONENT_SRCS  "raw_stream.c"
                    "vad_stream.c"
                    "i2s_stream_idf5_ics43434.c"


)
set(COMPONENT_ADD_INCLUDEDIRS "include")

set(COMPONENT_PRIV_INCLUDEDIRS "lib/hls/include" "lib/gzip/include")
list(APPEND COMPONENT_SRCS  "lib/hls/hls_parse.c"
                            "lib/hls/hls_playlist.c"
                            "lib/hls/line_reader.c"
                            "lib/hls/join_path.c")

list(APPEND COMPONENT_SRCS  "lib/gzip/gzip_miniz.c")

set(COMPONENT_REQUIRES audio_pipeline driver audio_sal esp_http_client tcp_transport spiffs audio_board esp-adf-libs bootloader_support esp_dispatcher esp_actions tone_partition mbedtls esp-sr)

register_component()
