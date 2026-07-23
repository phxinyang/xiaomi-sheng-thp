// SPDX-License-Identifier: Apache-2.0

#include "focus_pen_haptics.hpp"

namespace {

class NoopFocusPenHaptics final : public FocusPenHaptics {
public:
    void setDeviceAddress(std::string_view) noexcept override {}
    void sendRaw(std::vector<std::uint8_t>) noexcept override {}
    void vibrate(std::uint8_t, std::uint8_t) noexcept override {}
    void scheduleDoublePress() noexcept override {}
    void triggerSlide() noexcept override {}
    void triggerPinch(bool) noexcept override {}
    void stopVibrate() noexcept override {}
    void setBees(bool) noexcept override {}
    void setDoubleTapEnabled(bool) noexcept override {}
    void setWritingFeedback(std::uint8_t, std::uint8_t) noexcept override {}
    void setPinchMotorLevel(std::uint8_t) noexcept override {}
    void reset() noexcept override {}
};

}  // namespace

std::unique_ptr<FocusPenHaptics> makeFocusPenHaptics() {
    return std::make_unique<NoopFocusPenHaptics>();
}
