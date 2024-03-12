#pragma once

#include "params.hpp"

inline float mapfloat(float x, float in_min, float in_max, float out_min, float out_max) {
    return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

/// Checks if the roboteq response is ok
bool roboteq_res_ok(String &res) {
    res.trim();
    return res == "+";
}