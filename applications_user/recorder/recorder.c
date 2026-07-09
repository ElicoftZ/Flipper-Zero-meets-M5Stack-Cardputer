/**
 * Recorder — microphone voice/audio recorder for the M5Stack Cardputer-ADV.
 *
 * Captures the ES8311 codec mic (via furi_hal_mic) and streams it to a mono
 * 16-bit PCM WAV file on the SD card (/ext/recordings/). Lives in the main menu
 * (MENUEXTERNAL). OK toggles record/stop; Back exits.
 */

#include <furi.h>
#include <furi_hal.h>
#include <furi_hal_mic.h>

#include <gui/gui.h>
#include <gui/view_port.h>
#include <input/input.h>
#include <storage/storage.h>
#include <datetime/datetime.h>

#include <string.h>
#include <stdio.h>

#define TAG            "Recorder"
#define RECORDINGS_DIR EXT_PATH("recordings")
#define REC_SAMPLE_RATE 44100
#define REC_CHUNK_SAMPLES 512

typedef struct {
    Gui* gui;
    ViewPort* view_port;
    FuriMessageQueue* input_queue;
    Storage* storage;

    FuriMutex* mutex; /* guards the fields below (shared with worker thread) */
    bool recording;
    uint32_t start_tick;
    uint32_t data_bytes;
    int16_t level; /* last peak amplitude for the meter */
    char filename[40];
    char status[56];

    File* file;
    FuriThread* worker;
    volatile bool worker_run;
} Recorder;

/* ---- WAV (PCM mono 16-bit) header. ESP32 + WAV are both little-endian. ---- */
static void recorder_wav_write_header(File* file, uint32_t data_bytes) {
    const uint32_t sr = REC_SAMPLE_RATE;
    const uint16_t channels = 1;
    const uint16_t bits = 16;
    const uint32_t byte_rate = sr * channels * bits / 8;
    const uint16_t block_align = channels * bits / 8;
    const uint32_t riff_size = 36 + data_bytes;
    const uint32_t fmt_size = 16;
    const uint16_t audio_fmt = 1; /* PCM */

    uint8_t h[44];
    memcpy(h + 0, "RIFF", 4);
    memcpy(h + 4, &riff_size, 4);
    memcpy(h + 8, "WAVE", 4);
    memcpy(h + 12, "fmt ", 4);
    memcpy(h + 16, &fmt_size, 4);
    memcpy(h + 20, &audio_fmt, 2);
    memcpy(h + 22, &channels, 2);
    memcpy(h + 24, &sr, 4);
    memcpy(h + 28, &byte_rate, 4);
    memcpy(h + 32, &block_align, 2);
    memcpy(h + 34, &bits, 2);
    memcpy(h + 36, "data", 4);
    memcpy(h + 40, &data_bytes, 4);

    storage_file_seek(file, 0, true);
    storage_file_write(file, h, sizeof(h));
}

static int32_t recorder_worker(void* context) {
    Recorder* app = context;
    int16_t* buf = malloc(REC_CHUNK_SAMPLES * sizeof(int16_t));
    uint32_t last_ui = 0;

    while(app->worker_run) {
        size_t got = furi_hal_mic_read(buf, REC_CHUNK_SAMPLES, 200);
        if(got == 0) continue;

        int16_t peak = 0;
        for(size_t i = 0; i < got; i++) {
            int16_t v = buf[i] < 0 ? (int16_t)-buf[i] : buf[i];
            if(v > peak) peak = v;
        }
        size_t wrote = storage_file_write(app->file, buf, got * sizeof(int16_t));

        furi_mutex_acquire(app->mutex, FuriWaitForever);
        app->data_bytes += wrote;
        app->level = peak;
        furi_mutex_release(app->mutex);

        uint32_t now = furi_get_tick();
        if(now - last_ui > 100) {
            view_port_update(app->view_port);
            last_ui = now;
        }
    }

    free(buf);
    return 0;
}

static bool recorder_start(Recorder* app) {
    storage_common_mkdir(app->storage, RECORDINGS_DIR);

    DateTime dt;
    furi_hal_rtc_get_datetime(&dt);
    char path[96];
    snprintf(
        app->filename,
        sizeof(app->filename),
        "rec_%04u%02u%02u_%02u%02u%02u.wav",
        (unsigned)dt.year,
        (unsigned)dt.month,
        (unsigned)dt.day,
        (unsigned)dt.hour,
        (unsigned)dt.minute,
        (unsigned)dt.second);
    snprintf(path, sizeof(path), "%s/%s", RECORDINGS_DIR, app->filename);

    app->file = storage_file_alloc(app->storage);
    if(!storage_file_open(app->file, path, FSAM_WRITE, FSOM_CREATE_ALWAYS)) {
        storage_file_free(app->file);
        app->file = NULL;
        snprintf(app->status, sizeof(app->status), "Open failed (SD?)");
        return false;
    }
    recorder_wav_write_header(app->file, 0); /* placeholder sizes */

    if(furi_hal_mic_start() == 0) {
        storage_file_close(app->file);
        storage_file_free(app->file);
        app->file = NULL;
        snprintf(app->status, sizeof(app->status), "Mic busy");
        return false;
    }

    furi_mutex_acquire(app->mutex, FuriWaitForever);
    app->recording = true;
    app->start_tick = furi_get_tick();
    app->data_bytes = 0;
    app->level = 0;
    app->status[0] = '\0';
    furi_mutex_release(app->mutex);

    app->worker_run = true;
    app->worker = furi_thread_alloc_ex("RecWorker", 4096, recorder_worker, app);
    furi_thread_start(app->worker);
    return true;
}

static void recorder_stop(Recorder* app) {
    app->worker_run = false;
    if(app->worker) {
        furi_thread_join(app->worker);
        furi_thread_free(app->worker);
        app->worker = NULL;
    }
    furi_hal_mic_stop();

    furi_mutex_acquire(app->mutex, FuriWaitForever);
    uint32_t data_bytes = app->data_bytes;
    app->recording = false;
    furi_mutex_release(app->mutex);

    if(app->file) {
        /* Patch the RIFF + data chunk sizes now that we know the length. */
        uint32_t riff_size = 36 + data_bytes;
        storage_file_seek(app->file, 4, true);
        storage_file_write(app->file, &riff_size, 4);
        storage_file_seek(app->file, 40, true);
        storage_file_write(app->file, &data_bytes, 4);
        storage_file_close(app->file);
        storage_file_free(app->file);
        app->file = NULL;
    }

    furi_mutex_acquire(app->mutex, FuriWaitForever);
    snprintf(app->status, sizeof(app->status), "Saved: %s", app->filename);
    furi_mutex_release(app->mutex);
}

static void recorder_draw_callback(Canvas* canvas, void* context) {
    Recorder* app = context;

    furi_mutex_acquire(app->mutex, FuriWaitForever);
    bool rec = app->recording;
    uint32_t elapsed = rec ? (furi_get_tick() - app->start_tick) : 0;
    uint32_t bytes = app->data_bytes;
    int16_t level = app->level;
    char status[56];
    strncpy(status, app->status, sizeof(status));
    status[sizeof(status) - 1] = '\0';
    furi_mutex_release(app->mutex);

    canvas_clear(canvas);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 2, 12, "Recorder");
    canvas_set_font(canvas, FontSecondary);

    if(rec) {
        uint32_t sec = elapsed / 1000;
        char t[24];
        snprintf(t, sizeof(t), "REC  %02lu:%02lu", (unsigned long)(sec / 60), (unsigned long)(sec % 60));
        canvas_draw_box(canvas, 2, 22, 6, 6); /* record indicator */
        canvas_draw_str(canvas, 14, 30, t);

        int w = (int)(((int32_t)level * 102) / 32767);
        if(w < 0) w = 0;
        if(w > 102) w = 102;
        canvas_draw_frame(canvas, 2, 36, 104, 9);
        if(w > 0) canvas_draw_box(canvas, 3, 37, w, 7);

        char kb[24];
        snprintf(kb, sizeof(kb), "%lu KB", (unsigned long)(bytes / 1024));
        canvas_draw_str(canvas, 2, 58, kb);
        canvas_draw_str(canvas, 60, 58, "OK: stop");
    } else {
        canvas_draw_str(canvas, 2, 30, "OK: record");
        canvas_draw_str(canvas, 2, 42, "Back: exit");
        if(status[0]) canvas_draw_str(canvas, 2, 58, status);
    }
}

static void recorder_input_callback(InputEvent* event, void* context) {
    Recorder* app = context;
    furi_message_queue_put(app->input_queue, event, FuriWaitForever);
}

int32_t recorder_app(void* p) {
    UNUSED(p);

    Recorder* app = malloc(sizeof(Recorder));
    memset(app, 0, sizeof(Recorder));
    app->mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    app->input_queue = furi_message_queue_alloc(8, sizeof(InputEvent));
    app->storage = furi_record_open(RECORD_STORAGE);
    app->gui = furi_record_open(RECORD_GUI);

    app->view_port = view_port_alloc();
    view_port_draw_callback_set(app->view_port, recorder_draw_callback, app);
    view_port_input_callback_set(app->view_port, recorder_input_callback, app);
    gui_add_view_port(app->gui, app->view_port, GuiLayerFullscreen);

    bool running = true;
    while(running) {
        InputEvent event;
        if(furi_message_queue_get(app->input_queue, &event, 100) == FuriStatusOk) {
            if(event.type == InputTypeShort) {
                if(event.key == InputKeyOk) {
                    furi_mutex_acquire(app->mutex, FuriWaitForever);
                    bool rec = app->recording;
                    furi_mutex_release(app->mutex);
                    if(rec) {
                        recorder_stop(app);
                    } else {
                        recorder_start(app);
                    }
                } else if(event.key == InputKeyBack) {
                    furi_mutex_acquire(app->mutex, FuriWaitForever);
                    bool rec = app->recording;
                    furi_mutex_release(app->mutex);
                    if(rec) recorder_stop(app);
                    running = false;
                }
            }
        }
        view_port_update(app->view_port);
    }

    /* Safety: ensure recording is torn down. */
    if(app->recording || app->file || app->worker) {
        recorder_stop(app);
    }

    gui_remove_view_port(app->gui, app->view_port);
    view_port_free(app->view_port);
    furi_message_queue_free(app->input_queue);
    furi_record_close(RECORD_GUI);
    furi_record_close(RECORD_STORAGE);
    furi_mutex_free(app->mutex);
    free(app);
    return 0;
}
