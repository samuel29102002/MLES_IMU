#include <stdio.h>
#include <math.h>
#include <stdint.h>
#include "pico/stdlib.h"

static inline float clampf(float v, float lo, float hi) {
    return (v < lo) ? lo : (v > hi) ? hi : v;
}

// Quantize float in ~[-1,1) to a signed fixed-point integer with "bits" total bits
static void quantize_bits(const float *x, int n, int bits, void *q_out,
                          size_t elem_size, int *clip_count) {
    const int32_t scale = 1 << (bits - 1);          // e.g., 32768 for Q15
    const int32_t max_q = scale - 1;                // 0x7FFF ...
    const int32_t min_q = -scale;                   // 0x8000 ...
    const float hi = (float)max_q / (float)scale;   // avoid +1.0 overflow
    const float lo = -1.0f;

    int clips = 0;
    for (int i = 0; i < n; i++) {
        float s = clampf(x[i], lo, hi);
        if (s != x[i]) clips++;

        int32_t q = (int32_t)lrintf(s * (float)scale);
        if (q > max_q) q = max_q;
        if (q < min_q) q = min_q;

        if (elem_size == sizeof(int16_t)) {
            ((int16_t *)q_out)[i] = (int16_t)q;
        } else if (elem_size == sizeof(int8_t)) {
            ((int8_t *)q_out)[i] = (int8_t)q;
        } else if (elem_size == sizeof(int32_t)) {
            ((int32_t *)q_out)[i] = q;
        }
    }

    if (clip_count) *clip_count = clips;
}

static void dequantize_bits(const void *q_in, int n, int bits, size_t elem_size, float *y) {
    const float inv_scale = 1.0f / (float)(1 << (bits - 1));
    if (elem_size == sizeof(int16_t)) {
        const int16_t *src = (const int16_t *)q_in;
        for (int i = 0; i < n; i++) y[i] = (float)src[i] * inv_scale;
    } else if (elem_size == sizeof(int8_t)) {
        const int8_t *src = (const int8_t *)q_in;
        for (int i = 0; i < n; i++) y[i] = (float)src[i] * inv_scale;
    } else if (elem_size == sizeof(int32_t)) {
        const int32_t *src = (const int32_t *)q_in;
        for (int i = 0; i < n; i++) y[i] = (float)src[i] * inv_scale;
    }
}

static float rmsf(const float *x, int n) {
    double acc = 0.0;
    for (int i = 0; i < n; i++) acc += (double)x[i] * (double)x[i];
    return (float)sqrt(acc / (double)n);
}

static void snr_and_error(const float *ref, const float *test, int n,
                          float *snr_db, float *max_abs_err, float *rms_err) {
    double sig = 0.0, err = 0.0;
    float maxe = 0.0f;
    for (int i = 0; i < n; i++) {
        float e = ref[i] - test[i];
        sig += (double)ref[i] * (double)ref[i];
        err += (double)e * (double)e;
        if (fabsf(e) > maxe) maxe = fabsf(e);
    }
    float rms = (float)sqrt(err / (double)n);
    *rms_err = rms;
    *max_abs_err = maxe;
    if (err == 0.0) {
        *snr_db = INFINITY;
    } else {
        *snr_db = 10.0f * log10f((float)(sig / err));
    }
}

int main(void) {
    stdio_init_all();
    sleep_ms(1500);

    const float fs_hz = 2200.0f;  // example IMU sample rate
    const int axes = 3;           // accel/gyro example
    enum { N = 512 };

    // Synthetic test signal in [-1, 1) for repeatable validation
    float x[N];
    for (int i = 0; i < N; i++) {
        float t = (float)i / fs_hz;
        x[i] = 0.6f * sinf(2.0f * (float)M_PI * 7.0f * t)
              + 0.3f * sinf(2.0f * (float)M_PI * 23.0f * t);
    }

    const float signal_rms = rmsf(x, N);

    int16_t q16[N];
    int8_t  q8[N];
    int8_t  q4[N];

    float xq16[N];
    float xq8[N];
    float xq4[N];

    typedef struct {
        const char *label;
        int bits;
        void *quant;
        size_t elem_size;
        float storage_bytes_per_sample;
        float *dequant;
        int clips;
        float snr_db;
        float max_abs_err;
        float rms_err;
        float expected_rms_err;
        float expected_max_err;
        float expected_snr_db;
    } quant_case_t;

    quant_case_t cases[] = {
        { "Q15", 16, q16, sizeof(q16[0]), 2.0f, xq16, 0 },
        { "Q7",   8, q8, sizeof(q8[0]), 1.0f, xq8,  0 },
        { "Q3",   4, q4, sizeof(q4[0]), 0.5f, xq4, 0 },
    };

    const int num_cases = sizeof(cases) / sizeof(cases[0]);

    for (int i = 0; i < num_cases; i++) {
        quant_case_t *c = &cases[i];

        quantize_bits(x, N, c->bits, c->quant, c->elem_size, &c->clips);
        dequantize_bits(c->quant, N, c->bits, c->elem_size, c->dequant);
        snr_and_error(x, c->dequant, N, &c->snr_db, &c->max_abs_err, &c->rms_err);

        const float delta = 1.0f / (float)(1 << (c->bits - 1));
        c->expected_rms_err = delta / sqrtf(12.0f);
        c->expected_max_err = 0.5f * delta;
        c->expected_snr_db = 20.0f * log10f(signal_rms / c->expected_rms_err);
    }

    const float bytes_f32 = sizeof(float);
    const float block_bytes_f32 = bytes_f32 * (float)N;
    const float stream_f32 = (float)(axes * sizeof(float)) * fs_hz;

    printf("\n=== Quantization Validation (16/8/4-bit) ===\n");
    printf("Samples per block: %d\n", N);
    printf("Reference type: float32 (%.0f bytes/block)\n", block_bytes_f32);
    printf("Reference stream (@%d axes, %.1f Hz): %.1f kB/s\n",
           axes, fs_hz, stream_f32 / 1000.0f);
    printf("Signal RMS (reference): %.7f\n", signal_rms);

    for (int i = 0; i < num_cases; i++) {
        const quant_case_t *c = &cases[i];
        const float block_bytes = c->storage_bytes_per_sample * (float)N;
        const float stream_bytes = c->storage_bytes_per_sample * (float)axes * fs_hz;

        printf("\n[%s | %d-bit]\n", c->label, c->bits);
        printf("  Storage: %.2f bytes/sample  (block: %.1f bytes)\n", c->storage_bytes_per_sample, block_bytes);
        printf("  Streaming: %.1f kB/s\n", stream_bytes / 1000.0f);
        printf("  Clip count: %d (of %d)\n", c->clips, N);
        printf("  Actual SNR: %.2f dB\n", c->snr_db);
        printf("    Expected SNR (ideal): %.2f dB\n", c->expected_snr_db);
        printf("  RMS error: %.7f (expected: %.7f)\n", c->rms_err, c->expected_rms_err);
        printf("  Max |error|: %.7f (expected bound: %.7f)\n", c->max_abs_err, c->expected_max_err);
    }

    while (true) {
        tight_loop_contents();
    }

    return 0;
}

