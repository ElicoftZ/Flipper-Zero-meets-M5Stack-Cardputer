/**
 * @file furi_hal_mic.c
 * Microphone capture HAL.
 *
 * On the Cardputer-ADV the microphone is the ES8311 codec's ADC. Capture shares
 * the speaker's full-duplex I2S_NUM_0 channel (BCLK=41/WS=43) and the codec; the
 * mic data arrives on DIN=GPIO46. We reuse the speaker mutex for exclusive audio
 * access, and the RX channel + ES8311 ADC init exposed by furi_hal_speaker.c.
 *
 * Boards without a mic get no-op stubs so callers link everywhere.
 */

#include "furi_hal_mic.h"
#include "boards/board.h"
#include <furi.h>

#define TAG "FuriHalMic"

#if defined(BOARD_HAS_MIC) && BOARD_HAS_MIC && defined(BOARD_HAS_ES8311) && BOARD_HAS_ES8311

#include "furi_hal_speaker.h"
#include <driver/i2s_std.h>

#define MIC_SAMPLE_RATE 44100
#define MIC_READ_FRAMES 256 /* stereo frames per i2s read chunk */

static bool mic_rx_enabled = false;

uint32_t furi_hal_mic_start(void) {
    /* Acquire audio exclusivity: this also enables the I2S channel (clock runs,
     * silence via auto_clear) and inits the ES8311 (DAC base). */
    if(!furi_hal_speaker_acquire(1000)) {
        FURI_LOG_E(TAG, "audio busy, cannot start mic");
        return 0;
    }

    furi_hal_speaker_es8311_adc_init();

    i2s_chan_handle_t rx = (i2s_chan_handle_t)furi_hal_speaker_i2s_rx_handle();
    if(!rx) {
        FURI_LOG_E(TAG, "no I2S RX handle");
        furi_hal_speaker_release();
        return 0;
    }

    if(!mic_rx_enabled) {
        esp_err_t err = i2s_channel_enable(rx);
        if(err != ESP_OK) {
            FURI_LOG_E(TAG, "i2s rx enable failed: %d", err);
            furi_hal_speaker_release();
            return 0;
        }
        mic_rx_enabled = true;
    }

    FURI_LOG_I(TAG, "mic started @ %u Hz", (unsigned)MIC_SAMPLE_RATE);
    return MIC_SAMPLE_RATE;
}

size_t furi_hal_mic_read(int16_t* buf, size_t samples, uint32_t timeout_ms) {
    i2s_chan_handle_t rx = (i2s_chan_handle_t)furi_hal_speaker_i2s_rx_handle();
    if(!rx || !mic_rx_enabled || buf == NULL || samples == 0) return 0;

    /* RX slots are stereo (shared with the TX config); the ES8311 mono ADC lands
     * on the left slot. Read stereo frames and keep the left channel. */
    static int16_t stereo[MIC_READ_FRAMES * 2];
    size_t got = 0;

    while(got < samples) {
        size_t want = samples - got;
        if(want > MIC_READ_FRAMES) want = MIC_READ_FRAMES;

        size_t bytes_read = 0;
        esp_err_t err =
            i2s_channel_read(rx, stereo, want * 2 * sizeof(int16_t), &bytes_read, timeout_ms);
        if(err != ESP_OK || bytes_read == 0) break;

        size_t frames = bytes_read / (2 * sizeof(int16_t));
        for(size_t i = 0; i < frames; i++) {
            buf[got + i] = stereo[i * 2]; /* left channel */
        }
        got += frames;

        if(frames < want) break; /* short read: no more buffered data */
    }

    return got;
}

void furi_hal_mic_stop(void) {
    i2s_chan_handle_t rx = (i2s_chan_handle_t)furi_hal_speaker_i2s_rx_handle();
    if(rx && mic_rx_enabled) {
        i2s_channel_disable(rx);
        mic_rx_enabled = false;
    }
    if(furi_hal_speaker_is_mine()) {
        furi_hal_speaker_release();
    }
    FURI_LOG_I(TAG, "mic stopped");
}

#else /* board without ES8311 mic */

uint32_t furi_hal_mic_start(void) {
    return 0;
}

size_t furi_hal_mic_read(int16_t* buf, size_t samples, uint32_t timeout_ms) {
    UNUSED(buf);
    UNUSED(samples);
    UNUSED(timeout_ms);
    return 0;
}

void furi_hal_mic_stop(void) {
}

#endif
