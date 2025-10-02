#include <math.h>
#include "classifier.h"

const char* gesture_name(int cls) {
    switch (cls) {
        case G_SHAKE: return "SHAKE";
        case G_TILT:  return "TILT";
        case G_CIRCLE:return "CIRCLE";
        case G_NONE:
        default:      return "NONE";
    }
}

// Simple rule-based thresholds
int classify(const feat_vec_t* f) {
    float s = f->amag.std;
    float df = f->amag.dom_freq;
    float gsum = (f->gx_std + f->gy_std + f->gz_std) / 3.0f;

    // SHAKE: high std + higher frequency band
    if (s > 0.05f && df >= 3.0f) return G_SHAKE;

    // TILT: low frequency and moderate motion
    if (df > 0.2f && df < 2.0f && s > 0.01f && s < 0.3f) return G_TILT;

    // CIRCLE: moderate gyro stds, freq mid-range
    if (gsum > 10.0f && df >= 1.0f && df <= 3.0f) return G_CIRCLE;

    return G_NONE;
}
