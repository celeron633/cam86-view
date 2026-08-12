#pragma once

#include "cam86/camera/ICamera.hpp"

#include <atomic>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

namespace cam86 {

class CameraController {
public:
    CameraController();
    ~CameraController();

    CameraController(const CameraController&) = delete;
    CameraController& operator=(const CameraController&) = delete;

    void useSimulation(bool enabled);
    [[nodiscard]] bool usingSimulation() const noexcept { return simulation_; }
    [[nodiscard]] bool hardwareAvailable() const noexcept;

    bool connect();
    void disconnect();
    void startCapture(ExposureRequest request);
    void stopCapture();

    void setGain(int value);
    void setOffset(int value);
    void setTargetTemperature(double value);
    void setCooling(bool enabled);
    void requestTemperature();

    [[nodiscard]] CameraState state() const noexcept { return state_.load(); }
    [[nodiscard]] float progress() const noexcept { return progress_.load(); }
    [[nodiscard]] bool isConnected() const noexcept;
    [[nodiscard]] std::optional<Frame> takeFrame();
    [[nodiscard]] std::optional<double> takeTemperature();
    [[nodiscard]] std::vector<std::string> takeMessages();

private:
    void replaceCamera();
    void joinWorker();
    void pushMessage(std::string message);

    bool simulation_ = true;
    std::unique_ptr<ICamera> camera_;
    std::jthread worker_;
    std::atomic_bool cancel_{false};
    std::atomic<CameraState> state_{CameraState::Disconnected};
    std::atomic<float> progress_{0.0F};
    mutable std::mutex mutex_;
    std::optional<Frame> completedFrame_;
    std::optional<double> temperature_;
    std::deque<std::string> messages_;
};

} // namespace cam86

