// project/src/main.c
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <math.h>

#include "pico/stdlib.h"
#include "pico/time.h"
#include "hardware/i2c.h"

#include "ff.h"
#include "sd_card.h"
#include "f_util.h"
#include "hw_config.h"

#include "config.h"
#include "icm20948.h"
#include "filters.h"
#include "features.h"
#include "classifier.h"
#include "csv_logger.h"

// -------------------- User-tunable basics --------------------
#define CALIB_DURATION_SEC 2


// -------------------- IMU scaling (ICM-20948) ----------------
#define ACCEL_SCALE_G      (1.0f / 16384.0f)
#define GYRO_SCALE_DPS     (1.0f / 32.8f)

// -------------------- CSV logging ----------------------------
#define CSV_PATH_MAX  96
#define CSV_LINE_MAX  192

static FATFS g_fs;                     // FatFs must persist for mount lifetime
static sd_card_t *g_sd = NULL;
static const char *g_drive_prefix = NULL;
static csv_logger_t g_csv_logger;
static bool g_csv_logger_ready = false;
static bool g_csv_logger_failed = false;

static const char kCsvHeader[] =
    "t_ms,ax,ay,az,gx,gy,gz,amag_std,dom_freq,bp1,bp2,gx_std,gy_std,gz_std,cls,lat_ms,qbytes";

// -------------------- Derived sizes --------------------------
#define WIN_SAMPLES ((SAMPLE_HZ * WIN_MS) / 1000)
#define HOP_SAMPLES ((SAMPLE_HZ * HOP_MS) / 1000)

_Static_assert(WIN_SAMPLES > 0, "WIN_MS must yield at least one sample");
_Static_assert(HOP_SAMPLES > 0, "HOP_MS must yield at least one sample");

// -------------------- Helpers -------------------------------
static inline absolute_time_t add_interval(absolute_time_t t, uint32_t delta_us) {
    return delayed_by_us(t, delta_us);
}

static inline void report_fresult(const char *op, FRESULT fr) {
    printf("%s -> %s (%d)\n", op, FRESULT_str(fr), fr);
}

static void format_csv_line(char *out, size_t n,
                            uint32_t t_ms,
                            float ax, float ay, float az,
                            float gx, float gy, float gz,
                            float amag_std, float dom_f, float bp1, float bp2,
                            float gx_std, float gy_std, float gz_std,
                            int cls, float lat_ms, int qbytes) {
    snprintf(out, n,
             "%u,%.5f,%.5f,%.5f,%.5f,%.5f,%.5f,%.5f,%.5f,%.5f,%.5f,%.5f,%.5f,%.5f,%d,%.3f,%d",
             t_ms, ax, ay, az, gx, gy, gz,
             amag_std, dom_f, bp1, bp2, gx_std, gy_std, gz_std,
             cls, lat_ms, qbytes);
}

static bool ensure_sd_mounted(void) {
    if (g_drive_prefix) {
        return true;
    }

    if (!sd_init_driver()) {
        printf("sd_init_driver() failed\n");
        return false;
    }

    g_sd = sd_get_by_num(0);
    if (!g_sd) {
        printf("sd_get_by_num(0) returned NULL\n");
        return false;
    }

    g_drive_prefix = sd_get_drive_prefix(g_sd);
    if (!g_drive_prefix) {
        printf("sd_get_drive_prefix() returned NULL\n");
        return false;
    }

    FRESULT fr = f_mount(&g_fs, g_drive_prefix, 1);
    if (fr == FR_NO_FILESYSTEM) {
        BYTE work[4096];
        MKFS_PARM opt = { FM_FAT | FM_SFD, 0, 0, 0, 0 };
        report_fresult("f_mount (no filesystem)", fr);
        fr = f_mkfs(g_drive_prefix, &opt, work, sizeof work);
        if (fr != FR_OK) {
            report_fresult("f_mkfs", fr);
            return false;
        }
        fr = f_mount(&g_fs, g_drive_prefix, 1);
    }

    if (fr != FR_OK) {
        report_fresult("f_mount", fr);
        return false;
    }

    return true;
}

static bool init_csv_logging(void) {
    if (g_csv_logger_ready) return true;
    if (g_csv_logger_failed) return false;

    if (!ensure_sd_mounted()) {
        g_csv_logger_failed = true;
        return false;
    }

    char logs_dir[CSV_PATH_MAX];
    snprintf(logs_dir, sizeof logs_dir, "%s/logs", g_drive_prefix);
    FRESULT fr = f_mkdir(logs_dir);
    if (fr != FR_OK && fr != FR_EXIST) {
        report_fresult("f_mkdir(logs)", fr);
        g_csv_logger_failed = true;
        return false;
    }

    uint32_t session_ms = to_ms_since_boot(get_absolute_time());
    char file_path[CSV_PATH_MAX];
    snprintf(file_path, sizeof file_path, "%s/session_%lu.csv",
             logs_dir, (unsigned long)session_ms);

    fr = csv_open(&g_csv_logger, file_path, kCsvHeader);
    if (fr != FR_OK) {
        report_fresult("csv_open", fr);
        g_csv_logger_failed = true;
        return false;
    }

    g_csv_logger_ready = true;
    printf("SD logging to %s\n", file_path);
    return true;
}

static void append_csv_line(uint32_t t_ms,
                            float ax, float ay, float az,
                            float gx, float gy, float gz,
                            float amag_std, float dom_f, float bp1, float bp2,
                            float gx_std, float gy_std, float gz_std,
                            int cls, float lat_ms, int qbytes) {
    if (g_csv_logger_failed) return;
    if (!g_csv_logger_ready) {
        if (!init_csv_logging()) {
            printf("CSV logger disabled (init failed).\n");
            return;
        }
    }

    char line[CSV_LINE_MAX];
    format_csv_line(line, sizeof line,
                    t_ms, ax, ay, az,
                    gx, gy, gz,
                    amag_std, dom_f, bp1, bp2,
                    gx_std, gy_std, gz_std,
                    cls, lat_ms, qbytes);

    FRESULT fr = csv_append(&g_csv_logger, line);
    if (fr != FR_OK) {
        report_fresult("csv_append", fr);
        csv_close(&g_csv_logger);
        g_csv_logger_ready = false;
        g_csv_logger_failed = true;
    }
}

#if LOG_FEATURES
// copy a logical window from the ring buffer to a linear buffer
static void copy_window(float *dst, const float *ring, int ring_size, int start_idx) {
    for (int i = 0; i < ring_size; i++) {
        int idx = start_idx + i;
        if (idx >= ring_size) idx -= ring_size;
        dst[i] = ring[idx];
    }
}
#endif

int main(void) {
    // ---- USB CDC stdout init (make prints visible) ----
    stdio_init_all();
    sleep_ms(1000);
    setvbuf(stdout, NULL, _IONBF, 0);

    printf("PICO IMU features build starting...\n");
    printf("SAMPLE_HZ=%d, WIN_MS=%d, HOP_MS=%d, LOG_RAW=%d, LOG_FEATURES=%d, USE_GYRO=%d, USE_FFT=%d, USE_QUANT=%d\n",
           SAMPLE_HZ, WIN_MS, HOP_MS, LOG_RAW, LOG_FEATURES, USE_GYRO, USE_FFT, USE_QUANT);

    // ---- IMU init (ICM-20948) ----
    IMU_EN_SENSOR_TYPE sensor_type = IMU_EN_SENSOR_TYPE_NULL;
    printf("Initializing IMU...\n");
    imuInit(&sensor_type);
    if (sensor_type != IMU_EN_SENSOR_TYPE_ICM20948) {
        printf("Error: ICM-20948 not detected (type=%d). Halting.\n", (int)sensor_type);
        while (true) { sleep_ms(1000); }
    }
    printf("ICM-20948 detected.\n");

    const uint32_t sample_period_us = 1000000u / SAMPLE_HZ;
    const uint32_t calib_samples = SAMPLE_HZ * CALIB_DURATION_SEC;

#if PRINT_DEBUG
    printf("Calibrating IMU for %u samples (~%d s). Keep device still...\n",
           calib_samples, CALIB_DURATION_SEC);
#endif

    float sum_ax = 0.0f, sum_ay = 0.0f, sum_az = 0.0f;
    float sum_gx = 0.0f, sum_gy = 0.0f, sum_gz = 0.0f;

    IMU_ST_SENSOR_DATA gyro_raw = {0};
    IMU_ST_SENSOR_DATA accel_raw = {0};

    // pace calibration at target rate
    absolute_time_t next_tick = get_absolute_time();
    for (uint32_t i = 0; i < calib_samples; ++i) {
        next_tick = add_interval(next_tick, sample_period_us);
        sleep_until(next_tick);

        // read raw
        imuDataAccGyrGet(&gyro_raw, &accel_raw);

        // accumulate scaled values
        sum_ax += (float)accel_raw.s16X * ACCEL_SCALE_G;
        sum_ay += (float)accel_raw.s16Y * ACCEL_SCALE_G;
        sum_az += (float)accel_raw.s16Z * ACCEL_SCALE_G;

        sum_gx += (float)gyro_raw.s16X * GYRO_SCALE_DPS;
        sum_gy += (float)gyro_raw.s16Y * GYRO_SCALE_DPS;
        sum_gz += (float)gyro_raw.s16Z * GYRO_SCALE_DPS;
    }

    const float bias_ax = sum_ax / (float)calib_samples;
    const float bias_ay = sum_ay / (float)calib_samples;
    const float bias_az = sum_az / (float)calib_samples;
    const float bias_gx = sum_gx / (float)calib_samples;
    const float bias_gy = sum_gy / (float)calib_samples;
    const float bias_gz = sum_gz / (float)calib_samples;

#if PRINT_DEBUG
    printf("Calibration done. Bias accel[g]: %.5f %.5f %.5f | gyro[dps]: %.5f %.5f %.5f\n",
           bias_ax, bias_ay, bias_az, bias_gx, bias_gy, bias_gz);
#endif

    if (!init_csv_logging()) {
        printf("SD logging not active (initialization failed).\n");
    }

    // --------- Buffers for windowed feature computation ----------
#if LOG_FEATURES
    static float ax_ring[WIN_SAMPLES] = {0};
    static float ay_ring[WIN_SAMPLES] = {0};
    static float az_ring[WIN_SAMPLES] = {0};
    static float gx_ring[WIN_SAMPLES] = {0};
    static float gy_ring[WIN_SAMPLES] = {0};
    static float gz_ring[WIN_SAMPLES] = {0};

    int ring_index = 0;   // next write position
    int ring_filled = 0;  // up to WIN_SAMPLES
    int hop_accum  = 0;   // samples since last window
#endif

    // --------- CSV headers ----------
#if LOG_RAW
    printf("t_ms,ax,ay,az,gx,gy,gz\n");
#endif
#if LOG_FEATURES
    printf(CSV_HEADER "\n");
#endif

    // main sampling loop
    next_tick = get_absolute_time();
    const uint32_t t_start_ms = to_ms_since_boot(get_absolute_time());
    uint64_t last_sample_us = time_us_64();
    uint64_t next_rate_warn_us = last_sample_us;

    while (true) {
        // pace to target sampling rate
        next_tick = add_interval(next_tick, sample_period_us);
        sleep_until(next_tick);

        // read raw
        imuDataAccGyrGet(&gyro_raw, &accel_raw);

        // scale + bias-correct
        float ax = (float)accel_raw.s16X * ACCEL_SCALE_G - bias_ax;
        float ay = (float)accel_raw.s16Y * ACCEL_SCALE_G - bias_ay;
        float az = (float)accel_raw.s16Z * ACCEL_SCALE_G - bias_az;
        float gx = (float)gyro_raw.s16X * GYRO_SCALE_DPS - bias_gx;
        float gy = (float)gyro_raw.s16Y * GYRO_SCALE_DPS - bias_gy;
        float gz = (float)gyro_raw.s16Z * GYRO_SCALE_DPS - bias_gz;

        // simple rate monitor
        const uint64_t sample_time_us = time_us_64();
        const uint64_t dt_us = sample_time_us - last_sample_us;
        if (dt_us > 0) {
            const float actual_hz = 1000000.0f / (float)dt_us;
            const float drift = fabsf(actual_hz - (float)SAMPLE_HZ) / (float)SAMPLE_HZ;
            if (drift > 0.05f && sample_time_us >= next_rate_warn_us) {
                printf("WARN: sample rate drift=%.2f%% (%.2f Hz vs %d Hz)\n",
                       drift * 100.0f, actual_hz, SAMPLE_HZ);
                next_rate_warn_us = sample_time_us + 1000000u; // throttle to 1 Hz
            }
        }
        last_sample_us = sample_time_us;

        const uint32_t now_ms = to_ms_since_boot(get_absolute_time());
        const uint32_t t_ms = now_ms - t_start_ms;

#if LOG_RAW
        // per-sample CSV (useful for debugging or offline feature checks)
        printf("%lu,%.5f,%.5f,%.5f,%.5f,%.5f,%.5f\n",
               (unsigned long)t_ms, ax, ay, az, gx, gy, gz);
#endif

#if LOG_FEATURES
        // update rings
        ax_ring[ring_index] = ax;
        ay_ring[ring_index] = ay;
        az_ring[ring_index] = az;
        gx_ring[ring_index] = gx;
        gy_ring[ring_index] = gy;
        gz_ring[ring_index] = gz;

        ring_index++;
        if (ring_index >= WIN_SAMPLES) ring_index = 0;
        if (ring_filled < WIN_SAMPLES) ring_filled++;

        hop_accum++;

        // when a full window is available and hop reached, compute features
        if (ring_filled == WIN_SAMPLES && hop_accum >= HOP_SAMPLES) {
            hop_accum = 0;

            float ax_win[WIN_SAMPLES];
            float ay_win[WIN_SAMPLES];
            float az_win[WIN_SAMPLES];
            float gx_win[WIN_SAMPLES];
            float gy_win[WIN_SAMPLES];
            float gz_win[WIN_SAMPLES];

            // ring_index points to the NEXT write position -> it's also the start of the logical window
            copy_window(ax_win, ax_ring, WIN_SAMPLES, ring_index);
            copy_window(ay_win, ay_ring, WIN_SAMPLES, ring_index);
            copy_window(az_win, az_ring, WIN_SAMPLES, ring_index);
            copy_window(gx_win, gx_ring, WIN_SAMPLES, ring_index);
            copy_window(gy_win, gy_ring, WIN_SAMPLES, ring_index);
            copy_window(gz_win, gz_ring, WIN_SAMPLES, ring_index);

            const uint64_t t0 = time_us_64();

            feat_vec_t feat;
            compute_features(ax_win, ay_win, az_win, gx_win, gy_win, gz_win,
                             WIN_SAMPLES, (float)SAMPLE_HZ, &feat);

            const int cls = classify(&feat);
            const float lat_ms = (float)(time_us_64() - t0) / 1000.0f;

            int q_len = 0;
#if USE_QUANT
            uint8_t qbuf[64];
            quantize_features_u8(&feat, qbuf, &q_len);
#endif


#if USE_GYRO
            const float gx_sample = gx;
            const float gy_sample = gy;
            const float gz_sample = gz;
            const float gx_std_val = feat.gx_std;
            const float gy_std_val = feat.gy_std;
            const float gz_std_val = feat.gz_std;
#else
            const float gx_sample = 0.0f;
            const float gy_sample = 0.0f;
            const float gz_sample = 0.0f;
            const float gx_std_val = 0.0f;
            const float gy_std_val = 0.0f;
            const float gz_std_val = 0.0f;
#endif

            // per-window CSV (matches CSV_HEADER in config.h)
            printf("%lu,%.5f,%.5f,%.5f,%.5f,%.5f,%.5f,%.5f,%.5f,%.5f,%.5f,%.5f,%.5f,%.5f,%.5f,%.5f,%.5f,%.5f,%.5f,%d,%.3f,%d\n",
                   (unsigned long)t_ms,
                   ax, ay, az,
                   gx_sample, gy_sample, gz_sample,
                   feat.amag.mean,
                   feat.amag.std,
                   feat.amag.rms,
                   feat.amag.energy,
                   feat.amag.dom_freq,
                   feat.amag.bp1,
                   feat.amag.bp2,
                   gx_std_val,
                   gy_std_val,
                   gz_std_val,
                   feat.d_pitch_std,
                   feat.d_roll_std,
                   cls,
                   lat_ms,
                   q_len);

            append_csv_line(t_ms,
                            ax, ay, az,
                            gx_sample, gy_sample, gz_sample,
                            feat.amag.std,
                            feat.amag.dom_freq,
                            feat.amag.bp1,
                            feat.amag.bp2,
                            gx_std_val,
                            gy_std_val,
                            gz_std_val,
                            cls,
                            lat_ms,
                            q_len);

            if (lat_ms > 20.0f) {
                printf("WARN: feature latency=%.2f ms (OVERRUN)\n", lat_ms);
            }

#if PRINT_DEBUG
            printf("GESTURE: %s (lat=%.1f ms) dom=%.2fHz std=%.2f bp1=%.2f bp2=%.2f\n",
                   gesture_name(cls), lat_ms, feat.amag.dom_freq,
                   feat.amag.std, feat.amag.bp1, feat.amag.bp2);
#endif
        }
#endif
    }

    if (g_csv_logger_ready) {
        csv_close(&g_csv_logger);
    }

    return 0;
}
