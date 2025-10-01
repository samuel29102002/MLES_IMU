// project/src/features.c
#include <math.h>
#include <string.h>
#include <stdint.h>
#include "features.h"

#ifndef SPECTRAL_METHOD_GOERTZEL
#define SPECTRAL_METHOD_GOERTZEL 1
#endif

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// ===================== basic stats =====================

static void stats_basic(const float* x, int n, float* mean, float* std, float* rms, float* energy) {
    double s = 0.0, sq = 0.0;
    for (int i = 0; i < n; i++) {
        s  += (double)x[i];
        sq += (double)x[i] * (double)x[i];
    }
    const float m = (float)(s / (double)n);
    float v = (float)(sq / (double)n) - m * m;
    if (v < 0) v = 0.0f;

    *mean   = m;
    *std    = sqrtf(v);
    *rms    = sqrtf((float)(sq / (double)n));
    *energy = (float)sq;                 // un-normalized energy (sum of squares)
}

// ===================== tiny spectral helpers =====================

static void hann_window(int n, float *w) {
    if (n <= 0) return;
    if (n == 1) { w[0] = 1.0f; return; }

    static int cached_n = 0;
    static float cached[2048];

    const int max_len = (int)(sizeof(cached) / sizeof(cached[0]));
    if (n > max_len) n = max_len;

    if (cached_n != n) {
        for (int i = 0; i < n; i++) {
            cached[i] = 0.5f * (1.0f - cosf(2.0f * (float)M_PI * (float)i / (float)(n - 1)));
        }
        cached_n = n;
    }

    memcpy(w, cached, (size_t)n * sizeof(float));
}

static void spectral_features_capped(const float *x, int n, float fs,
                                     float *dom_freq, float *bp1, float *bp2)
{
    enum { MAX_SAMPLES = 2048, MAX_CAPPED_BINS = 256 };
    static float work[MAX_SAMPLES];
    static float win[MAX_SAMPLES];

    if (n <= 1 || fs <= 0.0f) { *dom_freq = 0.0f; *bp1 = 0.0f; *bp2 = 0.0f; return; }
    if (n > MAX_SAMPLES) n = MAX_SAMPLES;

    double sum = 0.0;
    for (int i = 0; i < n; i++) sum += (double)x[i];
    const float mean = (float)(sum / (double)n);
    for (int i = 0; i < n; i++) work[i] = x[i] - mean;

    hann_window(n, win);
    for (int i = 0; i < n; i++) work[i] *= win[i];

    const float df = fs / (float)n;
    if (df <= 0.0f) { *dom_freq = 0.0f; *bp1 = 0.0f; *bp2 = 0.0f; return; }

    const int nyquist = n / 2;
    int kmax = (int)floorf(10.0f / df);
    if (kmax > nyquist) kmax = nyquist;
    if (kmax < 1) { *dom_freq = 0.0f; *bp1 = 0.0f; *bp2 = 0.0f; return; }
    if (kmax > MAX_CAPPED_BINS) kmax = MAX_CAPPED_BINS;

    double bp1_acc = 0.0;
    double bp2_acc = 0.0;
    float best_mag2 = 0.0f;
    float best_freq = 0.0f;

#if SPECTRAL_METHOD_GOERTZEL
    for (int k = 1; k <= kmax; k++) {
        const double omega = 2.0 * M_PI * (double)k / (double)n;
        const double cosw = cos(omega);
        const double sinw = sin(omega);
        const double coeff = 2.0 * cosw;

        double s_prev = 0.0;
        double s_prev2 = 0.0;
        for (int i = 0; i < n; i++) {
            const double s_val = (double)work[i] + coeff * s_prev - s_prev2;
            s_prev2 = s_prev;
            s_prev = s_val;
        }

        const double real = s_prev - s_prev2 * cosw;
        const double imag = s_prev2 * sinw;
        const float mag2 = (float)(real * real + imag * imag);

        const float freq = df * (float)k;
        if (mag2 > best_mag2) { best_mag2 = mag2; best_freq = freq; }
        if (freq >= 0.5f && freq < 3.0f) bp1_acc += (double)mag2;
        else if (freq >= 3.0f && freq <= 10.0f) bp2_acc += (double)mag2;
    }
#else
    enum { MAX_BINS = MAX_CAPPED_BINS };
    int bin_count = kmax;
    if (bin_count > MAX_BINS) bin_count = MAX_BINS;

    float cos_angle[MAX_BINS];
    float sin_angle[MAX_BINS];
    float cos_state[MAX_BINS];
    float sin_state[MAX_BINS];
    double real_acc[MAX_BINS];
    double imag_acc[MAX_BINS];

    for (int idx = 0; idx < bin_count; idx++) {
        const int k = idx + 1;
        const float angle = 2.0f * (float)M_PI * (float)k / (float)n;
        cos_angle[idx] = cosf(angle);
        sin_angle[idx] = sinf(angle);
        cos_state[idx] = 1.0f;
        sin_state[idx] = 0.0f;
        real_acc[idx] = 0.0;
        imag_acc[idx] = 0.0;
    }

    for (int i = 0; i < n; i++) {
        const double sample = (double)work[i];
        for (int idx = 0; idx < bin_count; idx++) {
            real_acc[idx] += sample * (double)cos_state[idx];
            imag_acc[idx] -= sample * (double)sin_state[idx];

            const float c = cos_state[idx];
            const float s = sin_state[idx];
            const float cos_next = c * cos_angle[idx] - s * sin_angle[idx];
            const float sin_next = s * cos_angle[idx] + c * sin_angle[idx];
            cos_state[idx] = cos_next;
            sin_state[idx] = sin_next;
        }
    }

    for (int idx = 0; idx < bin_count; idx++) {
        const float freq = df * (float)(idx + 1);
        const float mag2 = (float)(real_acc[idx] * real_acc[idx] + imag_acc[idx] * imag_acc[idx]);
        if (mag2 > best_mag2) { best_mag2 = mag2; best_freq = freq; }
        if (freq >= 0.5f && freq < 3.0f) bp1_acc += (double)mag2;
        else if (freq >= 3.0f && freq <= 10.0f) bp2_acc += (double)mag2;
    }
#endif

    if (best_mag2 <= 0.0f) best_freq = 0.0f;
    *dom_freq = best_freq;
    *bp1 = (float)bp1_acc;
    *bp2 = (float)bp2_acc;
}

// ===================== public API =====================

void compute_features(const float* ax, const float* ay, const float* az,
                      const float* gx, const float* gy, const float* gz,
                      int n, float fs_hz, feat_vec_t* out)
{
    memset(out, 0, sizeof(*out));

    // 1) accel magnitude
    static float amag[2048];
    if (n > (int)(sizeof(amag) / sizeof(amag[0]))) n = (int)(sizeof(amag) / sizeof(amag[0]));
    for (int i = 0; i < n; i++) {
        const float x = ax[i], y = ay[i], z = az[i];
        amag[i] = sqrtf(x * x + y * y + z * z);
    }

    // 2) time-domain stats
    stats_basic(amag, n, &out->amag.mean, &out->amag.std, &out->amag.rms, &out->amag.energy);

    // 3) spectrum on demeaned amag (dominant freq + bandpowers)
    spectral_features_capped(amag, n, fs_hz, &out->amag.dom_freq, &out->amag.bp1, &out->amag.bp2);

    // 4) gyro stability (std only)
    float m, s, r, e;
    stats_basic(gx, n, &m, &s, &r, &e); out->gx_std = s;
    stats_basic(gy, n, &m, &s, &r, &e); out->gy_std = s;
    stats_basic(gz, n, &m, &s, &r, &e); out->gz_std = s;

    // 5) simple orientation deltas (placeholder: scaled gyro stds)
    out->d_pitch_std = out->gy_std * 0.001f;
    out->d_roll_std  = out->gx_std * 0.001f;
}

void quantize_features_u8(const feat_vec_t* f, uint8_t* out_buf, int* out_len) {
    // Layout: [amag_std, dom_freq/10, gx_std/300, gy_std/300, gz_std/300]
    float v[5] = {
        f->amag.std,
        f->amag.dom_freq / 10.0f,
        f->gx_std / 300.0f,
        f->gy_std / 300.0f,
        f->gz_std / 300.0f
    };
    int k = 0;
    for (int i = 0; i < 5; i++) {
        float x = v[i];
        if (x < 0) x = 0;
        if (x > 1) x = 1;
        out_buf[k++] = (uint8_t)lrintf(x * 255.0f);
    }
    if (out_len) *out_len = k;
}
