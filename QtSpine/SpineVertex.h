#pragma once
#include <stdint.h>
struct SpineVertex {
    float x, y;
    float u, v;
    uint32_t argb;
    uint32_t dark_argb;   // Tint Black 暗色，无该特性时填 0
};