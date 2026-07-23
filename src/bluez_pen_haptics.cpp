// SPDX-License-Identifier: Apache-2.0

#include "focus_pen_haptics.hpp"

#include <systemd/sd-bus.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <condition_variable>
#include <cctype>
#include <cstdlib>
#include <cstdint>
#include <deque>
#include <exception>
#include <iostream>
#include <memory>
#include <mutex>
#include <span>
#include <stop_token>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace {

// Stock MiuiBleOobHelperService fe11 command prefixes (decimal in source):
//   vibration  {94,2}  -> 5e 02 type amp
//   writing    {89,2}  -> 59 02 pen_type level
//   pinch fb   {90,1}  -> 5a 01 level
//   double-tap {95,1}  -> 5f 01 on/off
//   bees       {96,1}  -> 60 01 on/off
constexpr std::string_view kCommandUuid =
    "0000fe11-aa6c-462a-964a-7f2ed5b3e512";
constexpr auto kRetryDelay = std::chrono::milliseconds(100);
constexpr std::size_t kMaximumQueued = 48;

struct HapticRequest {
    std::vector<std::uint8_t> payload;
    std::string label;
    std::chrono::steady_clock::time_point due{};
    std::string address;
    int attempts = 0;
};

class BluezGattWriter {
public:
    ~BluezGattWriter() {
        resetBus();
    }

    void setDeviceAddress(std::string_view address) {
        if (address.empty()) {
            device_address_.clear();
            command_path_.clear();
            return;
        }
        std::string normalized;
        normalized.reserve(address.size());
        for (const char character : address) {
            normalized.push_back(static_cast<char>(
                std::toupper(static_cast<unsigned char>(character))));
        }
        if (normalized == device_address_)
            return;
        device_address_ = std::move(normalized);
        command_path_.clear();
    }

    bool trigger(std::span<const std::uint8_t> command) {
        if (writeCommand(command))
            return true;
        command_path_.clear();
        return findCommandPath() && writeCommand(command);
    }

private:
    sd_bus *bus_ = nullptr;
    std::string device_address_;
    std::string command_path_;

    bool findCommandPath() {
        if (device_address_.empty())
            return false;
        if (!bus_ && sd_bus_open_system(&bus_) < 0) {
            bus_ = nullptr;
            return false;
        }
        sd_bus_error error = SD_BUS_ERROR_NULL;
        sd_bus_message *reply = nullptr;
        const int result = sd_bus_call_method(
            bus_, "org.bluez", "/", "org.freedesktop.DBus.ObjectManager",
            "GetManagedObjects", &error, &reply, "");
        if (result < 0) {
            sd_bus_error_free(&error);
            sd_bus_message_unref(reply);
            resetBus();
            return false;
        }
        sd_bus_message_enter_container(reply, 'a', "{oa{sa{sv}}}");
        while (sd_bus_message_enter_container(
                   reply, 'e', "oa{sa{sv}}") > 0) {
            const char *path = nullptr;
            sd_bus_message_read_basic(reply, 'o', &path);
            bool uuid_match = false;
            sd_bus_message_enter_container(reply, 'a', "{sa{sv}}");
            while (sd_bus_message_enter_container(
                       reply, 'e', "sa{sv}") > 0) {
                const char *interface = nullptr;
                sd_bus_message_read_basic(reply, 's', &interface);
                const bool characteristic = interface &&
                    std::string_view(interface) ==
                        "org.bluez.GattCharacteristic1";
                sd_bus_message_enter_container(reply, 'a', "{sv}");
                while (sd_bus_message_enter_container(
                           reply, 'e', "sv") > 0) {
                    const char *property = nullptr;
                    sd_bus_message_read_basic(reply, 's', &property);
                    if (characteristic && property &&
                        std::string_view(property) == "UUID") {
                        const char *uuid = nullptr;
                        sd_bus_message_enter_container(reply, 'v', "s");
                        sd_bus_message_read_basic(reply, 's', &uuid);
                        sd_bus_message_exit_container(reply);
                        uuid_match = uuid &&
                            std::string_view(uuid) == kCommandUuid;
                    } else {
                        sd_bus_message_skip(reply, "v");
                    }
                    sd_bus_message_exit_container(reply);
                }
                sd_bus_message_exit_container(reply);
                sd_bus_message_exit_container(reply);
            }
            sd_bus_message_exit_container(reply);
            sd_bus_message_exit_container(reply);
            if (uuid_match && path && belongsToDevice(path)) {
                command_path_ = path;
                break;
            }
        }
        sd_bus_message_exit_container(reply);
        sd_bus_message_unref(reply);
        sd_bus_error_free(&error);
        return !command_path_.empty();
    }

    bool belongsToDevice(std::string_view characteristic_path) {
        const std::size_t service = characteristic_path.find("/service");
        if (service == std::string_view::npos)
            return false;
        const std::string device_path(characteristic_path.substr(0, service));
        sd_bus_error error = SD_BUS_ERROR_NULL;
        char *address = nullptr;
        const int result = sd_bus_get_property_string(
            bus_, "org.bluez", device_path.c_str(), "org.bluez.Device1",
            "Address", &error, &address);
        bool matches = false;
        if (result >= 0 && address) {
            std::string normalized(address);
            std::transform(
                normalized.begin(), normalized.end(), normalized.begin(),
                [](unsigned char character) {
                    return static_cast<char>(std::toupper(character));
                });
            matches = normalized == device_address_;
        }
        std::free(address);
        sd_bus_error_free(&error);
        return matches;
    }

    bool writeCommand(std::span<const std::uint8_t> command) {
        if (command_path_.empty() && !findCommandPath())
            return false;
        sd_bus_error error = SD_BUS_ERROR_NULL;
        sd_bus_message *message = nullptr;
        sd_bus_message *reply = nullptr;
        int result = sd_bus_message_new_method_call(
            bus_, &message, "org.bluez", command_path_.c_str(),
            "org.bluez.GattCharacteristic1", "WriteValue");
        if (result >= 0)
            result = sd_bus_message_append_array(
                message, 'y', command.data(), command.size());
        if (result >= 0)
            result = sd_bus_message_open_container(message, 'a', "{sv}");
        if (result >= 0)
            result = sd_bus_message_close_container(message);
        if (result >= 0)
            result = sd_bus_call(bus_, message, 500000, &error, &reply);
        sd_bus_message_unref(reply);
        sd_bus_message_unref(message);
        sd_bus_error_free(&error);
        if (result >= 0)
            return true;
        resetBus();
        return false;
    }

    void resetBus() {
        bus_ = sd_bus_unref(bus_);
    }
};

class BluezPenHaptics final : public FocusPenHaptics {
public:
    BluezPenHaptics()
        : worker_([this](std::stop_token stop) { run(stop); }) {}

    ~BluezPenHaptics() override {
        worker_.request_stop();
        condition_.notify_all();
    }

    void setDeviceAddress(std::string_view address) noexcept override {
        try {
            std::lock_guard lock(mutex_);
            if (device_address_ == address)
                return;
            device_address_ = address;
            requests_.clear();
            writing_level_ = 0xff;
            condition_.notify_all();
        } catch (const std::exception &error) {
            std::cerr << "Focus Pen Pro haptic address update failed: "
                      << error.what() << '\n';
        }
    }

    void sendRaw(std::vector<std::uint8_t> payload) noexcept override {
        enqueue(std::move(payload), "raw", std::chrono::milliseconds(0));
    }

    void vibrate(std::uint8_t type, std::uint8_t amplitude) noexcept override {
        enqueue({0x5e, 0x02, type, amplitude}, "vibrate",
                std::chrono::milliseconds(0));
    }

    void scheduleDoublePress() noexcept override {
        vibrate(kVibrateTypeDoublePress, kDefaultAmplitude);
    }

    void triggerSlide() noexcept override {
        vibrate(kVibrateTypeSlide, kDefaultAmplitude);
    }

    void triggerPinch(bool pressed) noexcept override {
        if (pressed)
            vibrate(kVibrateTypePinchDown, kDefaultAmplitude);
        else
            stopVibrate();
    }

    void stopVibrate() noexcept override {
        // Stock stop: {94,2,0,0}
        vibrate(kVibrateTypeStop, 0);
    }

    void setBees(bool enabled) noexcept override {
        // Stock: {96,1,0}/{96,1,1}
        enqueue({0x60, 0x01, static_cast<std::uint8_t>(enabled ? 1 : 0)},
                enabled ? "bees-on" : "bees-off",
                std::chrono::milliseconds(0));
    }

    void setDoubleTapEnabled(bool enabled) noexcept override {
        // Stock: {95,1,0}/{95,1,1}
        enqueue({0x5f, 0x01, static_cast<std::uint8_t>(enabled ? 1 : 0)},
                enabled ? "double-tap-on" : "double-tap-off",
                std::chrono::milliseconds(0));
    }

    void setWritingFeedback(std::uint8_t pen_type,
                            std::uint8_t level) noexcept override {
        // Stock: {89,2, pen_type, level}
        pen_type = std::min<std::uint8_t>(pen_type, 4);
        level = std::min<std::uint8_t>(level, 5);
        try {
            std::lock_guard lock(mutex_);
            if (writing_level_ == level && writing_pen_type_ == pen_type)
                return;
            writing_level_ = level;
            writing_pen_type_ = pen_type;
        } catch (...) {
        }
        enqueue({0x59, 0x02, pen_type, level}, "writing-feedback",
                std::chrono::milliseconds(0));
    }

    void setPinchMotorLevel(std::uint8_t level) noexcept override {
        // Stock: {90,1, level}
        level = std::min<std::uint8_t>(level, 5);
        enqueue({0x5a, 0x01, level}, "pinch-motor-level",
                std::chrono::milliseconds(0));
    }

    void reset() noexcept override {
        try {
            std::lock_guard lock(mutex_);
            requests_.clear();
            writing_level_ = 0xff;
            if (!device_address_.empty()) {
                requests_.push_back(HapticRequest{
                    {0x5e, 0x02, 0x00, 0x00}, "vibrate-stop",
                    std::chrono::steady_clock::now(), device_address_, 0});
            }
            condition_.notify_all();
        } catch (const std::exception &error) {
            std::cerr << "Focus Pen Pro haptic reset failed: "
                      << error.what() << '\n';
        }
    }

private:
    std::mutex mutex_;
    std::condition_variable condition_;
    std::deque<HapticRequest> requests_;
    std::string device_address_;
    std::uint8_t writing_level_ = 0xff;
    std::uint8_t writing_pen_type_ = 0xff;
    BluezGattWriter writer_;
    std::jthread worker_;

    void enqueue(std::vector<std::uint8_t> payload, const char *label,
                 std::chrono::milliseconds delay) noexcept {
        try {
            std::lock_guard lock(mutex_);
            if (requests_.size() >= kMaximumQueued)
                requests_.pop_front();
            requests_.push_back(HapticRequest{
                std::move(payload), label,
                std::chrono::steady_clock::now() + delay, device_address_,
                0});
            condition_.notify_all();
        } catch (const std::exception &error) {
            std::cerr << "Focus Pen Pro haptic request dropped: "
                      << error.what() << '\n';
        }
    }

    void run(std::stop_token stop) {
        std::stop_callback wake_on_stop(
            stop, [this]() { condition_.notify_all(); });
        std::unique_lock lock(mutex_);
        while (!stop.stop_requested()) {
            if (requests_.empty()) {
                condition_.wait(lock);
                continue;
            }
            const auto next = std::min_element(
                requests_.begin(), requests_.end(),
                [](const HapticRequest &left, const HapticRequest &right) {
                    return left.due < right.due;
                });
            const auto now = std::chrono::steady_clock::now();
            if (next->due > now) {
                condition_.wait_until(lock, next->due);
                continue;
            }
            HapticRequest request = std::move(*next);
            requests_.erase(next);
            lock.unlock();
            bool ok = false;
            try {
                ok = send(request);
            } catch (const std::exception &error) {
                std::cerr << "Focus Pen Pro haptic worker failed: "
                          << error.what() << '\n';
            }
            lock.lock();
            if (!ok && request.attempts == 0 && !request.address.empty()) {
                request.attempts = 1;
                request.due = std::chrono::steady_clock::now() + kRetryDelay;
                if (requests_.size() >= kMaximumQueued)
                    requests_.pop_front();
                requests_.push_back(std::move(request));
            }
        }
    }

    bool send(const HapticRequest &request) {
        if (request.address.empty()) {
            std::cerr << "Focus Pen Pro " << request.label
                      << " skipped: device address unavailable\n";
            return false;
        }
        if (request.payload.empty())
            return false;
        writer_.setDeviceAddress(request.address);
        const bool ok = writer_.trigger(request.payload);
        std::cerr << "Focus Pen Pro " << request.label << " "
                  << (ok ? "sent" : "failed") << " [";
        for (std::size_t i = 0; i < request.payload.size(); ++i) {
            if (i)
                std::cerr << ' ';
            const unsigned value = request.payload[i];
            if (value < 0x10)
                std::cerr << '0';
            std::cerr << std::hex << value << std::dec;
        }
        std::cerr << ']' << (request.attempts ? " retry" : "") << '\n';
        return ok;
    }
};

}  // namespace

std::unique_ptr<FocusPenHaptics> makeFocusPenHaptics() {
    try {
        return std::make_unique<BluezPenHaptics>();
    } catch (const std::exception &error) {
        std::cerr << "Focus Pen Pro haptics disabled: " << error.what()
                  << '\n';
        return nullptr;
    }
}
