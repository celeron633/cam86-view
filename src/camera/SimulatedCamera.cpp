#include "cam86/camera/ICamera.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <stdexcept>
#include <thread>

namespace cam86 {
namespace {

using namespace std::chrono_literals;

class SimulatedCamera final : public ICamera {
public:
    [[nodiscard]] CameraInfo info() const override {
        return {"CAM86 demo camera", "Synthetic frame generator", true};
    }

    void connect() override { connected_ = true; }
    void disconnect() noexcept override { connected_ = false; }
    [[nodiscard]] bool isConnected() const noexcept override { return connected_; }
    void setGain(const int value) override { gain_ = value; }
    void setOffset(const int value) override { offset_ = value; }
    void setTargetTemperature(const double celsius) override { targetTemperature_ = celsius; }
    void setCooling(const bool enabled) override { cooling_ = enabled; }

    double readTemperature() override {
        const auto target = cooling_ ? targetTemperature_ : 18.5;
        temperature_ += (target - temperature_) * 0.08;
        return temperature_;
    }

    Frame capture(const ExposureRequest& request, const std::atomic_bool& cancel,
                  ProgressCallback progress) override {
        if (!connected_) throw std::runtime_error("Demo camera is not connected");
        const auto duration = std::chrono::duration<double>(std::max(0.05, request.seconds));
        const auto start = std::chrono::steady_clock::now();
        while (!cancel.load() && std::chrono::steady_clock::now() - start < duration) {
            const auto elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - start);
            progress(static_cast<float>(std::min(0.92, elapsed.count() / duration.count())),
                     CameraState::Exposing);
            std::this_thread::sleep_for(16ms);
        }
        progress(0.95F, CameraState::Reading);

        Frame frame;
        frame.pixels.resize(static_cast<std::size_t>(kSensorWidth * kSensorHeight));
        frame.exposureSeconds = request.seconds;
        frame.sensorTemperature = readTemperature();
        std::uint32_t random = 0xCA8600U + frameNumber_++;
        const double exposureScale = std::clamp(request.seconds + 0.2, 0.15, 4.0);

        for (int y = 0; y < kSensorHeight; ++y) {
            for (int x = 0; x < kSensorWidth; ++x) {
                const auto dx = std::abs(x - 1540);
                const auto dy = std::abs(y - 940);
                const auto nebula = std::max(0, 760 - (dx + dy) / 2);
                const auto vignette = std::max(0.45, 1.0 - (dx * dx + dy * dy) / 13'000'000.0);
                random = random * 1664525U + 1013904223U;
                const auto noise = static_cast<int>((random >> 24U) & 0xFFU) - 128;
                double value = (620.0 + nebula) * vignette * exposureScale;
                value += static_cast<double>(offset_) * 7.0 + static_cast<double>(gain_) * 4.0 + noise * 0.32;
                frame.pixels[static_cast<std::size_t>(y) * kSensorWidth + x] =
                    static_cast<std::uint16_t>(std::clamp(value, 0.0, 65535.0));
            }
        }
        for (const auto& star : stars_) {
            const int centerX = static_cast<int>(star[0]);
            const int centerY = static_cast<int>(star[1]);
            for (int y = std::max(0, centerY - 24); y <= std::min(kSensorHeight - 1, centerY + 24); ++y) {
                for (int x = std::max(0, centerX - 24); x <= std::min(kSensorWidth - 1, centerX + 24); ++x) {
                    const auto sx = static_cast<double>(x - centerX);
                    const auto sy = static_cast<double>(y - centerY);
                    const auto addition = star[2] * exposureScale * std::exp(-(sx * sx + sy * sy) / star[3]);
                    auto& pixel = frame.pixels[static_cast<std::size_t>(y) * kSensorWidth + x];
                    pixel = static_cast<std::uint16_t>(std::clamp(static_cast<double>(pixel) + addition, 0.0, 65535.0));
                }
            }
        }
        if (request.bin2x2) {
            for (int y = 0; y < kSensorHeight; y += 2) {
                for (int x = 0; x < kSensorWidth; x += 2) {
                    const auto index = static_cast<std::size_t>(y) * kSensorWidth + x;
                    const auto average = static_cast<std::uint16_t>((
                        static_cast<std::uint32_t>(frame.pixels[index]) + frame.pixels[index + 1] +
                        frame.pixels[index + kSensorWidth] + frame.pixels[index + kSensorWidth + 1]) / 4U);
                    frame.pixels[index] = frame.pixels[index + 1] = average;
                    frame.pixels[index + kSensorWidth] = frame.pixels[index + kSensorWidth + 1] = average;
                }
            }
        }
        progress(1.0F, CameraState::Ready);
        return frame;
    }

    void stopExposure() noexcept override {}

private:
    bool connected_ = false;
    bool cooling_ = false;
    int gain_ = 0;
    int offset_ = 0;
    double targetTemperature_ = 0.0;
    double temperature_ = 18.5;
    unsigned frameNumber_ = 0;
    static constexpr std::array<std::array<double, 4>, 7> stars_{{
        {420, 310, 18000, 10}, {860, 1320, 9000, 18}, {1420, 730, 26000, 8},
        {1830, 1160, 14000, 12}, {2360, 410, 22000, 7}, {2680, 1590, 16000, 15},
        {1110, 1750, 12000, 11}
    }};
};

} // namespace

std::unique_ptr<ICamera> makeSimulatedCamera() { return std::make_unique<SimulatedCamera>(); }

} // namespace cam86
