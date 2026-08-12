#pragma once

#include "cam86/camera/CameraTypes.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace cam86 {

enum class DarkMode { Normal, Accumulate, Subtract };

struct ImageStats {
    double standardDeviation = 0.0;
    double lineStandardDeviation = 0.0;
    double mean = 0.0;
    double redMean = 0.0;
    double greenMean = 0.0;
    double blueMean = 0.0;
    std::uint16_t minimum = 0;
    std::uint16_t maximum = 0;
};

class ImageProcessor {
public:
    ImageProcessor();

    void accept(Frame frame, DarkMode darkMode);
    void rebuild(bool monochrome, int isoShift);
    [[nodiscard]] bool hasImage() const noexcept { return !frame_.pixels.empty(); }
    [[nodiscard]] const Frame& frame() const noexcept { return frame_; }

    [[nodiscard]] int previewWidth() const noexcept { return previewWidth_; }
    [[nodiscard]] int previewHeight() const noexcept { return previewHeight_; }
    [[nodiscard]] std::span<const std::uint8_t> previewRgba() const noexcept { return previewRgba_; }
    [[nodiscard]] std::span<const std::uint8_t> cropRgba() const noexcept { return cropRgba_; }
    [[nodiscard]] const std::array<std::uint32_t, 256>& histogramR() const noexcept { return histogramR_; }
    [[nodiscard]] const std::array<std::uint32_t, 256>& histogramG() const noexcept { return histogramG_; }
    [[nodiscard]] const std::array<std::uint32_t, 256>& histogramB() const noexcept { return histogramB_; }
    [[nodiscard]] ImageStats statistics() const;

    void select(float normalizedX, float normalizedY);
    [[nodiscard]] int selectionX() const noexcept { return selectionX_; }
    [[nodiscard]] int selectionY() const noexcept { return selectionY_; }
    [[nodiscard]] std::uint16_t blackLevel() const noexcept { return static_cast<std::uint16_t>(blackLevel_); }
    [[nodiscard]] std::uint16_t whiteLevel() const noexcept { return static_cast<std::uint16_t>(whiteLevel_); }

    void clearDark();
    [[nodiscard]] int darkFrameCount() const noexcept { return darkFrameCount_; }
    void saveDark(const std::filesystem::path& path) const;
    void loadDark(const std::filesystem::path& path);
    void viewDark();

    [[nodiscard]] std::uint64_t revision() const noexcept { return revision_; }

private:
    [[nodiscard]] std::array<std::uint16_t, 3> bayerAt(int x, int y) const;
    [[nodiscard]] std::uint8_t mapValue(std::uint16_t value) const;
    void calculateLevels(int isoShift);
    void updateCrop();

    Frame frame_;
    int previewWidth_ = 900;
    int previewHeight_ = 600;
    std::vector<std::uint8_t> previewRgba_;
    std::vector<std::uint8_t> cropRgba_;
    std::array<std::uint32_t, 256> histogramR_{};
    std::array<std::uint32_t, 256> histogramG_{};
    std::array<std::uint32_t, 256> histogramB_{};
    std::vector<double> darkAccumulator_;
    int darkFrameCount_ = 0;
    int selectionX_ = kSensorWidth / 2;
    int selectionY_ = kSensorHeight / 2;
    int blackLevel_ = 0;
    int whiteLevel_ = 65535;
    int isoShift_ = -1;
    bool monochrome_ = false;
    std::uint64_t revision_ = 0;
};

} // namespace cam86
