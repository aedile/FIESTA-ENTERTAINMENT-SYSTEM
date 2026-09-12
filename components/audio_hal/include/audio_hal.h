/*
 * audio_hal.h - ES8311 codec + I2S DMA output, from PELLETINO.
 *
 * The NES APU produces one frame of samples per emulated frame; audio_submit()
 * hands them to the I2S DMA queue and blocks (on the driver's semaphore, not a
 * spin) while the queue is full. That block is what paces emulation to the
 * DAC clock: 20050 Hz / 334 samples per frame = 60.03 fps.
 */
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define AUDIO_SAMPLE_RATE   20050
#define AUDIO_DMA_FRAME_NUM 256   /* samples per DMA descriptor */
#define AUDIO_DMA_BUFFERS   5     /* 5 x 256 = 64 ms of queue: that is also the max audio lag */

/* I2S / I2C pins (Waveshare ESP32-C6-LCD-1.69) */
#define PIN_I2S_MCK  GPIO_NUM_19
#define PIN_I2S_BCK  GPIO_NUM_20
#define PIN_I2S_LRCK GPIO_NUM_22
#define PIN_I2S_DOUT GPIO_NUM_23
#define PIN_I2S_DIN  GPIO_NUM_21
#define PIN_I2C_SDA  GPIO_NUM_8
#define PIN_I2C_SCL  GPIO_NUM_7
#define ES8311_ADDR  0x18

void audio_init(void);
/* Queue n mono int16 samples. Blocks until they fit; returns false on driver error. */
bool audio_submit(const int16_t *samples, size_t n);
uint32_t audio_get_underrun_count(void);   /* DMA queue ran dry (emulator too slow) */
int audio_queued_samples(void);            /* written but not yet played: 0 .. AUDIO_DMA_BUFFERS*AUDIO_DMA_FRAME_NUM */
void audio_set_volume(uint8_t volume);     /* ES8311 DAC volume register, 0-255 */
void audio_set_mute(bool muted);

#ifdef __cplusplus
}
#endif
