#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace cam86::ftdi {

inline constexpr std::uint8_t kResetRequest = 0;
inline constexpr std::uint8_t kSetBaudRateRequest = 3;
inline constexpr std::uint8_t kSetLatencyRequest = 9;
inline constexpr std::uint8_t kSetBitModeRequest = 11;
inline constexpr std::uint16_t kResetDevice = 0;
inline constexpr std::uint16_t kPurgeRx = 1;
inline constexpr std::uint16_t kPurgeTx = 2;

struct BaudRateValue {
    std::uint16_t value = 0;
    std::uint16_t index = 0;
};

// Encodes the FTDI divisor used by FT2232H high-speed devices.
[[nodiscard]] BaudRateValue encodeBaudRate(std::uint32_t baudRate, std::uint8_t interfaceIndex);

// D2XX removes the two modem-status bytes at the front of every USB packet.
// libusb exposes them, so the replacement backend must strip them explicitly.
[[nodiscard]] std::vector<std::uint8_t> stripModemStatusBytes(
    std::span<const std::uint8_t> transfer,
    std::size_t maxPacketSize);

} // namespace cam86::ftdi

