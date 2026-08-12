#pragma once

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

namespace cam86 {

inline constexpr int kSensorWidth = 3000;
inline constexpr int kSensorHeight = 2000;

enum class CameraState { Disconnected, Ready, Exposing, Reading, Error };

struct ExposureRequest {
    bool bin2x2 = false;
    bool roi = false;
    int roiCenterY = kSensorHeight / 2;
    double seconds = 0.0;
};

struct Frame {
    int width = kSensorWidth;
    int height = kSensorHeight;
    std::vector<std::uint16_t> pixels;
    double exposureSeconds = 0.0;
    double sensorTemperature = 0.0;
    std::chrono::system_clock::time_point capturedAt = std::chrono::system_clock::now();
};

struct CameraInfo {
    std::string name;
    std::string transport;
    bool simulated = false;
};

} // namespace cam86

