#include "cam86/usb/IUsbTransport.hpp"

#include "cam86/usb/FtdiProtocol.hpp"

#include <algorithm>
#include <array>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#if CAM86_HAS_LIBUSB
#include <libusb.h>
#endif

namespace cam86 {

#if CAM86_HAS_LIBUSB
namespace {

struct DeviceListDeleter {
    void operator()(libusb_device** devices) const noexcept {
        if (devices != nullptr) libusb_free_device_list(devices, 1);
    }
};

[[noreturn]] void throwUsb(const std::string& operation, const int result) {
    throw std::runtime_error(operation + ": " + libusb_error_name(result));
}

class LibusbFtdiTransport final : public IUsbTransport {
public:
    LibusbFtdiTransport() {
        const auto result = libusb_init(&context_);
        if (result != LIBUSB_SUCCESS) {
            throwUsb("libusb_init", result);
        }
    }

    ~LibusbFtdiTransport() override {
        close();
        libusb_exit(context_);
    }

    void open(const std::uint16_t vendorId, const std::uint16_t productId,
              const std::string& serialPrefix) override {
        close();
        libusb_device** devices = nullptr;
        const auto count = libusb_get_device_list(context_, &devices);
        if (count < 0) {
            throwUsb("libusb_get_device_list", static_cast<int>(count));
        }
        const std::unique_ptr<libusb_device*, DeviceListDeleter> list(devices);

        for (ssize_t i = 0; i < count && handle_ == nullptr; ++i) {
            libusb_device_descriptor descriptor{};
            if (libusb_get_device_descriptor(devices[i], &descriptor) != LIBUSB_SUCCESS ||
                descriptor.idVendor != vendorId || descriptor.idProduct != productId) {
                continue;
            }

            libusb_device_handle* candidate = nullptr;
            if (libusb_open(devices[i], &candidate) != LIBUSB_SUCCESS) {
                continue;
            }

            std::array<unsigned char, 256> serial{};
            std::string serialText;
            if (descriptor.iSerialNumber != 0) {
                const auto length = libusb_get_string_descriptor_ascii(
                    candidate, descriptor.iSerialNumber, serial.data(), static_cast<int>(serial.size()));
                if (length > 0) {
                    serialText.assign(reinterpret_cast<char*>(serial.data()), static_cast<std::size_t>(length));
                }
            }
            if (!serialPrefix.empty() && !serialText.starts_with(serialPrefix)) {
                libusb_close(candidate);
                continue;
            }
            handle_ = candidate;
            device_ = libusb_get_device(handle_);
            libusb_ref_device(device_);
        }

        if (handle_ == nullptr) {
            throw std::runtime_error("CAM86 FT2232H (VID 0403, PID 6010, serial CAM86*) was not found");
        }

        try {
            for (int interfaceNumber = 0; interfaceNumber < 2; ++interfaceNumber) {
#ifndef _WIN32
                if (libusb_kernel_driver_active(handle_, interfaceNumber) == 1) {
                    const auto detachResult = libusb_detach_kernel_driver(handle_, interfaceNumber);
                    if (detachResult == LIBUSB_SUCCESS) {
                        detached_[static_cast<std::size_t>(interfaceNumber)] = true;
                    }
                }
#endif
                const auto result = libusb_claim_interface(handle_, interfaceNumber);
                if (result != LIBUSB_SUCCESS) {
                    throwUsb("claim FT2232 interface " + std::to_string(interfaceNumber), result);
                }
                claimed_[static_cast<std::size_t>(interfaceNumber)] = true;
                packetSizes_[static_cast<std::size_t>(interfaceNumber)] = packetSize(interfaceNumber);
            }
        } catch (...) {
            close();
            throw;
        }
    }

    void close() noexcept override {
        if (handle_ == nullptr) {
            return;
        }
        for (int interfaceNumber = 1; interfaceNumber >= 0; --interfaceNumber) {
            if (claimed_[static_cast<std::size_t>(interfaceNumber)]) {
                libusb_release_interface(handle_, interfaceNumber);
                claimed_[static_cast<std::size_t>(interfaceNumber)] = false;
            }
#ifndef _WIN32
            if (detached_[static_cast<std::size_t>(interfaceNumber)]) {
                libusb_attach_kernel_driver(handle_, interfaceNumber);
                detached_[static_cast<std::size_t>(interfaceNumber)] = false;
            }
#endif
        }
        libusb_close(handle_);
        handle_ = nullptr;
        if (device_ != nullptr) {
            libusb_unref_device(device_);
            device_ = nullptr;
        }
    }

    [[nodiscard]] bool isOpen() const noexcept override { return handle_ != nullptr; }

    void reset(const FtdiChannel channel) override {
        control(ftdi::kResetRequest, ftdi::kResetDevice, channelIndex(channel));
    }

    void purge(const FtdiChannel channel, const bool rx, const bool tx) override {
        if (rx) {
            control(ftdi::kResetRequest, ftdi::kPurgeRx, channelIndex(channel));
        }
        if (tx) {
            control(ftdi::kResetRequest, ftdi::kPurgeTx, channelIndex(channel));
        }
    }

    void setLatency(const FtdiChannel channel, const std::uint8_t milliseconds) override {
        control(ftdi::kSetLatencyRequest, milliseconds, channelIndex(channel));
    }

    void setBaudRate(const FtdiChannel channel, const std::uint32_t baudRate) override {
        const auto encoded = ftdi::encodeBaudRate(
            baudRate, static_cast<std::uint8_t>(channelNumber(channel)));
        control(ftdi::kSetBaudRateRequest, encoded.value, encoded.index);
    }

    void setBitMode(const FtdiChannel channel, const std::uint8_t mask,
                    const std::uint8_t mode) override {
        const auto value = static_cast<std::uint16_t>(mask | (static_cast<std::uint16_t>(mode) << 8U));
        control(ftdi::kSetBitModeRequest, value, channelIndex(channel));
    }

    void write(const FtdiChannel channel, const std::span<const std::uint8_t> bytes,
               const std::chrono::milliseconds timeout) override {
        ensureOpen();
        std::size_t offset = 0;
        while (offset < bytes.size()) {
            int transferred = 0;
            const auto chunk = static_cast<int>(std::min<std::size_t>(bytes.size() - offset, 1U << 20U));
            const auto result = libusb_bulk_transfer(
                handle_, outEndpoint(channel),
                const_cast<unsigned char*>(bytes.data() + offset), chunk, &transferred,
                static_cast<unsigned int>(timeout.count()));
            if (result != LIBUSB_SUCCESS) {
                throwUsb("FT2232 bulk write", result);
            }
            if (transferred <= 0) {
                throw std::runtime_error("FT2232 bulk write made no progress");
            }
            offset += static_cast<std::size_t>(transferred);
        }
    }

    [[nodiscard]] std::vector<std::uint8_t> read(
        const FtdiChannel channel, const std::size_t payloadBytes,
        const std::chrono::milliseconds timeout) override {
        ensureOpen();
        std::vector<std::uint8_t> result;
        result.reserve(payloadBytes);
        std::vector<std::uint8_t> transfer(64U * 1024U);
        const auto packetSize = packetSizes_[channelNumber(channel)];

        while (result.size() < payloadBytes) {
            int transferred = 0;
            const auto usbResult = libusb_bulk_transfer(
                handle_, inEndpoint(channel), transfer.data(), static_cast<int>(transfer.size()),
                &transferred, static_cast<unsigned int>(timeout.count()));
            if (usbResult != LIBUSB_SUCCESS) {
                throwUsb("FT2232 bulk read", usbResult);
            }
            if (transferred <= 0) {
                throw std::runtime_error("FT2232 bulk read returned no data");
            }
            const auto payload = ftdi::stripModemStatusBytes(
                std::span<const std::uint8_t>(transfer.data(), static_cast<std::size_t>(transferred)),
                packetSize);
            const auto needed = payloadBytes - result.size();
            result.insert(result.end(), payload.begin(),
                          payload.begin() + static_cast<std::ptrdiff_t>((std::min)(needed, payload.size())));
        }
        return result;
    }

private:
    [[nodiscard]] static std::size_t channelNumber(const FtdiChannel channel) {
        return static_cast<std::size_t>(channel);
    }

    [[nodiscard]] static std::uint16_t channelIndex(const FtdiChannel channel) {
        return static_cast<std::uint16_t>(channelNumber(channel) + 1U);
    }

    [[nodiscard]] static unsigned char inEndpoint(const FtdiChannel channel) {
        return channel == FtdiChannel::A ? 0x81 : 0x83;
    }

    [[nodiscard]] static unsigned char outEndpoint(const FtdiChannel channel) {
        return channel == FtdiChannel::A ? 0x02 : 0x04;
    }

    void ensureOpen() const {
        if (handle_ == nullptr) {
            throw std::runtime_error("FT2232 transport is not open");
        }
    }

    void control(const std::uint8_t request, const std::uint16_t value,
                 const std::uint16_t index) {
        ensureOpen();
        const auto requestType = static_cast<std::uint8_t>(
            static_cast<unsigned>(LIBUSB_REQUEST_TYPE_VENDOR) |
            static_cast<unsigned>(LIBUSB_RECIPIENT_DEVICE) |
            static_cast<unsigned>(LIBUSB_ENDPOINT_OUT));
        const auto result = libusb_control_transfer(
            handle_, requestType,
            request, value, index, nullptr, 0, 1000);
        if (result < 0) {
            throwUsb("FT2232 vendor control request", result);
        }
    }

    [[nodiscard]] std::size_t packetSize(const int interfaceNumber) const {
        const auto endpoint = static_cast<unsigned char>(interfaceNumber == 0 ? 0x81 : 0x83);
        const auto size = libusb_get_max_packet_size(device_, endpoint);
        if (size <= 2) {
            throw std::runtime_error("Could not determine FT2232 bulk packet size");
        }
        return static_cast<std::size_t>(size);
    }

    libusb_context* context_ = nullptr;
    libusb_device_handle* handle_ = nullptr;
    libusb_device* device_ = nullptr;
    std::array<bool, 2> claimed_{false, false};
    std::array<bool, 2> detached_{false, false};
    std::array<std::size_t, 2> packetSizes_{512, 512};
};

} // namespace
#endif

std::unique_ptr<IUsbTransport> makeLibusbFtdiTransport() {
#if CAM86_HAS_LIBUSB
    return std::make_unique<LibusbFtdiTransport>();
#else
    throw std::runtime_error(
        "This build has no libusb backend. Install libusb (vcpkg on Windows or libusb-1.0-dev on Linux) and reconfigure CMake.");
#endif
}

} // namespace cam86
