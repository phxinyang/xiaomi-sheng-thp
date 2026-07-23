// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>
#include <memory>
#include <string_view>
#include <vector>

// Stock MIPP pen commands over GATT 0000fe11-aa6c-462a-964a-7f2ed5b3e512
// (MiuiBleOobHelperService).
class FocusPenHaptics {
public:
    // Vibration: 5e 02 <type> <amplitude>  (prefix {94,2})
    static constexpr std::uint8_t kVibrateTypeStop = 0;
    static constexpr std::uint8_t kVibrateTypePinchDown = 1;
    static constexpr std::uint8_t kVibrateTypePinchUp = 2;
    static constexpr std::uint8_t kVibrateTypeDoublePress = 3;
    static constexpr std::uint8_t kVibrateTypeSlide = 4;
    static constexpr std::uint8_t kDefaultAmplitude = 0x80;

    virtual ~FocusPenHaptics() = default;

    virtual void setDeviceAddress(std::string_view address) noexcept = 0;

    // Generic raw write (for stock multi-opcode fe11 commands).
    virtual void sendRaw(std::vector<std::uint8_t> payload) noexcept = 0;

    // Vibration family: 5e 02 type amp
    virtual void vibrate(std::uint8_t type,
                         std::uint8_t amplitude = kDefaultAmplitude) noexcept = 0;
    virtual void scheduleDoublePress() noexcept = 0;
    virtual void triggerSlide() noexcept = 0;
    virtual void triggerPinch(bool pressed) noexcept = 0;
    virtual void stopVibrate() noexcept = 0;

    // Bees (buzzer): 60 01 00/01
    virtual void setBees(bool enabled) noexcept = 0;

    // Double-tap feature enable on pen: 5f 01 00/01
    virtual void setDoubleTapEnabled(bool enabled) noexcept = 0;

    // Writing-feedback motor: 59 02 <pen_type 0-4> <level 0-5>
    virtual void setWritingFeedback(std::uint8_t pen_type,
                                    std::uint8_t level) noexcept = 0;

    // Pinch motor feedback level: 5a 01 <level 0-5>
    virtual void setPinchMotorLevel(std::uint8_t level) noexcept = 0;

    virtual void reset() noexcept = 0;
};

std::unique_ptr<FocusPenHaptics> makeFocusPenHaptics();
