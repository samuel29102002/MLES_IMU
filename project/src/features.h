#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    float mean;
    float std;
    float rms;
    float energy;
    float dom_freq;  // Hz
    float bp1;       // bandpower 0.5–3 Hz
    float bp2;       // bandpower 3–10 Hz
} amag_feats_t;

typedef struct {
    amag_feats_t amag;
    float gx_std, gy_std, gz_std;       // gyro stability
    float d_pitch_std, d_roll_std;      // orientation deltas
} feat_vec_t;

// Compute features for one window (lab-style)
void compute_features(const float* ax, const float* ay, const float* az,
                      const float* gx, const float* gy, const float* gz,
                      int n, float fs_hz, feat_vec_t* out);

// Optional: quantize feature vector to u8 (for logging/bandwidth tests)
void quantize_features_u8(const feat_vec_t* f, uint8_t* out_buf, int* out_len);

#ifdef __cplusplus
}
#endif
