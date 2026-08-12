#pragma once

#include "cam86/camera/CameraTypes.hpp"

#include <filesystem>

namespace cam86 {

void writeFits(const std::filesystem::path& path, const Frame& frame,
               std::uint16_t displayBlack, std::uint16_t displayWhite);

} // namespace cam86

