#pragma once

inline float mapfloat(float x, float in_min, float in_max, float out_min, float out_max) {
    return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

/// Sets up roboteq settings
inline void setup_roboteq() {
    // Turn off echos
    Serial2.write("^ECHOF 1 _");
    // Echo and +
    Serial2.readStringUntil('\r');
    Serial2.readStringUntil('\r');

    // Set AMP limit to 140A
    Serial2.write("^ALIM 1 1400 _");
    Serial2.readStringUntil('\r');

    // Set overvolt to 53V
    Serial2.write("^OVL 530 _");
    Serial2.readStringUntil('\r');

    // Set undervolt to 48V
    Serial2.write("^UVL 480 _");
    Serial2.readStringUntil('\r');
}