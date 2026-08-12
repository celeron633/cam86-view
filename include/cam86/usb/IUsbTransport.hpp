#pragma once

#include <chrono>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace cam86 {

enum class FtdiChannel : std::uint8_t { A = 0, B = 1 };

class IUsbTransport {
public:
    virtual ~IUsbTransport() = default;

    virtual void open(std::uint16_t vendorId, std::uint16_t productId,
                      const std::string& serialPrefix) = 0;
    virtual void close() noexcept = 0;
    [[nodiscard]] virtual bool isOpen() const noexcept = 0;

    virtual void reset(FtdiChannel channel) = 0;
    virtual void purge(FtdiChannel channel, bool rx, bool tx) = 0;
    virtual void setLatency(FtdiChannel channel, std::uint8_t milliseconds) = 0;
    virtual void setBaudRate(FtdiChannel channel, std::uint32_t baudRate) = 0;
    virtual void setBitMode(FtdiChannel channel, std::uint8_t mask, std::uint8_t mode) = 0;

    virtual void write(FtdiChannel channel, std::span<const std::uint8_t> bytes,
                       std::chrono::milliseconds timeout) = 0;
    [[nodiscard]] virtual std::vector<std::uint8_t> read(
        FtdiChannel channel, std::size_t payloadBytes,
        std::chrono::milliseconds timeout) = 0;
};

[[nodiscard]] std::unique_ptr<IUsbTransport> makeLibusbFtdiTransport();

} // namespace cam86
