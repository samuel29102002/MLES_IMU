#pragma once

// ===== Assignment-wide tunables =====
#define SAMPLE_HZ     100       // sampling rate [Hz]
#define WIN_MS        1000      // window length [ms]
#define HOP_MS        500       // hop length [ms]

// Print/Log toggles
#define LOG_RAW       0         // 1: print per-sample raw CSV
#define LOG_FEATURES  1         // 1: print per-window feature CSV
#define PRINT_DEBUG   0         // 1: print "GESTURE: ..." friendly lines
#define PRINT_WARN    0         // 1: print WARN lines (e.g., drift)

// Feature switches
#define USE_GYRO      1         // include gyro-based features
#define USE_FFT       1         // compute FFT-derived features
#define USE_QUANT     0         // quantize final feature vector (u8) for logging

// CSV header (matches firmware printf order)
#define CSV_HEADER \
"t_ms,ax,ay,az,gx,gy,gz,amag_mean,amag_std,amag_rms,energy,dom_freq,bp1,bp2,gx_std,gy_std,gz_std,d_pitch_std,d_roll_std,class,lat_ms,q_len"
