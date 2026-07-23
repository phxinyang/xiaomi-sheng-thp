// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <algorithm>
#include <cmath>

// Stock /odm/etc/touch/Pencil_Posture.xml for NVTCapacitivePenP81c:
//   complementary: kp=40, ki=0.1, Fs=50, ACC_Kp=0.6
//   Butterworth input/output second-order sections (a0 = 1).
// Without a live IMU stream we treat THP tilt as the "accelerometer"
// measurement and run the same filter chain on each axis.

namespace posture {

struct Biquad {
    float b0 = 1.0F;
    float b1 = 0.0F;
    float b2 = 0.0F;
    float a1 = 0.0F;
    float a2 = 0.0F;
    float x1 = 0.0F;
    float x2 = 0.0F;
    float y1 = 0.0F;
    float y2 = 0.0F;

    void reset() {
        x1 = x2 = y1 = y2 = 0.0F;
    }

    float process(float x) {
        const float y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
        x2 = x1;
        x1 = x;
        y2 = y1;
        y1 = y;
        return y;
    }
};

struct AxisFilter {
    static constexpr float kFs = 50.0F;
    static constexpr float kDt = 1.0F / kFs;
    static constexpr float kKp = 40.0F;
    static constexpr float kKi = 0.1F;
    static constexpr float kAccKp = 0.6F;
    static constexpr float kIntegralLimit = 30.0F;

    Biquad input{
        0.029954582208092474F,
        0.059909164416184948F,
        0.029954582208092474F,
        -1.4542435862515848F,
        0.5740619150839547F,
    };
    Biquad output{
        0.14532388387704243F,
        0.29064776775408485F,
        0.14532388387704243F,
        -0.6710290907740962F,
        0.25232462628226593F,
    };
    float estimate = 0.0F;
    float integral = 0.0F;
    bool initialized = false;

    void reset() {
        input.reset();
        output.reset();
        estimate = 0.0F;
        integral = 0.0F;
        initialized = false;
    }

    float process(float raw) {
        const float measured = input.process(raw);
        if (!initialized) {
            estimate = measured;
            integral = 0.0F;
            initialized = true;
            return output.process(estimate);
        }

        // Complementary blend of measurement and previous estimate.
        estimate = kAccKp * measured + (1.0F - kAccKp) * estimate;

        // PI correction (kp is large; scale by dt so one sample is stable).
        const float error = measured - estimate;
        integral = std::clamp(integral + error * kDt, -kIntegralLimit,
                              kIntegralLimit);
        estimate += (kKp * error + kKi * integral) * kDt;

        return output.process(estimate);
    }
};

class PencilPostureFilter {
public:
    void reset() {
        tilt_x_.reset();
        tilt_y_.reset();
    }

    void filterTilt(int &tilt_x, int &tilt_y) {
        tilt_x = static_cast<int>(std::lround(tilt_x_.process(
            static_cast<float>(tilt_x))));
        tilt_y = static_cast<int>(std::lround(tilt_y_.process(
            static_cast<float>(tilt_y))));
        tilt_x = std::clamp(tilt_x, -60, 60);
        tilt_y = std::clamp(tilt_y, -60, 60);
    }

private:
    AxisFilter tilt_x_{};
    AxisFilter tilt_y_{};
};

}  // namespace posture
