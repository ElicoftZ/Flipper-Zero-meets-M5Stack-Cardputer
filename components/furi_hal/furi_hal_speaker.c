/**
 * @file furi_hal_speaker.c
 * Speaker HAL — I2S tone generation for boards with speaker hardware,
 * no-op stubs for boards without.
 *
 * Stays close to the STM32 Flipper Zero firmware API:
 *   - Mutex-based exclusive ownership (acquire/release/is_mine)
 *   - start(frequency, volume) / set_volume / stop
 *   - Cubic volume scaling (v^3) for perceptually linear loudness
 *
 * On ESP32 the speaker is driven via I2S (instead of STM32's PWM/TIM16).
 * A background thread continuously writes a sine-wave buffer to the I2S
 * peripheral; frequency and volume changes regenerate the buffer.
 */

#include "furi_hal_speaker.h"
#include "boards/board.h"
#include <furi.h>

#define TAG "FuriHalSpeaker"

#if BOARD_HAS_SPEAKER

#include <driver/i2s_std.h>
#include <driver/gpio.h>
#include <esp_timer.h>

#if defined(BOARD_HAS_ES8311) && BOARD_HAS_ES8311
#include <driver/i2c.h>
#endif

#include <math.h>
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* ---- Configuration ---- */
#define SPEAKER_SAMPLE_RATE   44100
#define SPEAKER_BITS          16
#define SPEAKER_DMA_DESC_NUM  4
#define SPEAKER_DMA_FRAME_NUM 256
#define SPEAKER_THREAD_STACK  2048

/* Maximum number of samples we pre-compute for one sine cycle.
 * For very low frequencies the cycle is long; we cap it to keep
 * memory usage reasonable (~4 KB for stereo 16-bit). */
#define SPEAKER_MAX_CYCLE_SAMPLES 1024

/* GDO-mirror mode: sample-rate and per-write buffer size.
 * 16 kHz sample rate is plenty for 1-bit OOK/FM audio (typical RF audio
 * bandwidth <5 kHz) and keeps CPU cost reasonable. 64-frame buffer ⇒ ~4 ms
 * per write, which sets the pacing for the I2S writer loop. */
#define MIRROR_SAMPLE_RATE 16000
#define MIRROR_BUF_FRAMES  64

typedef enum {
    SpeakerModeIdle,
    SpeakerModeTone,
    SpeakerModeGdoMirror,
} SpeakerMode;

/* ---- Static state ---- */
static FuriMutex* speaker_mutex = NULL;
static i2s_chan_handle_t i2s_tx_handle = NULL;
static bool i2s_channel_enabled = false;

#if defined(BOARD_HAS_MIC) && BOARD_HAS_MIC
/* Full-duplex RX side of the same I2S_NUM_0 channel: reads the ES8311 ADC (mic).
 * Owned here (shared clocks with TX); furi_hal_mic.c drives it via the accessor. */
static i2s_chan_handle_t i2s_rx_handle = NULL;
static bool es8311_adc_inited = false;
#endif

/* Active mode — read by writer thread, written by API callers under ownership. */
static volatile SpeakerMode speaker_mode = SpeakerModeIdle;

/* Tone-mode state */
static volatile float speaker_frequency = 0.0f;
static volatile float speaker_volume = 0.0f;
static volatile bool speaker_buffer_dirty = true;

/* GDO-mirror-mode state */
static volatile gpio_num_t speaker_mirror_pin = (gpio_num_t)-1;
static volatile float speaker_mirror_volume = 0.0f;

/* Background writer thread */
static FuriThread* speaker_thread = NULL;
static volatile bool speaker_thread_run = false;

/* Pre-computed waveform buffer (stereo interleaved: L, R, L, R, ...) */
static int16_t* wave_buffer = NULL;
static size_t wave_buffer_samples = 0; /* number of stereo frames */
static size_t wave_buffer_bytes = 0;

#if defined(BOARD_HAS_ES8311) && BOARD_HAS_ES8311
/* ---- ES8311 codec (Cardputer-ADV) ----
 * Unlike the standard Cardputer's dumb NS4168 I2S amp, the ADV routes audio
 * through an ES8311 codec that must be configured over I2C before it will pass
 * any I2S data to the speaker. Init runs lazily on the first speaker acquire
 * (by then the keyboard has installed the shared I2C bus and the I2S channel —
 * hence MCLK — is enabled). */
static bool es8311_inited = false;

static esp_err_t es8311_write(uint8_t reg, uint8_t val) {
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (BOARD_ES8311_I2C_ADDR << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, reg, true);
    i2c_master_write_byte(cmd, val, true);
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(BOARD_ES8311_I2C_PORT, cmd, pdMS_TO_TICKS(50));
    i2c_cmd_link_delete(cmd);
    return ret;
}

/* Register init for DAC/speaker playback on the Cardputer-ADV. Values follow
 * M5Unified's `_speaker_enabled_cb_cardputer_adv` (m5stack/M5Unified), which is
 * the known-good sequence for THIS board. The critical bit is reg 0x01=0xB5:
 * the ADV routes NO dedicated MCLK to the ES8311, so the codec must derive its
 * master clock from BCLK/SCLK. (The previous 0x01=0x30 made the codec wait for
 * an MCLK pin that doesn't exist here → DAC never clocked → silence.)
 * es8311_seq[0] is a reset with a 20 ms settle before the rest is written. */
static const uint8_t es8311_seq[][2] = {
    {0x00, 0x1F}, /* reset digital + analog (20 ms settle follows) */
    {0x00, 0x80}, /* CSM power on */
    {0x01, 0xB5}, /* clock manager: MCLK sourced from BCLK/SCLK (no MCLK pin) */
    {0x02, 0x18}, /* clock manager: MULT_PRE=3 */
    {0x0D, 0x01}, /* power up analog circuitry */
    {0x12, 0x00}, /* power up DAC */
    {0x13, 0x10}, /* enable output drive (to speaker/HP) */
    {0x32, 0xBF}, /* DAC volume ~0 dB */
    {0x37, 0x08}, /* bypass DAC equalizer */
};

static void es8311_ensure_init(void) {
    if(es8311_inited) return;
    es8311_inited = true;

    esp_err_t first = es8311_write(es8311_seq[0][0], es8311_seq[0][1]); /* reset */
    furi_delay_ms(20);
    unsigned nacks = (first != ESP_OK) ? 1 : 0;
    for(size_t i = 1; i < sizeof(es8311_seq) / sizeof(es8311_seq[0]); i++) {
        if(es8311_write(es8311_seq[i][0], es8311_seq[i][1]) != ESP_OK) nacks++;
    }
    FURI_LOG_I(TAG, "ES8311 init done (addr 0x%02X, %u write errors)",
               BOARD_ES8311_I2C_ADDR, nacks);
}

void furi_hal_speaker_es8311_set_volume_reg(float volume) {
    if(!es8311_inited) return;
    if(volume < 0.0f) volume = 0.0f;
    if(volume > 1.0f) volume = 1.0f;
    es8311_write(0x32, (uint8_t)(volume * 255.0f)); /* 0x00 mute .. 0xFF max */
}

#if defined(BOARD_HAS_MIC) && BOARD_HAS_MIC
/* ES8311 ADC / microphone capture path, layered on top of es8311_ensure_init()
 * (which brings up clocks + power). Values follow the ESP-ADF / ESPHome ES8311
 * record init: power the ADC modulator + analog PGA, select the analog mic
 * input, and set mic gain + ADC digital volume. Tune ES8311_MIC_GAIN / the ADC
 * volume (reg 0x17) if recordings are too quiet or clip. */
#define ES8311_MIC_GAIN 0x07 /* reg 0x16 PGA: 0x00 (min) .. 0x07 (max analog gain) */
#define ES8311_ADC_VOL  0xFF /* reg 0x17 ADC digital volume: 0xBF≈0 dB .. 0xFF≈+32 dB */
static const uint8_t es8311_adc_seq[][2] = {
    {0x0D, 0x01}, /* power up analog (shared with DAC) */
    {0x0E, 0x02}, /* power up ADC modulator + analog PGA */
    {0x14, 0x5A}, /* select analog mic input (BIT6) + base 0x1A */
    {0x16, ES8311_MIC_GAIN}, /* ADC PGA / mic gain (max) */
    {0x17, ES8311_ADC_VOL}, /* ADC digital volume (near max — mic is quiet) */
    {0x1C, 0x6A}, /* ADC EQ bypass + DC offset cancel */
    {0x00, 0x80}, /* re-assert CSM power on */
};

void furi_hal_speaker_es8311_adc_init(void) {
    if(es8311_adc_inited) return;
    es8311_ensure_init(); /* codec base (clocks/power) must be up first */
    es8311_adc_inited = true;

    unsigned nacks = 0;
    for(size_t i = 0; i < sizeof(es8311_adc_seq) / sizeof(es8311_adc_seq[0]); i++) {
        if(es8311_write(es8311_adc_seq[i][0], es8311_adc_seq[i][1]) != ESP_OK) nacks++;
    }
    FURI_LOG_I(TAG, "ES8311 ADC/mic init done (%u write errors)", nacks);
}

void* furi_hal_speaker_i2s_rx_handle(void) {
    return i2s_rx_handle;
}
#endif /* BOARD_HAS_MIC */
#endif

/* ---- Helpers ---- */

/** Apply cubic volume scaling (matches STM32 firmware behaviour). */
static inline float speaker_volume_curve(float v) {
    if(v < 0.0f) v = 0.0f;
    if(v > 1.0f) v = 1.0f;
    return v * v * v;
}

/** (Re-)generate the sine-wave buffer for the current frequency/volume. */
static void speaker_generate_buffer(void) {
    float freq = speaker_frequency;
    float vol = speaker_volume_curve(speaker_volume);

    if(freq < 1.0f) freq = 1.0f;

    /* Compute how many samples make up one full cycle */
    uint32_t samples_per_cycle = (uint32_t)(SPEAKER_SAMPLE_RATE / freq);
    if(samples_per_cycle < 2) samples_per_cycle = 2;
    if(samples_per_cycle > SPEAKER_MAX_CYCLE_SAMPLES) samples_per_cycle = SPEAKER_MAX_CYCLE_SAMPLES;

    /* Allocate / reallocate if needed */
    size_t needed = samples_per_cycle * 2 * sizeof(int16_t); /* stereo */
    if(wave_buffer == NULL || wave_buffer_samples != samples_per_cycle) {
        if(wave_buffer) free(wave_buffer);
        wave_buffer = malloc(needed);
        wave_buffer_samples = samples_per_cycle;
    }
    wave_buffer_bytes = needed;

    /* Fill with sine wave */
    float amplitude = vol * 32767.0f;
    for(uint32_t i = 0; i < samples_per_cycle; i++) {
        int16_t sample = (int16_t)(amplitude * sinf(2.0f * (float)M_PI * (float)i / (float)samples_per_cycle));
        wave_buffer[i * 2] = sample;     /* Left */
        wave_buffer[i * 2 + 1] = sample; /* Right */
    }

    speaker_buffer_dirty = false;
}

/** Background thread: continuously writes audio to I2S in the active mode. */
static int32_t speaker_writer_thread(void* context) {
    UNUSED(context);

    /* Stack-allocated mirror buffer (stereo int16). Kept here, not on the
     * thread stack of every iteration, so its lifetime spans the whole loop. */
    static int16_t mirror_buf[MIRROR_BUF_FRAMES * 2];

    while(speaker_thread_run) {
        SpeakerMode mode = speaker_mode;

        if(mode == SpeakerModeTone) {
            if(speaker_buffer_dirty) {
                speaker_generate_buffer();
            }
            if(wave_buffer && wave_buffer_bytes > 0) {
                size_t bytes_written = 0;
                i2s_channel_write(i2s_tx_handle, wave_buffer, wave_buffer_bytes, &bytes_written, 100);
            }
            continue;
        }

        if(mode == SpeakerModeGdoMirror) {
            const gpio_num_t pin = speaker_mirror_pin;
            const float vol = speaker_volume_curve(speaker_mirror_volume);
            const int16_t amp = (int16_t)(vol * 32767.0f);
            const int64_t period_us = 1000000 / MIRROR_SAMPLE_RATE;

            /* Sample the GPIO at MIRROR_SAMPLE_RATE, busy-waiting between
             * samples so each sample reflects the GDO0 level at the right
             * moment. The i2s_channel_write below provides the longer-term
             * pacing (DMA back-pressure) — busy-wait only handles intra-buffer
             * spacing (~62 µs at 16 kHz). */
            int64_t next_us = esp_timer_get_time();
            for(int i = 0; i < MIRROR_BUF_FRAMES; i++) {
                while(esp_timer_get_time() < next_us) { /* spin */ }
                next_us += period_us;
                int16_t s = gpio_get_level(pin) ? amp : (int16_t)-amp;
                mirror_buf[i * 2] = s;
                mirror_buf[i * 2 + 1] = s;
            }

            size_t bytes_written = 0;
            i2s_channel_write(i2s_tx_handle, mirror_buf, sizeof(mirror_buf), &bytes_written, 100);
            continue;
        }

        /* Idle */
        furi_delay_ms(5);
    }

    return 0;
}

/* ---- Public API ---- */

void furi_hal_speaker_init(void) {
    furi_assert(speaker_mutex == NULL);
    speaker_mutex = furi_mutex_alloc(FuriMutexTypeNormal);

    /* Release pin 40 from LCD RST usage — the display driver only pulses it
     * during init and does not need it afterwards. */
    gpio_reset_pin((gpio_num_t)BOARD_PIN_SPEAKER_WCLK);

    /* Configure I2S standard mode */
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    chan_cfg.dma_desc_num = SPEAKER_DMA_DESC_NUM;
    chan_cfg.dma_frame_num = SPEAKER_DMA_FRAME_NUM;
    chan_cfg.auto_clear = true; /* send silence when no data */
#if defined(BOARD_HAS_MIC) && BOARD_HAS_MIC
    /* Full-duplex: TX drives the ES8311 DAC (speaker), RX reads its ADC (mic).
     * Both share BCLK/WS; auto_clear keeps the clock running (silence) so the
     * mic can capture even when nothing is playing. */
    ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, &i2s_tx_handle, &i2s_rx_handle));
#else
    ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, &i2s_tx_handle, NULL));
#endif

    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(SPEAKER_SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            /* No MCLK pin: the ADV's ES8311 derives its master clock from BCLK
             * (reg 0x01=0xB5) and the standard Cardputer's NS4168 needs none.
             * Routing MCLK to GPIO0 (BOOT strap) was both unnecessary and wrong. */
            .mclk = I2S_GPIO_UNUSED,
            .bclk = (gpio_num_t)BOARD_PIN_SPEAKER_BCLK,
            .ws = (gpio_num_t)BOARD_PIN_SPEAKER_WCLK,
            .dout = (gpio_num_t)BOARD_PIN_SPEAKER_DOUT,
            .din = I2S_GPIO_UNUSED,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(i2s_tx_handle, &std_cfg));

#if defined(BOARD_HAS_MIC) && BOARD_HAS_MIC
    /* RX shares clk/slot config; only the data pin differs (DIN = mic ADC out).
     * Non-fatal: this runs at boot, so a full-duplex hiccup must not panic the
     * whole firmware — just disable the mic (speaker/TX stays fine). */
    i2s_std_config_t rx_cfg = std_cfg;
    rx_cfg.gpio_cfg.dout = I2S_GPIO_UNUSED;
    rx_cfg.gpio_cfg.din = (gpio_num_t)BOARD_PIN_MIC_DATA;
    esp_err_t rx_err = i2s_channel_init_std_mode(i2s_rx_handle, &rx_cfg);
    if(rx_err != ESP_OK) {
        FURI_LOG_E(TAG, "I2S RX (mic) init failed: %d — mic disabled", rx_err);
        i2s_del_channel(i2s_rx_handle);
        i2s_rx_handle = NULL;
    }
#endif

    /* Start the writer thread (it idles until speaker_mode != Idle) */
    speaker_thread = furi_thread_alloc_ex("SpeakerWorker", SPEAKER_THREAD_STACK, speaker_writer_thread, NULL);
    speaker_thread_run = true;
    furi_thread_start(speaker_thread);

    FURI_LOG_I(TAG, "Init OK (I2S, BCLK=%d WS=%d DOUT=%d)",
               BOARD_PIN_SPEAKER_BCLK, BOARD_PIN_SPEAKER_WCLK, BOARD_PIN_SPEAKER_DOUT);
}

void furi_hal_speaker_deinit(void) {
    furi_check(speaker_mutex != NULL);

    /* Stop writer thread */
    speaker_thread_run = false;
    furi_thread_join(speaker_thread);
    furi_thread_free(speaker_thread);
    speaker_thread = NULL;

    /* Tear down I2S */
    if(i2s_channel_enabled) {
        i2s_channel_disable(i2s_tx_handle);
        i2s_channel_enabled = false;
    }
    i2s_del_channel(i2s_tx_handle);
    i2s_tx_handle = NULL;

#if defined(BOARD_HAS_MIC) && BOARD_HAS_MIC
    if(i2s_rx_handle) {
        i2s_del_channel(i2s_rx_handle);
        i2s_rx_handle = NULL;
    }
    es8311_adc_inited = false;
#endif

    /* Free buffer */
    if(wave_buffer) {
        free(wave_buffer);
        wave_buffer = NULL;
    }

    furi_mutex_free(speaker_mutex);
    speaker_mutex = NULL;
}

bool furi_hal_speaker_acquire(uint32_t timeout) {
    furi_check(!FURI_IS_IRQ_MODE());

    if(furi_mutex_acquire(speaker_mutex, timeout) == FuriStatusOk) {
        /* Enable I2S channel on first acquire */
        if(!i2s_channel_enabled) {
            ESP_ERROR_CHECK(i2s_channel_enable(i2s_tx_handle));
            i2s_channel_enabled = true;
        }
#if defined(BOARD_HAS_ES8311) && BOARD_HAS_ES8311
        /* MCLK is now running (channel enabled) and the keyboard's shared I2C
         * bus is up by first speaker use — configure the codec once. */
        es8311_ensure_init();
#endif
        return true;
    }
    return false;
}

void furi_hal_speaker_release(void) {
    furi_check(!FURI_IS_IRQ_MODE());
    furi_check(furi_hal_speaker_is_mine());

    furi_hal_speaker_stop();
    furi_check(furi_mutex_release(speaker_mutex) == FuriStatusOk);
}

bool furi_hal_speaker_is_mine(void) {
    return (FURI_IS_IRQ_MODE()) ||
           (furi_mutex_get_owner(speaker_mutex) == furi_thread_get_current_id());
}

void furi_hal_speaker_start(float frequency, float volume) {
    furi_check(furi_hal_speaker_is_mine());

    if(volume <= 0.0f) {
        furi_hal_speaker_stop();
        return;
    }

    speaker_frequency = frequency;
    speaker_volume = volume;
    speaker_buffer_dirty = true;
    speaker_mode = SpeakerModeTone;
}

void furi_hal_speaker_start_gdo_mirror(const GpioPin* gdo_pin, float volume) {
    furi_check(furi_hal_speaker_is_mine());
    furi_check(gdo_pin);
    furi_check(gdo_pin->pin < GPIO_NUM_MAX);

    if(volume <= 0.0f) {
        furi_hal_speaker_stop();
        return;
    }

    speaker_mirror_pin = (gpio_num_t)gdo_pin->pin;
    speaker_mirror_volume = volume;
    speaker_mode = SpeakerModeGdoMirror;
}

void furi_hal_speaker_set_volume(float volume) {
    furi_check(furi_hal_speaker_is_mine());

    if(volume <= 0.0f) {
        furi_hal_speaker_stop();
        return;
    }

    /* Update whichever mode is active */
    speaker_volume = volume;
    speaker_mirror_volume = volume;
    speaker_buffer_dirty = true;
}

void furi_hal_speaker_stop(void) {
    furi_check(furi_hal_speaker_is_mine());
    speaker_mode = SpeakerModeIdle;
}

#else /* !BOARD_HAS_SPEAKER */

/* ---- No-op stubs for boards without speaker hardware ---- */

void furi_hal_speaker_init(void) {
}

void furi_hal_speaker_deinit(void) {
}

bool furi_hal_speaker_acquire(uint32_t timeout) {
    (void)timeout;
    return true;
}

void furi_hal_speaker_release(void) {
}

bool furi_hal_speaker_is_mine(void) {
    return true;
}

void furi_hal_speaker_start(float frequency, float volume) {
    (void)frequency;
    (void)volume;
}

void furi_hal_speaker_start_gdo_mirror(const GpioPin* gdo_pin, float volume) {
    (void)gdo_pin;
    (void)volume;
}

void furi_hal_speaker_set_volume(float volume) {
    (void)volume;
}

void furi_hal_speaker_stop(void) {
}

#endif /* BOARD_HAS_SPEAKER */
