#pragma once

#include "cam86/camera/CameraTypes.hpp"

#include <atomic>
#include <functional>
#include <memory>
#include <string>

namespace cam86 {

using ProgressCallback = std::function<void(float, CameraState)>;

class ICamera {
public:
    virtual ~ICamera() = default;

    [[nodiscard]] virtual CameraInfo info() const = 0;
    virtual void connect() = 0;
    virtual void disconnect() noexcept = 0;
    [[nodiscard]] virtual bool isConnected() const noexcept = 0;

    virtual void setGain(int value) = 0;
    virtual void setOffset(int value) = 0;
    virtual void setTargetTemperature(double celsius) = 0;
    virtual void setCooling(bool enabled) = 0;
    virtual double readTemperature() = 0;

    virtual Frame capture(const ExposureRequest& request,
                          const std::atomic_bool& cancel,
                          ProgressCallback progress) = 0;
    virtual void stopExposure() noexcept = 0;
};

std::unique_ptr<ICamera> makeSimulatedCamera();
std::unique_ptr<ICamera> makeCam86Camera();
class IUsbTransport;
// Supply a transport for protocol testing without a physical camera.
std::unique_ptr<ICamera> makeCam86Camera(std::unique_ptr<IUsbTransport> transport);
[[nodiscard]] bool libusbBackendAvailable() noexcept;
[[nodiscard]] bool hardwareBackendAvailable() noexcept;
[[nodiscard]] const char* hardwareBackendName() noexcept;

} // namespace cam86
