// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>
#include <memory>
#include <string_view>
#include <vector>

// Stock MIPP pen commands over GATT 0000fe11-aa6c-462a-964a-7f2ed5b3e512.
// Spec: focus-pen-stack/stock-thp/haptics/STOCK-FOCUS-PEN-BLE.md
class FocusPenHaptics {
public:
    // Vibration: 5e 02 <type> <amplitude>
    static constexpr std::uint8_t kVibrateTypeStop = 0;
    static constexpr std::uint8_t kVibrateTypePinchDown = 1;
    static constexpr std::uint8_t kVibrateTypePinchUp = 2;
    static constexpr std::uint8_t kVibrateTypeDoublePress = 3;
    static constexpr std::uint8_t kVibrateTypeSlide = 4;
    static constexpr std::uint8_t kDefaultAmplitude = 0x80;

    // Official sheng.xml pinch threshold levels (trigger/release).
    enum class PinchThresholdLevel {
        ExtremeWeak = 0,  // 90 / 45
        Weak = 1,         // 180 / 90
        Medium = 2,       // 270 / 135
        Strong = 3,       // 360 / 180
        ExtremeStrong = 4 // 450 / 225
    };

    virtual ~FocusPenHaptics() = default;

    virtual void setDeviceAddress(std::string_view address) noexcept = 0;
    virtual void sendRaw(std::vector<std::uint8_t> payload) noexcept = 0;

    // 5e 02 type amp — gesture vibration only (types 3/4 in production).
    virtual void vibrate(std::uint8_t type,
                         std::uint8_t amplitude = kDefaultAmplitude) noexcept = 0;
    virtual void scheduleDoublePress() noexcept = 0;
    virtual void triggerSlide() noexcept = 0;
    virtual void stopVibrate() noexcept = 0;

    // Pro connect init (STOCK §3.4 / §7 P0):
    //   5f 01 01 | 5a 01 03 | 5c 04 <thr> | 61 01 01
    virtual void setDoubleTapEnabled(bool enabled) noexcept = 0;
    virtual void setPinchMotorLevel(std::uint8_t level) noexcept = 0;
    virtual void setPinchThreshold(std::uint16_t trigger,
                                   std::uint16_t release) noexcept = 0;
    virtual void setPinchThresholdLevel(PinchThresholdLevel level) noexcept = 0;
    virtual void setScreenState(bool on) noexcept = 0;

    // Optional / settings-owned (not default-on for Pro init):
    virtual void setBees(bool enabled) noexcept = 0;
    virtual void setWritingFeedback(std::uint8_t pen_type,
                                    std::uint8_t level) noexcept = 0;
    // P1: mirror pinch key as A+G state (not vibration).
    virtual void setAccelGyroState(bool active) noexcept = 0;
    // P1: stylus up/move.
    virtual void setStylusUpMove(bool active) noexcept = 0;

    virtual void reset() noexcept = 0;
};

std::unique_ptr<FocusPenHaptics> makeFocusPenHaptics();
