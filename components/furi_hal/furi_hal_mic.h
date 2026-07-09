/**
 * @file furi_hal_mic.h
 * Microphone capture HAL. On the Cardputer-ADV the mic is the ES8311 codec ADC,
 * read over the speaker's full-duplex I2S bus (see furi_hal_speaker.c). No-op on
 * boards without a mic.
 */
#pragma once

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Start microphone capture.
 *
 * Acquires exclusive audio ownership (shared with the speaker), brings up the
 * codec ADC path, and enables the I2S RX channel.
 *
 * @return sample rate in Hz, or 0 on failure (audio busy / no mic).
 */
uint32_t furi_hal_mic_start(void);

/** Read mono 16-bit PCM samples.
 *
 * @param buf         destination for `samples` int16 mono samples
 * @param samples     number of samples requested
 * @param timeout_ms  per-DMA-read timeout
 * @return number of mono samples actually written to `buf`
 */
size_t furi_hal_mic_read(int16_t* buf, size_t samples, uint32_t timeout_ms);

/** Stop capture and release audio ownership. */
void furi_hal_mic_stop(void);

#ifdef __cplusplus
}
#endif
