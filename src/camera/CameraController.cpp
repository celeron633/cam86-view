#include "cam86/camera/CameraController.hpp"

#include <exception>
#include <utility>

namespace cam86 {

CameraController::CameraController() { replaceCamera(); }

CameraController::~CameraController() { disconnect(); }

void CameraController::useSimulation(const bool enabled) {
    if (simulation_ == enabled) return;
    disconnect();
    simulation_ = enabled;
    replaceCamera();
}

bool CameraController::hardwareAvailable() const noexcept { return libusbBackendAvailable(); }

bool CameraController::connect() {
    joinWorker();
    try {
        camera_->connect();
        state_ = CameraState::Ready;
        pushMessage("Connected: " + camera_->info().name + " (" + camera_->info().transport + ")");
        return true;
    } catch (const std::exception& error) {
        state_ = CameraState::Error;
        pushMessage(std::string("Connect failed: ") + error.what());
        return false;
    }
}

void CameraController::disconnect() {
    stopCapture();
    joinWorker();
    if (camera_ && camera_->isConnected()) {
        camera_->disconnect();
        pushMessage("Disconnected");
    }
    state_ = CameraState::Disconnected;
    progress_ = 0.0F;
}

void CameraController::startCapture(ExposureRequest request) {
    if (!camera_->isConnected() || state_ == CameraState::Exposing || state_ == CameraState::Reading) {
        return;
    }
    joinWorker();
    cancel_ = false;
    progress_ = 0.0F;
    state_ = CameraState::Exposing;
    worker_ = std::jthread([this, request](std::stop_token) {
        try {
            auto frame = camera_->capture(request, cancel_, [this](const float value, const CameraState state) {
                progress_ = value;
                state_ = state;
            });
            {
                std::scoped_lock lock(mutex_);
                completedFrame_ = std::move(frame);
            }
            state_ = CameraState::Ready;
            progress_ = 1.0F;
            pushMessage("Frame received");
        } catch (const std::exception& error) {
            state_ = camera_->isConnected() ? CameraState::Ready : CameraState::Error;
            progress_ = 0.0F;
            pushMessage(std::string("Capture failed: ") + error.what());
        }
    });
}

void CameraController::stopCapture() {
    cancel_ = true;
    if (camera_) camera_->stopExposure();
}

void CameraController::setGain(const int value) {
    try { if (camera_->isConnected()) camera_->setGain(value); }
    catch (const std::exception& error) { pushMessage(std::string("Gain: ") + error.what()); }
}

void CameraController::setOffset(const int value) {
    try { if (camera_->isConnected()) camera_->setOffset(value); }
    catch (const std::exception& error) { pushMessage(std::string("Offset: ") + error.what()); }
}

void CameraController::setTargetTemperature(const double value) {
    try { if (camera_->isConnected()) camera_->setTargetTemperature(value); }
    catch (const std::exception& error) { pushMessage(std::string("Set temperature: ") + error.what()); }
}

void CameraController::setCooling(const bool enabled) {
    try { if (camera_->isConnected()) camera_->setCooling(enabled); }
    catch (const std::exception& error) { pushMessage(std::string("Cooling: ") + error.what()); }
}

void CameraController::requestTemperature() {
    if (!camera_->isConnected() || state_ == CameraState::Exposing || state_ == CameraState::Reading) return;
    try {
        const auto value = camera_->readTemperature();
        std::scoped_lock lock(mutex_);
        temperature_ = value;
    } catch (const std::exception& error) {
        pushMessage(std::string("Read temperature: ") + error.what());
    }
}

bool CameraController::isConnected() const noexcept { return camera_ && camera_->isConnected(); }

std::optional<Frame> CameraController::takeFrame() {
    std::scoped_lock lock(mutex_);
    auto result = std::move(completedFrame_);
    completedFrame_.reset();
    if (result) progress_ = 0.0F;
    return result;
}

std::optional<double> CameraController::takeTemperature() {
    std::scoped_lock lock(mutex_);
    auto result = temperature_;
    temperature_.reset();
    return result;
}

std::vector<std::string> CameraController::takeMessages() {
    std::scoped_lock lock(mutex_);
    std::vector<std::string> result(messages_.begin(), messages_.end());
    messages_.clear();
    return result;
}

void CameraController::replaceCamera() {
    camera_ = simulation_ ? makeSimulatedCamera() : makeCam86Camera();
}

void CameraController::joinWorker() {
    if (worker_.joinable()) worker_.join();
}

void CameraController::pushMessage(std::string message) {
    std::scoped_lock lock(mutex_);
    if (messages_.size() >= 500) messages_.pop_front();
    messages_.push_back(std::move(message));
}

} // namespace cam86
