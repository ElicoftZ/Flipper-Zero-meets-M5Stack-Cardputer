/**
 * @file furi_hal_infrared.c
 * ESP32 Infrared HAL implementation using RMT peripheral.
 *
 * RX: RMT receiver captures edges, a GPTimer provides silence timeout.
 * TX: RMT transmitter with carrier modulation, fed via encode callback.
 */

#include <furi_hal_infrared.h>
#include <furi.h>

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include <driver/rmt_tx.h>
#include <driver/rmt_rx.h>
#include <driver/rmt_encoder.h>
#include <driver/gptimer.h>
#include <driver/gpio.h>
#include <esp_heap_caps.h>

#include BOARD_INCLUDE
#include "furi_hal_resources.h"

/* ---- Configuration ---- */

#if BOARD_HAS_IR
#define IR_TX_GPIO  gpio_ir_tx.pin
#define IR_RX_GPIO  gpio_ir_rx.pin
#else
#define IR_TX_GPIO  GPIO_NUM_NC
#define IR_RX_GPIO  GPIO_NUM_NC
#endif

/* Grove / Qwiic connector pins for external IR unit (M5Unit IR) auto-detection.
 * On the Cardputer-ADV the Grove RX line is the same pin as the internal IR RX
 * (both GPIO1 = Grove SCL); the internal LED is on G44, the Grove IR TX on G2. */
#if defined(BOARD_PIN_QWIIC_SDA) && defined(BOARD_PIN_QWIIC_SCL)
#define IR_GROVE_TX_GPIO ((gpio_num_t)BOARD_PIN_QWIIC_SDA) /* M5Unit IR TX line */
#define IR_GROVE_RX_GPIO ((gpio_num_t)BOARD_PIN_QWIIC_SCL) /* M5Unit IR TSOP RX */
#else
#define IR_GROVE_TX_GPIO GPIO_NUM_NC
#define IR_GROVE_RX_GPIO GPIO_NUM_NC
#endif

/* Which physical TX output the IR HAL currently drives. Selected by the IR app
 * via furi_hal_infrared_set_tx_output() (usually from the auto-detect probe). */
static FuriHalInfraredTxPin active_tx_pin = FuriHalInfraredTxPinInternal;

#define IR_RMT_RX_MEM_BLOCK_SYMBOLS 128
#define IR_RMT_RX_RESOLUTION_HZ     1000000 /* 1 MHz = 1 us per tick */
#define IR_RMT_TX_RESOLUTION_HZ     1000000 /* 1 MHz = 1 us per tick */
#define IR_RMT_RX_MAX_SYMBOLS       1024

/* When the RX line returns to idle, RMT ends the burst and records the
 * terminating space as a zero-duration "end marker" — so the long inter-frame
 * space that trails every burst is never delivered to the decoder. The STM32
 * capture path delivers that space naturally (when the next burst's leading
 * edge arrives), and min_split_time protocols (NEC, Samsung, RC5/6, SIRC,
 * Kaseikyo, RCA, Pioneer) rely on it to finish decoding mid-stream. Without it
 * they can only complete via the silence timeout — which never fires while a
 * button is HELD, because the ~110 ms repeat bursts keep restarting it, so a
 * held remote is never learned. We therefore synthesise the trailing space at
 * the end of each idle-terminated burst. Its duration must exceed the largest
 * protocol min_split_time (Pioneer = 26000 us) yet stay below NEC's repeat
 * pause max (150000 us) and the silence timeout, so 50 ms is a safe choice. */
#define IR_RX_FRAME_END_SPACE_US 50000

/* ---- State ---- */

typedef enum {
    InfraredStateIdle,
    InfraredStateAsyncRx,
    InfraredStateAsyncTx,
    InfraredStateAsyncTxStopReq,
    InfraredStateAsyncTxStopped,
    InfraredStateMAX,
} InfraredState;

/* RX state */
static struct {
    rmt_channel_handle_t channel;
    rmt_receive_config_t rx_config;
    rmt_symbol_word_t symbols[IR_RMT_RX_MAX_SYMBOLS];
    FuriHalInfraredRxCaptureCallback capture_callback;
    void* capture_context;
    FuriHalInfraredRxTimeoutCallback timeout_callback;
    void* timeout_context;
    gptimer_handle_t timeout_timer;
    uint32_t timeout_us;
} ir_rx;

/* TX state */
static struct {
    rmt_channel_handle_t channel;
    rmt_encoder_handle_t copy_encoder;
    FuriHalInfraredTxGetDataISRCallback data_callback;
    void* data_context;
    FuriHalInfraredTxSignalSentISRCallback signal_sent_callback;
    void* signal_sent_context;
    SemaphoreHandle_t done_semaphore;
    uint32_t carrier_freq;
    float duty_cycle;
} ir_tx;

static volatile InfraredState ir_state = InfraredStateIdle;

/* ---- RX Implementation ---- */

static void ir_rx_restart_timeout(void) {
    if(ir_rx.timeout_timer) {
        gptimer_stop(ir_rx.timeout_timer);
        gptimer_set_raw_count(ir_rx.timeout_timer, 0);
        gptimer_start(ir_rx.timeout_timer);
    }
}

static bool IRAM_ATTR ir_rx_timeout_isr(gptimer_handle_t timer,
                                         const gptimer_alarm_event_data_t* edata,
                                         void* user_ctx) {
    (void)timer;
    (void)edata;
    (void)user_ctx;
    gptimer_stop(timer);
    if(ir_rx.timeout_callback) {
        ir_rx.timeout_callback(ir_rx.timeout_context);
    }
    return false; /* no high-priority task woken */
}

static bool IRAM_ATTR ir_rmt_rx_done_callback(rmt_channel_handle_t channel,
                                                const rmt_rx_done_event_data_t* edata,
                                                void* user_ctx) {
    (void)channel;
    (void)user_ctx;

    bool last_level_mark = false; /* level of the last edge we delivered */
    bool any_edge = false;

    /* Process received symbols and call capture callback for each edge */
    for(size_t i = 0; i < edata->num_symbols; i++) {
        const rmt_symbol_word_t* sym = &edata->received_symbols[i];

        /* Each RMT symbol has two phases: duration0/level0 then duration1/level1.
         * invert_in=true on the RX channel already converts the active-low IR
         * receiver output to logical mark=1/space=0, so pass sym->level through
         * unchanged. Negating here would drop the leader mark via the worker's
         * "skip first space" rule. */
        if(sym->duration0 > 0 && ir_rx.capture_callback) {
            ir_rx.capture_callback(
                ir_rx.capture_context, sym->level0, sym->duration0);
            ir_rx_restart_timeout();
            last_level_mark = sym->level0;
            any_edge = true;
        }
        if(sym->duration1 > 0 && ir_rx.capture_callback) {
            ir_rx.capture_callback(
                ir_rx.capture_context, sym->level1, sym->duration1);
            ir_rx_restart_timeout();
            last_level_mark = sym->level1;
            any_edge = true;
        }
    }

    /* A burst always ends on a Mark (the trailing Space is the idle that
     * terminated reception, dropped above as a zero-duration end marker).
     * Deliver that trailing Space synthetically so min_split_time protocols
     * decode immediately, instead of waiting for a silence timeout that never
     * arrives while a held remote keeps repeating. See IR_RX_FRAME_END_SPACE_US.
     * Decoded protocols consume it (the worker resets its timing count, so no
     * stray raw timing); raw signals just gain a trailing gap. */
    if(any_edge && last_level_mark && ir_rx.capture_callback) {
        ir_rx.capture_callback(ir_rx.capture_context, false, IR_RX_FRAME_END_SPACE_US);
        ir_rx_restart_timeout();
    }

    /* Re-start receiving for next burst */
    if(ir_state == InfraredStateAsyncRx) {
        rmt_receive(channel, ir_rx.symbols, sizeof(ir_rx.symbols), &ir_rx.rx_config);
    }

    return false;
}

void furi_hal_infrared_async_rx_start(void) {
    furi_check(ir_state == InfraredStateIdle);
#if !BOARD_HAS_IR
    FURI_LOG_E("IR", "Board has no IR support");
    return;
#endif

    /* Route RX to the active input: the Grove M5Unit TSOP when an external
     * module is selected, otherwise the board's internal IR RX pin. */
    gpio_num_t rx_gpio = IR_RX_GPIO;
    if(active_tx_pin == FuriHalInfraredTxPinExtPA7) {
        rx_gpio = IR_GROVE_RX_GPIO;
    }

    /* Configure RMT RX channel */
    rmt_rx_channel_config_t rx_chan_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = IR_RMT_RX_RESOLUTION_HZ,
        .mem_block_symbols = IR_RMT_RX_MEM_BLOCK_SYMBOLS,
        .gpio_num = rx_gpio,
        .flags = {
            .invert_in = true, /* IR receiver module output is active-low */
            .with_dma = false,
        },
    };
    ESP_ERROR_CHECK(rmt_new_rx_channel(&rx_chan_config, &ir_rx.channel));

    /* Register RX done callback */
    rmt_rx_event_callbacks_t rx_cbs = {
        .on_recv_done = ir_rmt_rx_done_callback,
    };
    ESP_ERROR_CHECK(rmt_rx_register_event_callbacks(ir_rx.channel, &rx_cbs, NULL));

    /* Enable channel */
    ESP_ERROR_CHECK(rmt_enable(ir_rx.channel));

    /* Configure receive parameters */
    ir_rx.rx_config.signal_range_min_ns = 1250;  /* glitch filter: ignore < 1.25us */
    /* RMT idle threshold: silence longer than this ends the frame.
       Max at 1 MHz resolution is ~32.7 ms (RMT_LL_MAX_IDLE_VALUE=32767).
       Use 20 ms default which covers all common IR protocols. */
    uint32_t max_ns = ir_rx.timeout_us > 0 ? ir_rx.timeout_us * 1000 : 20000 * 1000;
    if(max_ns > 32000 * 1000) max_ns = 32000 * 1000; /* clamp to RMT HW limit */
    ir_rx.rx_config.signal_range_max_ns = max_ns;
    ir_rx.rx_config.flags.en_partial_rx = false;

    ir_state = InfraredStateAsyncRx;

    /* Start receiving */
    ESP_ERROR_CHECK(rmt_receive(ir_rx.channel, ir_rx.symbols, sizeof(ir_rx.symbols), &ir_rx.rx_config));
}

void furi_hal_infrared_async_rx_stop(void) {
    if(ir_state != InfraredStateAsyncRx) {
        FURI_LOG_W("IR", "async_rx_stop called but RX was not running");
        return;
    }

    ir_state = InfraredStateIdle;

    rmt_disable(ir_rx.channel);
    rmt_del_channel(ir_rx.channel);
    ir_rx.channel = NULL;

    /* Stop and delete timeout timer if active */
    if(ir_rx.timeout_timer) {
        gptimer_stop(ir_rx.timeout_timer);
        gptimer_disable(ir_rx.timeout_timer);
        gptimer_del_timer(ir_rx.timeout_timer);
        ir_rx.timeout_timer = NULL;
    }
}

void furi_hal_infrared_async_rx_set_timeout(uint32_t timeout_us) {
    ir_rx.timeout_us = timeout_us;

    /* If RX is already running, create/update the GPTimer for timeout */
    if(ir_state == InfraredStateAsyncRx || ir_state == InfraredStateIdle) {
        /* Delete existing timer */
        if(ir_rx.timeout_timer) {
            gptimer_stop(ir_rx.timeout_timer);
            gptimer_disable(ir_rx.timeout_timer);
            gptimer_del_timer(ir_rx.timeout_timer);
            ir_rx.timeout_timer = NULL;
        }

        if(timeout_us == 0) return;

        /* Create a one-shot alarm timer */
        gptimer_config_t timer_config = {
            .clk_src = GPTIMER_CLK_SRC_DEFAULT,
            .direction = GPTIMER_COUNT_UP,
            .resolution_hz = 1000000, /* 1 MHz = 1us */
        };
        ESP_ERROR_CHECK(gptimer_new_timer(&timer_config, &ir_rx.timeout_timer));

        gptimer_alarm_config_t alarm_config = {
            .alarm_count = timeout_us,
            .reload_count = 0,
            .flags = {
                .auto_reload_on_alarm = false,
            },
        };
        ESP_ERROR_CHECK(gptimer_set_alarm_action(ir_rx.timeout_timer, &alarm_config));

        gptimer_event_callbacks_t timer_cbs = {
            .on_alarm = ir_rx_timeout_isr,
        };
        ESP_ERROR_CHECK(gptimer_register_event_callbacks(ir_rx.timeout_timer, &timer_cbs, NULL));
        ESP_ERROR_CHECK(gptimer_enable(ir_rx.timeout_timer));
        /* Timer will be started when first edge is received */
    }
}

void furi_hal_infrared_async_rx_set_capture_isr_callback(
    FuriHalInfraredRxCaptureCallback callback,
    void* ctx) {
    ir_rx.capture_callback = callback;
    ir_rx.capture_context = ctx;
}

void furi_hal_infrared_async_rx_set_timeout_isr_callback(
    FuriHalInfraredRxTimeoutCallback callback,
    void* ctx) {
    ir_rx.timeout_callback = callback;
    ir_rx.timeout_context = ctx;
}

/* ---- TX Implementation ---- */

/**
 * TX strategy: We use a simple loop in a FreeRTOS task context.
 * The data_callback provides (duration_us, level) pairs.
 * We build RMT symbols and transmit them in batches.
 *
 * The STM32 uses DMA double-buffering with ISR callbacks;
 * on ESP32 we use the RMT encoder infrastructure with a custom
 * "IR TX encoder" that pulls data from the callback.
 */

/* A raw signal can hold up to MAX_TIMINGS_AMOUNT (1024) timings = 512 RMT
 * symbols. The whole packet MUST be sent in a single rmt_transmit() call:
 * splitting it into several transactions makes the RMT channel go idle
 * between them, inserting timing gaps mid-frame that corrupt the waveform so
 * the receiver no longer recognises it (raw/learned signals only — decoded
 * protocol packets are short). The RMT driver does internal ping-pong refill
 * from this buffer, so one transaction is gapless even though the buffer is
 * larger than mem_block_symbols. */
#define IR_TX_MAX_SYMBOLS 600

static void ir_tx_task(void* arg) {
    (void)arg;

    rmt_symbol_word_t* symbols =
        heap_caps_malloc(IR_TX_MAX_SYMBOLS * sizeof(rmt_symbol_word_t), MALLOC_CAP_8BIT);
    furi_check(symbols);
    rmt_transmit_config_t tx_config = {
        .loop_count = 0, /* no loop, single shot */
        .flags = {
            .eot_level = 0, /* idle low after transmission */
        },
    };

    bool running = true;
    while(running) {
        size_t sym_count = 0;
        bool packet_end = false;
        bool last_packet = false;

        /* Accumulate one full packet of symbols from the callback */
        while(sym_count < IR_TX_MAX_SYMBOLS && !packet_end) {
            uint32_t duration_mark = 0;
            uint32_t duration_space = 0;
            bool level_mark = false;
            bool level_space = false;

            /* Get mark timing */
            FuriHalInfraredTxGetDataState state_mark =
                ir_tx.data_callback(ir_tx.data_context, &duration_mark, &level_mark);

            if(state_mark == FuriHalInfraredTxGetDataStateLastDone && duration_mark == 0) {
                last_packet = true;
                break;
            }

            /* Get space timing */
            FuriHalInfraredTxGetDataState state_space = FuriHalInfraredTxGetDataStateOk;
            if(state_mark == FuriHalInfraredTxGetDataStateOk) {
                state_space =
                    ir_tx.data_callback(ir_tx.data_context, &duration_space, &level_space);
            } else {
                /* Mark was the last in this packet, add a trailing space */
                duration_space = 0;
                level_space = false;
                if(state_mark == FuriHalInfraredTxGetDataStateDone) {
                    packet_end = true;
                } else {
                    last_packet = true;
                    packet_end = true;
                }
            }

            /* Build RMT symbol: duration0=mark, duration1=space */
            symbols[sym_count].duration0 = duration_mark;
            symbols[sym_count].level0 = level_mark ? 1 : 0;
            symbols[sym_count].duration1 = duration_space > 0 ? duration_space : 1;
            symbols[sym_count].level1 = level_space ? 1 : 0;
            sym_count++;

            if(state_space == FuriHalInfraredTxGetDataStateDone) {
                packet_end = true;
            } else if(state_space == FuriHalInfraredTxGetDataStateLastDone) {
                packet_end = true;
                last_packet = true;
            }
        }

        if(sym_count > 0) {
            /* Transmit the whole packet in a single (gapless) transaction */
            ESP_ERROR_CHECK(rmt_transmit(
                ir_tx.channel, ir_tx.copy_encoder,
                symbols, sym_count * sizeof(rmt_symbol_word_t),
                &tx_config));
            /* Wait for transmission to complete */
            ESP_ERROR_CHECK(rmt_tx_wait_all_done(ir_tx.channel, portMAX_DELAY));
        }

        if(packet_end) {
            /* Signal sent callback */
            if(ir_tx.signal_sent_callback) {
                ir_tx.signal_sent_callback(ir_tx.signal_sent_context);
            }
        }

        /* Check stop conditions */
        if(last_packet || ir_state == InfraredStateAsyncTxStopReq) {
            running = false;
        }
    }

    heap_caps_free(symbols);

    ir_state = InfraredStateAsyncTxStopped;
    if(ir_tx.done_semaphore) {
        xSemaphoreGive(ir_tx.done_semaphore);
    }

    vTaskDelete(NULL);
}

void furi_hal_infrared_async_tx_start(uint32_t freq, float duty_cycle) {
    furi_check(ir_state == InfraredStateIdle);
    furi_check(ir_tx.data_callback != NULL);
    furi_check(freq >= INFRARED_MIN_FREQUENCY && freq <= INFRARED_MAX_FREQUENCY);
    furi_check(duty_cycle > 0.0f && duty_cycle <= 1.0f);
#if !BOARD_HAS_IR
    FURI_LOG_E("IR", "Board has no IR support");
    return;
#endif

    ir_tx.carrier_freq = freq;
    ir_tx.duty_cycle = duty_cycle;

    /* Route TX to the active output: the Grove M5Unit IR LED (G2) when an
     * external module is selected, otherwise the board's internal IR LED (G44). */
    gpio_num_t tx_gpio = IR_TX_GPIO;
    if(active_tx_pin == FuriHalInfraredTxPinExtPA7) {
        tx_gpio = IR_GROVE_TX_GPIO;
    }

    /* Create RMT TX channel with carrier modulation */
    rmt_tx_channel_config_t tx_chan_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = IR_RMT_TX_RESOLUTION_HZ,
        .mem_block_symbols = 64,
        .trans_queue_depth = 4,
        .gpio_num = tx_gpio,
        .flags = {
            .invert_out = false,
            .with_dma = false,
        },
    };
    ESP_ERROR_CHECK(rmt_new_tx_channel(&tx_chan_config, &ir_tx.channel));

    /* Apply carrier modulation */
    rmt_carrier_config_t carrier_config = {
        .frequency_hz = freq,
        .duty_cycle = duty_cycle,
        .flags = {
            .polarity_active_low = false,
            .always_on = false, /* Only modulate during mark (level=1) */
        },
    };
    ESP_ERROR_CHECK(rmt_apply_carrier(ir_tx.channel, &carrier_config));

    /* Create a copy encoder (pass-through, symbols already built) */
    rmt_copy_encoder_config_t copy_enc_config = {};
    ESP_ERROR_CHECK(rmt_new_copy_encoder(&copy_enc_config, &ir_tx.copy_encoder));

    ESP_ERROR_CHECK(rmt_enable(ir_tx.channel));

    /* Create synchronization semaphore (raw FreeRTOS, not Furi — tx task is xTaskCreate) */
    ir_tx.done_semaphore = xSemaphoreCreateBinary();

    ir_state = InfraredStateAsyncTx;

    /* Start TX task */
    xTaskCreate(ir_tx_task, "ir_tx", 4096, NULL, 15, NULL);
}

void furi_hal_infrared_async_tx_wait_termination(void) {
    furi_check(ir_state >= InfraredStateAsyncTx);
    furi_check(ir_state < InfraredStateMAX);

    /* Wait for TX task to finish */
    xSemaphoreTake(ir_tx.done_semaphore, portMAX_DELAY);

    /* Cleanup */
    rmt_disable(ir_tx.channel);
    rmt_del_encoder(ir_tx.copy_encoder);
    rmt_del_channel(ir_tx.channel);
    ir_tx.channel = NULL;
    ir_tx.copy_encoder = NULL;

    vSemaphoreDelete(ir_tx.done_semaphore);
    ir_tx.done_semaphore = NULL;

    ir_state = InfraredStateIdle;
}

void furi_hal_infrared_async_tx_stop(void) {
    furi_check(ir_state >= InfraredStateAsyncTx);
    furi_check(ir_state < InfraredStateMAX);

    if(ir_state == InfraredStateAsyncTx) {
        ir_state = InfraredStateAsyncTxStopReq;
    }

    furi_hal_infrared_async_tx_wait_termination();
}

void furi_hal_infrared_async_tx_set_data_isr_callback(
    FuriHalInfraredTxGetDataISRCallback callback,
    void* context) {
    furi_check(ir_state == InfraredStateIdle);
    ir_tx.data_callback = callback;
    ir_tx.data_context = context;
}

void furi_hal_infrared_async_tx_set_signal_sent_isr_callback(
    FuriHalInfraredTxSignalSentISRCallback callback,
    void* context) {
    ir_tx.signal_sent_callback = callback;
    ir_tx.signal_sent_context = context;
}

bool furi_hal_infrared_is_busy(void) {
    return ir_state != InfraredStateIdle;
}

FuriHalInfraredTxPin furi_hal_infrared_detect_tx_output(void) {
    /*
     * Hardware loopback probe for the M5Unit IR module on the Grove connector.
     * Grove SDA (G2) feeds the module's IR LED; Grove SCL (G1) is the TSOP
     * receiver output (active-LOW when it sees a 38 kHz carrier). Emit ~20
     * cycles of 38 kHz on G2 and watch G1: if it pulls LOW the module is
     * present -> use the Grove/external TX; otherwise fall back to the internal
     * LED (G44). No module -> behaves exactly like the old stub (Internal).
     */
#if defined(BOARD_PIN_QWIIC_SDA) && defined(BOARD_PIN_QWIIC_SCL)
    gpio_config_t tx_cfg = {
        .pin_bit_mask = (1ULL << IR_GROVE_TX_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&tx_cfg);

    gpio_config_t rx_cfg = {
        .pin_bit_mask = (1ULL << IR_GROVE_RX_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&rx_cfg);

    gpio_set_level(IR_GROVE_TX_GPIO, 0);
    furi_delay_ms(2);

    bool module_detected = false;
    for(int i = 0; i < 20 && !module_detected; i++) {
        gpio_set_level(IR_GROVE_TX_GPIO, 1);
        furi_delay_us(13); /* ~38 kHz half-period */
        gpio_set_level(IR_GROVE_TX_GPIO, 0);
        furi_delay_us(13);
        if(gpio_get_level(IR_GROVE_RX_GPIO) == 0) {
            module_detected = true;
        }
    }

    /* Restore both pins to Hi-Z so the RMT RX/TX HAL can reclaim them. */
    gpio_config_t restore_cfg = {
        .pin_bit_mask = (1ULL << IR_GROVE_TX_GPIO) | (1ULL << IR_GROVE_RX_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&restore_cfg);

    if(module_detected) {
        FURI_LOG_I("IR", "M5Unit IR module detected on Grove (G2/G1) - using external TX");
        return FuriHalInfraredTxPinExtPA7;
    }
    FURI_LOG_I("IR", "No IR module on Grove - using internal LED (G44)");
#endif
    return FuriHalInfraredTxPinInternal;
}

void furi_hal_infrared_set_tx_output(FuriHalInfraredTxPin tx_pin) {
    if(tx_pin < FuriHalInfraredTxPinMax) {
        active_tx_pin = tx_pin;
    }
}

FuriHalInfraredTxPin furi_hal_infrared_get_tx_output(void) {
    return active_tx_pin;
}
