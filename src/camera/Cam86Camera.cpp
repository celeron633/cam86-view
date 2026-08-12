#include "cam86/camera/ICamera.hpp"

#include "cam86/camera/Cam86Protocol.hpp"
#include "cam86/usb/IUsbTransport.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <future>
#include <mutex>
#include <stdexcept>
#include <thread>

namespace cam86::protocol {
namespace {

constexpr std::uint8_t kPortIdle = 0x11;

[[nodiscard]] std::uint16_t readBigEndianWord(const std::span<const std::uint8_t> bytes,
                                               const std::size_t wordIndex) {
    const auto byteIndex = wordIndex * 2U;
    if (byteIndex + 1U >= bytes.size()) {
        throw std::runtime_error("CAM86 frame payload ended before the expected image data");
    }
    return static_cast<std::uint16_t>((static_cast<std::uint16_t>(bytes[byteIndex]) << 8U) |
                                      bytes[byteIndex + 1U]);
}

} // namespace

std::vector<std::uint8_t> makeSpiWaveform(const std::uint8_t command,
                                          const std::uint16_t parameter) {
    std::vector<std::uint8_t> output(100, kPortIdle);
    const std::array<std::uint8_t, 3> input{
        command, static_cast<std::uint8_t>(parameter >> 8U), static_cast<std::uint8_t>(parameter)};
    for (std::size_t byte = 0; byte < input.size(); ++byte) {
        auto value = input[byte];
        for (std::size_t bit = 0; bit < 8; ++bit) {
            const auto base = 2U * bit + 16U * byte;
            output[base + 1U] = static_cast<std::uint8_t>(output[base + 1U] + 0x20U);
            if ((value & 0x80U) != 0) {
                output[base] = static_cast<std::uint8_t>(output[base] + 0x80U);
                output[base + 1U] = static_cast<std::uint8_t>(output[base + 1U] + 0x80U);
            }
            value = static_cast<std::uint8_t>(value << 1U);
        }
    }
    return output;
}

std::uint16_t decodeSpiResponse(const std::span<const std::uint8_t> bytes) {
    // The original Delphi buffer was declared as Word[], although FT_Read wrote bytes.
    // Its FT_In_Buffer[i+9] therefore samples byte 18 + i*2.
    if (bytes.size() < 49) {
        throw std::runtime_error("CAM86 SPI response is too short");
    }
    std::uint16_t result = 0;
    for (std::size_t bit = 0; bit < 16; ++bit) {
        result = static_cast<std::uint16_t>(result << 1U);
        if ((bytes[18U + bit * 2U] & 0x40U) != 0) {
            result = static_cast<std::uint16_t>(result | 1U);
        }
    }
    return result;
}

std::vector<std::uint8_t> makeAd9822Waveform(const std::uint8_t address,
                                              const std::uint16_t value) {
    std::vector<std::uint8_t> output(64, kPortIdle);
    for (std::size_t i = 1; i <= 32; ++i) {
        output[i] = static_cast<std::uint8_t>(output[i] & 0xFEU);
    }
    for (std::size_t i = 0; i < 16; ++i) {
        output[2U * i + 2U] = static_cast<std::uint8_t>(output[2U * i + 2U] + 2U);
    }
    const auto setPair = [&output](const std::size_t first) {
        output[first] = static_cast<std::uint8_t>(output[first] + 4U);
        output[first + 1U] = static_cast<std::uint8_t>(output[first + 1U] + 4U);
    };
    if ((address & 4U) != 0) setPair(3);
    if ((address & 2U) != 0) setPair(5);
    if ((address & 1U) != 0) setPair(7);
    for (int bit = 8; bit >= 0; --bit) {
        if ((value & (1U << static_cast<unsigned>(bit))) != 0) {
            const auto first = static_cast<std::size_t>(15 + 2 * (8 - bit));
            setPair(first);
        }
    }
    return output;
}

void decodeFramePayload(const std::span<const std::uint8_t> payload, const bool bin2x2,
                        const int firstSensorRow, const int sensorRowCount,
                        std::vector<std::uint16_t>& destination) {
    if (destination.size() != static_cast<std::size_t>(kSensorWidth * kSensorHeight)) {
        destination.assign(static_cast<std::size_t>(kSensorWidth * kSensorHeight), 0);
    }
    if (firstSensorRow < 0 || sensorRowCount < 0 || firstSensorRow + sensorRowCount > kSensorHeight ||
        (firstSensorRow % 2) != 0 || (sensorRowCount % 2) != 0) {
        throw std::invalid_argument("CAM86 frame row range is invalid");
    }

    const int rowPairs = sensorRowCount / 2;
    if (!bin2x2) {
        constexpr std::size_t wordsPerPair = 6004;
        const auto expected = static_cast<std::size_t>(rowPairs) * wordsPerPair * 2U;
        if (payload.size() < expected) {
            throw std::runtime_error("Unbinned CAM86 payload is shorter than expected");
        }
        for (int pair = 0; pair < rowPairs; ++pair) {
            const auto sourceBase = static_cast<std::size_t>(pair) * wordsPerPair + 4U;
            const auto y = firstSensorRow + pair * 2;
            for (int x = 0; x < kSensorWidth / 2; ++x) {
                const auto source = sourceBase + static_cast<std::size_t>(x) * 4U;
                const auto x0 = x * 2;
                destination[static_cast<std::size_t>(y) * kSensorWidth + x0] =
                    readBigEndianWord(payload, source);
                destination[static_cast<std::size_t>(y + 1) * kSensorWidth + x0] =
                    readBigEndianWord(payload, source + 1U);
                destination[static_cast<std::size_t>(y + 1) * kSensorWidth + x0 + 1] =
                    readBigEndianWord(payload, source + 2U);
                destination[static_cast<std::size_t>(y) * kSensorWidth + x0 + 1] =
                    readBigEndianWord(payload, source + 3U);
            }
        }
        return;
    }

    constexpr std::size_t wordsPerPair = 1504;
    const auto expected = static_cast<std::size_t>(rowPairs) * wordsPerPair * 2U;
    if (payload.size() < expected) {
        throw std::runtime_error("Binned CAM86 payload is shorter than expected");
    }
    for (int pair = 0; pair < rowPairs; ++pair) {
        // The old source used +7 against a 1504-word row, which crosses each row boundary.
        // The wire size proves this is a four-word header followed by 1500 pixels.
        const auto sourceBase = static_cast<std::size_t>(pair) * wordsPerPair + 4U;
        const auto y = firstSensorRow + pair * 2;
        for (int x = 0; x < kSensorWidth / 2; ++x) {
            const auto sample = readBigEndianWord(payload, sourceBase + static_cast<std::size_t>(x));
            const auto x0 = x * 2;
            destination[static_cast<std::size_t>(y) * kSensorWidth + x0] = sample;
            destination[static_cast<std::size_t>(y) * kSensorWidth + x0 + 1] = sample;
            destination[static_cast<std::size_t>(y + 1) * kSensorWidth + x0] = sample;
            destination[static_cast<std::size_t>(y + 1) * kSensorWidth + x0 + 1] = sample;
        }
    }
}

} // namespace cam86::protocol

namespace cam86 {
namespace {

using namespace std::chrono_literals;

class Cam86Camera final : public ICamera {
public:
    Cam86Camera() = default;
    ~Cam86Camera() override { disconnect(); }

    [[nodiscard]] CameraInfo info() const override {
        return {"CAM86", "libusb / FT2232H", false};
    }

    void connect() override {
        std::scoped_lock lock(ioMutex_);
        if (connected_) return;
        transport_ = makeLibusbFtdiTransport();
        transport_->open(0x0403, 0x6010, "CAM86");
        try {
            transport_->setBitMode(FtdiChannel::B, 0xBF, 0x04);
            transport_->setBaudRate(FtdiChannel::B, 20'000);
            transport_->setLatency(FtdiChannel::A, 1);
            transport_->setLatency(FtdiChannel::B, 1);
            transport_->purge(FtdiChannel::A, true, true);
            transport_->purge(FtdiChannel::B, true, true);
            writeAd9822(0, 0xD8);
            setGainLocked(0);
            setOffsetLocked(-6);
            std::this_thread::sleep_for(100ms);
            spiCommand(0xDB, 0);
            std::this_thread::sleep_for(100ms);
            transport_->purge(FtdiChannel::A, true, false);
            connected_ = true;
        } catch (...) {
            transport_->close();
            transport_.reset();
            throw;
        }
    }

    void disconnect() noexcept override {
        std::scoped_lock lock(ioMutex_);
        connected_ = false;
        if (transport_) transport_->close();
        transport_.reset();
    }

    [[nodiscard]] bool isConnected() const noexcept override { return connected_; }

    void setGain(const int value) override {
        std::scoped_lock lock(ioMutex_);
        requireConnected();
        setGainLocked(value);
    }

    void setOffset(const int value) override {
        std::scoped_lock lock(ioMutex_);
        requireConnected();
        setOffsetLocked(value);
    }

    void setTargetTemperature(const double celsius) override {
        std::scoped_lock lock(ioMutex_);
        requireConnected();
        const auto encoded = static_cast<std::uint16_t>(1280 + std::lround(celsius * 10.0));
        spiCommand(0xAB, encoded);
    }

    void setCooling(const bool enabled) override {
        std::scoped_lock lock(ioMutex_);
        requireConnected();
        spiCommand(0x9B, enabled ? 1 : 0);
    }

    double readTemperature() override {
        std::scoped_lock lock(ioMutex_);
        requireConnected();
        const auto raw = spiCommand(0xBF, 0);
        const auto temperature = (static_cast<double>(raw) - 1280.0) / 10.0;
        if (temperature >= -120.0 && temperature <= 120.0) {
            temperatureCache_ = temperature;
        }
        return temperatureCache_;
    }

    Frame capture(const ExposureRequest& request, const std::atomic_bool& cancel,
                  ProgressCallback progress) override {
        std::scoped_lock lock(ioMutex_);
        requireConnected();

        const int sensorRows = request.roi ? 100 : kSensorHeight;
        const int firstRow = request.roi
            ? std::clamp(request.roiCenterY - sensorRows / 2, 0, kSensorHeight - sensorRows)
            : 0;
        const int rowPairs = sensorRows / 2;
        spiCommand(0x4B, static_cast<std::uint16_t>(firstRow / 2));
        spiCommand(0x5B, static_cast<std::uint16_t>(rowPairs));
        spiCommand(0x8B, request.bin2x2 ? 1 : 0);
        spiCommand(0x6B, static_cast<std::uint16_t>(
            std::min(1001L, std::lround(request.seconds * 1000.0))));

        progress(0.0F, CameraState::Exposing);
        if (request.seconds > 1.0) {
            spiCommand(0x2B, 0);
            std::this_thread::sleep_for(40ms);
            spiCommand(0xCB, 0);
            std::this_thread::sleep_for(180ms);
            spiCommand(0x3B, 0);
            const auto remaining = std::chrono::duration<double>(std::max(0.0, request.seconds - 1.2));
            const auto start = std::chrono::steady_clock::now();
            while (!cancel.load() && std::chrono::steady_clock::now() - start < remaining) {
                const auto elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - start);
                const auto fraction = request.seconds <= 0.0 ? 1.0 :
                    std::clamp((elapsed.count() + 0.2) / request.seconds, 0.0, 0.95);
                progress(static_cast<float>(fraction), CameraState::Exposing);
                std::this_thread::sleep_for(20ms);
            }
        }

        progress(0.96F, CameraState::Reading);
        transport_->purge(FtdiChannel::A, true, false);
        const auto expectedBytes = static_cast<std::size_t>(rowPairs) *
            (request.bin2x2 ? 3008U : 12008U);
        // Match the Delphi implementation: arm the large channel-A read first, then tell the
        // controller to send the frame on channel B. This prevents the small FTDI FIFO filling
        // before the host starts draining image data.
        auto readFuture = std::async(std::launch::async, [this, expectedBytes] {
            return transport_->read(FtdiChannel::A, expectedBytes, 3000ms);
        });
        spiCommand(0x1B, 0);
        const auto payload = readFuture.get();

        Frame frame;
        frame.pixels.assign(static_cast<std::size_t>(kSensorWidth * kSensorHeight), 0);
        protocol::decodeFramePayload(payload, request.bin2x2, firstRow, sensorRows, frame.pixels);
        frame.exposureSeconds = request.seconds;
        frame.sensorTemperature = temperatureCache_;
        progress(1.0F, CameraState::Ready);
        return frame;
    }

    void stopExposure() noexcept override {
        // capture() observes CameraController's cancellation flag. Avoid issuing USB commands
        // concurrently with an active FT2232 transfer.
    }

private:
    void requireConnected() const {
        if (!connected_ || !transport_) {
            throw std::runtime_error("CAM86 camera is not connected");
        }
    }

    std::uint16_t spiCommand(const std::uint8_t command, const std::uint16_t parameter) {
        transport_->purge(FtdiChannel::B, true, true);
        const auto waveform = protocol::makeSpiWaveform(command, parameter);
        transport_->write(FtdiChannel::B, waveform, 100ms);
        const auto response = transport_->read(FtdiChannel::B, 100, 100ms);
        const auto decoded = protocol::decodeSpiResponse(response);
        std::this_thread::sleep_for(20ms);
        return decoded;
    }

    void writeAd9822(const std::uint8_t address, const std::uint16_t value) {
        const auto waveform = protocol::makeAd9822Waveform(address, value);
        transport_->write(FtdiChannel::B, waveform, 100ms);
    }

    void setGainLocked(const int value) {
        writeAd9822(3, static_cast<std::uint16_t>(std::clamp(value, 0, 63)));
    }

    void setOffsetLocked(const int value) {
        auto encoded = std::abs(2 * std::clamp(value, -127, 127));
        if (value < 0) encoded += 256;
        writeAd9822(6, static_cast<std::uint16_t>(encoded));
    }

    mutable std::mutex ioMutex_;
    std::unique_ptr<IUsbTransport> transport_;
    std::atomic_bool connected_{false};
    double temperatureCache_ = 0.0;
};

} // namespace

std::unique_ptr<ICamera> makeCam86Camera() { return std::make_unique<Cam86Camera>(); }

bool libusbBackendAvailable() noexcept {
#if CAM86_HAS_LIBUSB
    return true;
#else
    return false;
#endif
}

} // namespace cam86
