/*
 * audio_hal.c - ES8311 + I2S, lifted from PELLETINO's audio_hal.cpp with the Namco
 * synth removed: the NES core renders the samples, this only carries them to the DAC.
 */
#include "audio_hal.h"
#include "driver/i2s_std.h"
#include "driver/i2c.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "AUDIO";

static i2s_chan_handle_t i2s_tx;
static volatile uint32_t underruns;
static volatile uint32_t bytes_sent;     /* advanced by the DMA ISR */
static uint32_t bytes_written;           /* advanced by audio_submit */
static bool muted;

#define ES8311_REG_RESET    0x00
#define ES8311_REG_SDPOUT   0x09
#define ES8311_REG_SDPIN    0x0A
#define ES8311_REG_SYS_CTRL 0x0D
#define ES8311_REG_ADC_VOL  0x17
#define ES8311_REG_DAC_VOL  0x32

static esp_err_t es8311_write_reg(uint8_t reg, uint8_t value)
{
    uint8_t data[2] = { reg, value };
    return i2c_master_write_to_device(I2C_NUM_0, ES8311_ADDR, data, 2, pdMS_TO_TICKS(100));
}

static void es8311_init(void)
{
    i2c_config_t c = {
        .mode = I2C_MODE_MASTER, .sda_io_num = PIN_I2C_SDA, .scl_io_num = PIN_I2C_SCL,
        .sda_pullup_en = GPIO_PULLUP_ENABLE, .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = 100000,
    };
    i2c_param_config(I2C_NUM_0, &c);
    esp_err_t r = i2c_driver_install(I2C_NUM_0, I2C_MODE_MASTER, 0, 0, 0);
    if (r != ESP_OK && r != ESP_ERR_INVALID_STATE) ESP_LOGE(TAG, "i2c install: %s", esp_err_to_name(r));

    /* register sequence proven on this board in PELLETINO */
    es8311_write_reg(ES8311_REG_RESET, 0x3F); vTaskDelay(pdMS_TO_TICKS(20));
    es8311_write_reg(ES8311_REG_RESET, 0x00); vTaskDelay(pdMS_TO_TICKS(20));
    es8311_write_reg(0x01, 0x3F); es8311_write_reg(0x02, 0x00); es8311_write_reg(0x03, 0x10);
    es8311_write_reg(0x04, 0x10); es8311_write_reg(0x05, 0x00); es8311_write_reg(0x06, 0x03);
    es8311_write_reg(0x07, 0x00); es8311_write_reg(0x08, 0xFF);
    es8311_write_reg(ES8311_REG_SDPOUT, 0x0C);   /* 16-bit, Philips I2S */
    es8311_write_reg(ES8311_REG_SDPIN, 0x0C);
    es8311_write_reg(ES8311_REG_SYS_CTRL, 0x00);
    es8311_write_reg(0x0E, 0x02); es8311_write_reg(0x0F, 0x44);
    es8311_write_reg(0x10, 0x0C); es8311_write_reg(0x11, 0x00);
    es8311_write_reg(0x12, 0x00); es8311_write_reg(0x13, 0x10); es8311_write_reg(0x14, 0x10);
    es8311_write_reg(ES8311_REG_DAC_VOL, 0xBF);
    es8311_write_reg(ES8311_REG_ADC_VOL, 0xBF);
    es8311_write_reg(0x00, 0x80);
    es8311_write_reg(0x01, 0x3F);
}

static bool IRAM_ATTR on_sent(i2s_chan_handle_t h, i2s_event_data_t *e, void *ctx)
{
    bytes_sent += e->size;
    return false;
}

static bool IRAM_ATTR on_underrun(i2s_chan_handle_t h, i2s_event_data_t *e, void *ctx)
{
    underruns++;
    return false;
}

void audio_init(void)
{
    es8311_init();

    i2s_chan_config_t chan = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    chan.dma_desc_num = AUDIO_DMA_BUFFERS;
    chan.dma_frame_num = AUDIO_DMA_FRAME_NUM;
    chan.auto_clear = true;   /* silence, not a stale buffer, on underrun */
    ESP_ERROR_CHECK(i2s_new_channel(&chan, &i2s_tx, NULL));
    i2s_event_callbacks_t cbs = { .on_sent = on_sent, .on_send_q_ovf = on_underrun };
    ESP_ERROR_CHECK(i2s_channel_register_event_callback(i2s_tx, &cbs, NULL));

    i2s_std_config_t std = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(AUDIO_SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = { .mclk = PIN_I2S_MCK, .bclk = PIN_I2S_BCK, .ws = PIN_I2S_LRCK,
                      .dout = PIN_I2S_DOUT, .din = PIN_I2S_DIN },
    };
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(i2s_tx, &std));
    ESP_LOGI(TAG, "ES8311 + I2S at %d Hz", AUDIO_SAMPLE_RATE);
}

/* The channel starts on the first submit, primed with ~3 descriptors of silence: no
 * underruns while the game loads, and headroom for a slow frame (BLE, cache miss). */
static void audio_start(void)
{
    static const int16_t silence[AUDIO_DMA_FRAME_NUM * 3];
    size_t loaded = 0;
    i2s_channel_preload_data(i2s_tx, silence, sizeof silence, &loaded);
    bytes_written = loaded;
    ESP_ERROR_CHECK(i2s_channel_enable(i2s_tx));
}

bool audio_submit(const int16_t *samples, size_t n)
{
    static bool started;
    if (!i2s_tx) return false;
    if (!started) { audio_start(); started = true; }
    static const int16_t silence[AUDIO_DMA_FRAME_NUM];
    size_t written = 0;
    const void *src = samples;
    if (muted) { src = silence; if (n > AUDIO_DMA_FRAME_NUM) n = AUDIO_DMA_FRAME_NUM; }
    /* The DMA ring plays whether or not we refilled it (underrun = silence), and the ISR
     * counts everything played. If it got ahead of us, resync so 'queued' is real again. */
    if ((int32_t)(bytes_written - bytes_sent) < 0) bytes_written = bytes_sent;
    esp_err_t r = i2s_channel_write(i2s_tx, src, n * sizeof(int16_t), &written, portMAX_DELAY);
    bytes_written += written;
    return r == ESP_OK;
}

int audio_queued_samples(void)
{
    int32_t q = (int32_t)(bytes_written - bytes_sent) / (int32_t)sizeof(int16_t);
    return q < 0 ? 0 : q;
}

uint32_t audio_get_underrun_count(void) { return underruns; }
void audio_set_volume(uint8_t v) { es8311_write_reg(ES8311_REG_DAC_VOL, v); }
void audio_set_mute(bool m) { muted = m; }
