#pragma once

#include "cam86/camera/CameraTypes.hpp"

#include <cstdint>
#include <span>
#include <vector>

namespace cam86::protocol {

[[nodiscard]] std::vector<std::uint8_t> makeSpiWaveform(std::uint8_t command,
                                                        std::uint16_t parameter);
[[nodiscard]] std::uint16_t decodeSpiResponse(std::span<const std::uint8_t> bytes);
[[nodiscard]] std::vector<std::uint8_t> makeAd9822Waveform(std::uint8_t address,
                                                           std::uint16_t value);

// Decode the D2XX-compatible payload returned on FT2232 channel A.
void decodeFramePayload(std::span<const std::uint8_t> payload, bool bin2x2,
                        int firstSensorRow, int sensorRowCount,
                        std::vector<std::uint16_t>& destination);

} // namespace cam86::protocol

