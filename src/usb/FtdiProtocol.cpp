#include "cam86/usb/FtdiProtocol.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>

namespace cam86::ftdi {
namespace {

constexpr std::array<std::uint8_t, 8> kFractionCode{0, 3, 2, 4, 1, 5, 6, 7};

} // namespace

BaudRateValue encodeBaudRate(const std::uint32_t baudRate, const std::uint8_t interfaceIndex) {
    if (baudRate == 0) {
        throw std::invalid_argument("FTDI baud rate cannot be zero");
    }

    // FT2232H uses the 120 MHz clock divided by 10 when the high-speed flag is set.
    constexpr double baseClock = 12'000'000.0;
    auto divisor8 = static_cast<std::uint32_t>(std::llround((baseClock * 8.0) / baudRate));
    divisor8 = std::clamp(divisor8, 8U, 0x1FFFFU);

    const auto integerDivisor = divisor8 / 8U;
    const auto fraction = divisor8 % 8U;
    std::uint32_t encoded = integerDivisor | (static_cast<std::uint32_t>(kFractionCode[fraction]) << 14U);

    // Special encodings documented by FTDI and used by libftdi.
    if (encoded == 1U) {
        encoded = 0U;
    } else if (encoded == 0x4001U) {
        encoded = 1U;
    }

    BaudRateValue result;
    result.value = static_cast<std::uint16_t>(encoded & 0xFFFFU);
    result.index = static_cast<std::uint16_t>((encoded >> 16U) & 0xFFFFU);
    result.index = static_cast<std::uint16_t>(result.index | 0x0200U | (interfaceIndex + 1U));
    return result;
}

std::vector<std::uint8_t> stripModemStatusBytes(const std::span<const std::uint8_t> transfer,
                                                const std::size_t maxPacketSize) {
    if (maxPacketSize < 3) {
        throw std::invalid_argument("FTDI maximum packet size is invalid");
    }

    std::vector<std::uint8_t> payload;
    payload.reserve(transfer.size());
    for (std::size_t offset = 0; offset < transfer.size(); offset += maxPacketSize) {
        const auto packetSize = std::min(maxPacketSize, transfer.size() - offset);
        if (packetSize <= 2) {
            continue;
        }
        payload.insert(payload.end(), transfer.begin() + static_cast<std::ptrdiff_t>(offset + 2),
                       transfer.begin() + static_cast<std::ptrdiff_t>(offset + packetSize));
    }
    return payload;
}

} // namespace cam86::ftdi

