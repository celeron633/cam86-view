#include "cam86/image/ImageProcessor.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>
#include <numeric>
#include <stdexcept>

namespace cam86 {
namespace {

constexpr int kCropSize = 50;

template <typename T>
T clampSample(const std::vector<T>& pixels, const int x, const int y) {
    const auto cx = std::clamp(x, 0, kSensorWidth - 1);
    const auto cy = std::clamp(y, 0, kSensorHeight - 1);
    return pixels[static_cast<std::size_t>(cy) * kSensorWidth + cx];
}

} // namespace

ImageProcessor::ImageProcessor()
    : previewRgba_(static_cast<std::size_t>(previewWidth_ * previewHeight_ * 4), 0),
      cropRgba_(static_cast<std::size_t>(kCropSize * kCropSize * 4), 0) {}

void ImageProcessor::accept(Frame frame, const DarkMode darkMode) {
    if (frame.pixels.size() != static_cast<std::size_t>(kSensorWidth * kSensorHeight)) {
        throw std::invalid_argument("A CAM86 frame must contain 3000 x 2000 pixels");
    }
    frame_ = std::move(frame);
    if (darkMode == DarkMode::Accumulate) {
        if (darkAccumulator_.size() != frame_.pixels.size()) {
            darkAccumulator_.assign(frame_.pixels.size(), 0.0);
            darkFrameCount_ = 0;
        }
        for (std::size_t i = 0; i < frame_.pixels.size(); ++i) {
            darkAccumulator_[i] += frame_.pixels[i];
        }
        ++darkFrameCount_;
    } else if (darkMode == DarkMode::Subtract) {
        if (darkFrameCount_ <= 0 || darkAccumulator_.size() != frame_.pixels.size()) {
            throw std::runtime_error("No dark frame is loaded or accumulated");
        }
        constexpr int pedestal = 500;
        for (std::size_t i = 0; i < frame_.pixels.size(); ++i) {
            const auto dark = static_cast<int>(std::lround(darkAccumulator_[i] / darkFrameCount_));
            const auto corrected = static_cast<int>(frame_.pixels[i]) - dark + pedestal;
            frame_.pixels[i] = static_cast<std::uint16_t>(std::clamp(corrected, 0, 65535));
        }
    }
}

void ImageProcessor::rebuild(const bool monochrome, const int isoShift) {
    if (!hasImage()) return;
    monochrome_ = monochrome;
    calculateLevels(isoShift);
    histogramR_.fill(0);
    histogramG_.fill(0);
    histogramB_.fill(0);

    for (int py = 0; py < previewHeight_; ++py) {
        const int y = std::min(kSensorHeight - 1, py * kSensorHeight / previewHeight_);
        for (int px = 0; px < previewWidth_; ++px) {
            const int x = std::min(kSensorWidth - 1, px * kSensorWidth / previewWidth_);
            std::array<std::uint16_t, 3> rgb{};
            if (monochrome_) {
                const auto value = frame_.pixels[static_cast<std::size_t>(y) * kSensorWidth + x];
                rgb = {value, value, value};
            } else {
                rgb = bayerAt(x, y);
            }
            const auto r = mapValue(rgb[0]);
            const auto g = mapValue(rgb[1]);
            const auto b = mapValue(rgb[2]);
            const auto index = static_cast<std::size_t>(py * previewWidth_ + px) * 4U;
            previewRgba_[index] = r;
            previewRgba_[index + 1U] = g;
            previewRgba_[index + 2U] = b;
            previewRgba_[index + 3U] = 255;
            ++histogramR_[r];
            ++histogramG_[g];
            ++histogramB_[b];
        }
    }
    updateCrop();
    ++revision_;
}

ImageStats ImageProcessor::statistics() const {
    ImageStats result;
    if (!hasImage()) return result;

    long double sum = 0.0;
    long double squared = 0.0;
    long double red = 0.0;
    long double green = 0.0;
    long double blue = 0.0;
    std::size_t redCount = 0;
    std::size_t greenCount = 0;
    std::size_t blueCount = 0;
    result.minimum = std::numeric_limits<std::uint16_t>::max();
    result.maximum = 0;
    for (int y = 0; y < kSensorHeight; ++y) {
        for (int x = 0; x < kSensorWidth; ++x) {
            const auto value = frame_.pixels[static_cast<std::size_t>(y) * kSensorWidth + x];
            sum += value;
            squared += static_cast<long double>(value) * value;
            result.minimum = std::min(result.minimum, value);
            result.maximum = std::max(result.maximum, value);
            if ((y & 1) == 0 && (x & 1) == 1) {
                red += value; ++redCount;
            } else if ((y & 1) == 1 && (x & 1) == 0) {
                blue += value; ++blueCount;
            } else {
                green += value; ++greenCount;
            }
        }
    }
    const auto count = static_cast<long double>(frame_.pixels.size());
    result.mean = static_cast<double>(sum / count);
    result.standardDeviation = static_cast<double>(
        std::sqrt(std::max(0.0L, squared / count - (sum / count) * (sum / count))));
    result.redMean = static_cast<double>(red / redCount);
    result.greenMean = static_cast<double>(green / greenCount);
    result.blueMean = static_cast<double>(blue / blueCount);

    const int first = 500 * kSensorWidth;
    const int lineCount = 2 * kSensorWidth;
    long double lineSum = 0.0;
    long double lineSquared = 0.0;
    for (int i = 0; i < lineCount; ++i) {
        const auto value = frame_.pixels[static_cast<std::size_t>(first + i)];
        lineSum += value;
        lineSquared += static_cast<long double>(value) * value;
    }
    const auto lineMean = lineSum / lineCount;
    result.lineStandardDeviation = static_cast<double>(
        std::sqrt(std::max(0.0L, lineSquared / lineCount - lineMean * lineMean)));
    return result;
}

void ImageProcessor::select(const float normalizedX, const float normalizedY) {
    selectionX_ = std::clamp(static_cast<int>(normalizedX * kSensorWidth), 0, kSensorWidth - 1);
    selectionY_ = std::clamp(static_cast<int>(normalizedY * kSensorHeight), 0, kSensorHeight - 1);
    updateCrop();
    ++revision_;
}

void ImageProcessor::clearDark() {
    darkAccumulator_.assign(static_cast<std::size_t>(kSensorWidth * kSensorHeight), 0.0);
    darkFrameCount_ = 0;
}

void ImageProcessor::saveDark(const std::filesystem::path& path) const {
    if (darkFrameCount_ <= 0 || darkAccumulator_.empty()) {
        throw std::runtime_error("There are no accumulated dark frames to save");
    }
    auto outputPath = path;
    if (outputPath.extension() != ".drk") outputPath += ".drk";
    std::ofstream output(outputPath, std::ios::binary);
    if (!output) throw std::runtime_error("Cannot create dark file: " + outputPath.string());
    const std::int32_t count = darkFrameCount_;
    output.write(reinterpret_cast<const char*>(&count), sizeof(count));
    for (const auto value : darkAccumulator_) {
        const auto compatibleValue = static_cast<float>(value);
        output.write(reinterpret_cast<const char*>(&compatibleValue), sizeof(compatibleValue));
    }
    if (!output) throw std::runtime_error("Failed while writing dark file: " + outputPath.string());
}

void ImageProcessor::loadDark(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("Cannot open dark file: " + path.string());
    std::int32_t count = 0;
    input.read(reinterpret_cast<char*>(&count), sizeof(count));
    if (count <= 0) throw std::runtime_error("Dark file has an invalid frame count");
    darkAccumulator_.resize(static_cast<std::size_t>(kSensorWidth * kSensorHeight));
    for (auto& value : darkAccumulator_) {
        float compatibleValue = 0.0F;
        input.read(reinterpret_cast<char*>(&compatibleValue), sizeof(compatibleValue));
        value = compatibleValue;
    }
    if (!input) throw std::runtime_error("Dark file is truncated");
    darkFrameCount_ = count;
}

void ImageProcessor::viewDark() {
    if (darkFrameCount_ <= 0 || darkAccumulator_.empty()) {
        throw std::runtime_error("No dark frame is loaded or accumulated");
    }
    frame_.width = kSensorWidth;
    frame_.height = kSensorHeight;
    frame_.pixels.resize(darkAccumulator_.size());
    for (std::size_t i = 0; i < darkAccumulator_.size(); ++i) {
        frame_.pixels[i] = static_cast<std::uint16_t>(std::clamp(
            std::lround(darkAccumulator_[i] / darkFrameCount_), 0L, 65535L));
    }
}

std::array<std::uint16_t, 3> ImageProcessor::bayerAt(const int x, const int y) const {
    const auto& pixels = frame_.pixels;
    const bool oddX = (x & 1) != 0;
    const bool oddY = (y & 1) != 0;
    const auto center = clampSample(pixels, x, y);
    if (!oddY && oddX) { // R
        const auto g = static_cast<std::uint16_t>((
            static_cast<std::uint32_t>(clampSample(pixels, x - 1, y)) + clampSample(pixels, x + 1, y) +
            clampSample(pixels, x, y - 1) + clampSample(pixels, x, y + 1)) / 4U);
        const auto b = static_cast<std::uint16_t>((
            static_cast<std::uint32_t>(clampSample(pixels, x - 1, y - 1)) + clampSample(pixels, x + 1, y - 1) +
            clampSample(pixels, x - 1, y + 1) + clampSample(pixels, x + 1, y + 1)) / 4U);
        return {center, g, b};
    }
    if (oddY && !oddX) { // B
        const auto g = static_cast<std::uint16_t>((
            static_cast<std::uint32_t>(clampSample(pixels, x - 1, y)) + clampSample(pixels, x + 1, y) +
            clampSample(pixels, x, y - 1) + clampSample(pixels, x, y + 1)) / 4U);
        const auto r = static_cast<std::uint16_t>((
            static_cast<std::uint32_t>(clampSample(pixels, x - 1, y - 1)) + clampSample(pixels, x + 1, y - 1) +
            clampSample(pixels, x - 1, y + 1) + clampSample(pixels, x + 1, y + 1)) / 4U);
        return {r, g, center};
    }
    if (!oddY) { // G on R row
        const auto r = static_cast<std::uint16_t>((
            static_cast<std::uint32_t>(clampSample(pixels, x - 1, y)) + clampSample(pixels, x + 1, y)) / 2U);
        const auto b = static_cast<std::uint16_t>((
            static_cast<std::uint32_t>(clampSample(pixels, x, y - 1)) + clampSample(pixels, x, y + 1)) / 2U);
        return {r, center, b};
    }
    const auto r = static_cast<std::uint16_t>((
        static_cast<std::uint32_t>(clampSample(pixels, x, y - 1)) + clampSample(pixels, x, y + 1)) / 2U);
    const auto b = static_cast<std::uint16_t>((
        static_cast<std::uint32_t>(clampSample(pixels, x - 1, y)) + clampSample(pixels, x + 1, y)) / 2U);
    return {r, center, b};
}

std::uint8_t ImageProcessor::mapValue(const std::uint16_t value) const {
    if (isoShift_ >= 0) {
        return static_cast<std::uint8_t>(std::min(255, static_cast<int>(value) >> isoShift_));
    }
    const auto mapped = (static_cast<int>(value) - blackLevel_) * 255 /
                        std::max(1, whiteLevel_ - blackLevel_);
    return static_cast<std::uint8_t>(std::clamp(mapped, 0, 255));
}

void ImageProcessor::calculateLevels(const int isoShift) {
    isoShift_ = isoShift;
    if (isoShift_ >= 0) return;
    std::array<std::uint32_t, 65536> histogram{};
    for (const auto value : frame_.pixels) ++histogram[value];
    const auto tail = static_cast<std::uint32_t>(kSensorWidth); // Same threshold as old CalkGis.
    std::uint32_t lowCount = 0;
    std::uint32_t highCount = 0;
    blackLevel_ = 0;
    whiteLevel_ = 65535;
    for (int i = 0; i < 65536; ++i) {
        lowCount += histogram[static_cast<std::size_t>(i)];
        if (lowCount < tail) blackLevel_ = i;
        highCount += histogram[static_cast<std::size_t>(65535 - i)];
        if (highCount < tail) whiteLevel_ = 65535 - i;
    }
    if (whiteLevel_ <= blackLevel_) whiteLevel_ = blackLevel_ + 1;
}

void ImageProcessor::updateCrop() {
    if (!hasImage()) return;
    const int firstX = std::clamp(selectionX_ - kCropSize / 2, 0, kSensorWidth - kCropSize);
    const int firstY = std::clamp(selectionY_ - kCropSize / 2, 0, kSensorHeight - kCropSize);
    for (int y = 0; y < kCropSize; ++y) {
        for (int x = 0; x < kCropSize; ++x) {
            std::array<std::uint16_t, 3> rgb{};
            if (monochrome_) {
                const auto value = frame_.pixels[static_cast<std::size_t>(firstY + y) * kSensorWidth + firstX + x];
                rgb = {value, value, value};
            } else {
                rgb = bayerAt(firstX + x, firstY + y);
            }
            const auto index = static_cast<std::size_t>(y * kCropSize + x) * 4U;
            cropRgba_[index] = mapValue(rgb[0]);
            cropRgba_[index + 1U] = mapValue(rgb[1]);
            cropRgba_[index + 2U] = mapValue(rgb[2]);
            cropRgba_[index + 3U] = 255;
        }
    }
}

} // namespace cam86
