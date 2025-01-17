# Components

The components audio_board and audio_stream are copied from esp-adf component directory and adjusted for ESP Firebeatle esp32s3 board.

This includes:
* audio_board: Pin configuration for Firebeetle ESP32S3 board

* audio_stream: Adjusted i2s read function for the ics 43434 microphone

* adjusted ringbuffer size for i2s stream in i2s_stream.h 
  #define I2S_STREAM_TASK_STACK           (3584)
  #define I2S_STREAM_BUF_SIZE             (4800)
  #define I2S_STREAM_TASK_PRIO            (23)
  #define I2S_STREAM_TASK_CORE            (1)
  #define I2S_STREAM_RINGBUFFER_SIZE      (45 * 1024)

* adjusted ringbuffer size on http_stream.h
  #define HTTP_STREAM_TASK_STACK          (6 * 1024)
  #define HTTP_STREAM_TASK_CORE           (0)
  #define HTTP_STREAM_TASK_PRIO           (4)
  #define HTTP_STREAM_RINGBUFFER_SIZE     (24 * 1024)
```