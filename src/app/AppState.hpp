#pragma once

#include "cam86/camera/CameraController.hpp"
#include "cam86/image/ImageProcessor.hpp"

#include <array>
#include <algorithm>
#include <chrono>
#include <cstring>
#include <string>
#include <vector>

namespace cam86 {

struct ChoiceValue {
    const char* label;
    double value;
};

inline constexpr std::array<ChoiceValue, 39> kExposureChoices{{
    {"0 ms", 0.0}, {"1 ms", .001}, {"2 ms", .002}, {"3 ms", .003}, {"5 ms", .005},
    {"7 ms", .007}, {"10 ms", .010}, {"15 ms", .015}, {"20 ms", .020}, {"30 ms", .030},
    {"50 ms", .050}, {"70 ms", .070}, {"100 ms", .100}, {"150 ms", .150}, {"200 ms", .200},
    {"300 ms", .300}, {"500 ms", .500}, {"700 ms", .700}, {"1000 ms", 1.0}, {"1500 ms", 1.5},
    {"2 sec", 2.0}, {"3 sec", 3.0}, {"5 sec", 5.0}, {"7 sec", 7.0}, {"10 sec", 10.0},
    {"15 sec", 15.0}, {"20 sec", 20.0}, {"30 sec", 30.0}, {"45 sec", 45.0}, {"60 sec", 60.0},
    {"90 sec", 90.0}, {"2 min", 120.0}, {"3 min", 180.0}, {"5 min", 300.0}, {"7 min", 420.0},
    {"10 min", 600.0}, {"15 min", 900.0}, {"20 min", 1200.0}, {"30 min", 1800.0}
}};

inline constexpr std::array<const char*, 10> kIsoChoices{
    "8..15", "7..14", "6..13", "5..12", "4..11", "3..10", "2..9", "1..8", "0..7", "auto"};

struct AppState {
    CameraController camera;
    ImageProcessor image;
    std::vector<std::string> log;

    bool simulation = true;
    bool bin2x2 = false;
    bool roi = false;
    bool information = false;
    bool writeFits = false;
    bool continuous = false;
    bool infiniteFrames = true;
    bool cooling = false;
    int gain = 0;
    int offset = 0;
    int exposureIndex = 0;
    int isoIndex = 9;
    int targetTemperature = 0;
    int continuousFrames = 10;
    int delaySeconds = 5;
    int frameNumber = 0;
    double sensorTemperature = 0.0;
    DarkMode darkMode = DarkMode::Normal;
    std::array<char, 256> fileName{};
    std::chrono::steady_clock::time_point nextCapture = std::chrono::steady_clock::now();

    AppState() { std::strcpy(fileName.data(), "FileName"); }

    [[nodiscard]] double exposureSeconds() const {
        return kExposureChoices[static_cast<std::size_t>(std::clamp(
            exposureIndex, 0, static_cast<int>(kExposureChoices.size()) - 1))].value;
    }
    [[nodiscard]] int isoShift() const { return isoIndex >= 9 ? -1 : 8 - isoIndex; }
    [[nodiscard]] bool busy() const {
        return camera.state() == CameraState::Exposing || camera.state() == CameraState::Reading;
    }
    void addLog(std::string message) {
        if (log.size() >= 1000) log.erase(log.begin(), log.begin() + 100);
        log.push_back(std::move(message));
    }
};

} // namespace cam86
