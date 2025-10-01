#pragma once
#ifdef __cplusplus
extern "C" {
#endif

#include "features.h"

enum {
    G_NONE = 0,
    G_SHAKE = 1,
    G_TILT = 2,
    G_CIRCLE = 3
};

int classify(const feat_vec_t* f);
const char* gesture_name(int cls);

#ifdef __cplusplus
}
#endif
