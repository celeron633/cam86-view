#include "cam86/camera/Cam86Protocol.hpp"
#include "cam86/camera/ICamera.hpp"
#include "cam86/image/ImageProcessor.hpp"
#include "cam86/usb/FtdiProtocol.hpp"

#include <cstdint>
#include <atomic>
#include <exception>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void require(const bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

void testFtdiStatusStripping() {
    const std::vector<std::uint8_t> transfer{
        0x01, 0x60, 10, 11, 12, 13, 14, 15,
        0x01, 0x60, 20, 21, 22
    };
    const auto payload = cam86::ftdi::stripModemStatusBytes(transfer, 8);
    require(payload == std::vector<std::uint8_t>({10, 11, 12, 13, 14, 15, 20, 21, 22}),
            "FTDI modem status bytes were not stripped per USB packet");
}

void testSpiWaveformAndResponse() {
    const auto waveform = cam86::protocol::makeSpiWaveform(0x80, 0);
    require(waveform.size() == 100, "SPI waveform must preserve the old 100-byte transfer");
    require((waveform[0] & 0x80U) != 0 && (waveform[1] & 0xA0U) == 0xA0U,
            "SPI waveform did not encode the command's high bit");

    std::vector<std::uint8_t> response(100, 0);
    constexpr std::uint16_t expected = 0xA55A;
    for (int bit = 0; bit < 16; ++bit) {
        if ((expected & (1U << static_cast<unsigned>(15 - bit))) != 0) {
            response[18U + static_cast<std::size_t>(bit) * 2U] = 0x40;
        }
    }
    require(cam86::protocol::decodeSpiResponse(response) == expected,
            "SPI response sampling differs from the Delphi Word[] buffer behavior");
}

void putWord(std::vector<std::uint8_t>& bytes, const std::size_t wordIndex,
             const std::uint16_t value) {
    bytes[wordIndex * 2U] = static_cast<std::uint8_t>(value >> 8U);
    bytes[wordIndex * 2U + 1U] = static_cast<std::uint8_t>(value);
}

void testUnbinnedFrameDecode() {
    std::vector<std::uint8_t> payload(12008, 0);
    putWord(payload, 4, 0x0102);
    putWord(payload, 5, 0x0304);
    putWord(payload, 6, 0x0506);
    putWord(payload, 7, 0x0708);
    std::vector<std::uint16_t> image;
    cam86::protocol::decodeFramePayload(payload, false, 0, 2, image);
    require(image[0] == 0x0102 && image[cam86::kSensorWidth] == 0x0304 &&
            image[cam86::kSensorWidth + 1] == 0x0506 && image[1] == 0x0708,
            "Unbinned sensor mosaic order is wrong");
}

void testBinnedFrameDecode() {
    std::vector<std::uint8_t> payload(3008, 0);
    putWord(payload, 4, 0x1234);
    std::vector<std::uint16_t> image;
    cam86::protocol::decodeFramePayload(payload, true, 10, 2, image);
    const auto index = static_cast<std::size_t>(10 * cam86::kSensorWidth);
    require(image[index] == 0x1234 && image[index + 1] == 0x1234 &&
            image[index + cam86::kSensorWidth] == 0x1234,
            "Binned sample was not expanded to its 2x2 display block");
}

void testDemoCapturePipeline() {
    auto camera = cam86::makeSimulatedCamera();
    camera->connect();
    std::atomic_bool cancel{false};
    cam86::ExposureRequest request;
    request.seconds = 0.0;
    auto frame = camera->capture(request, cancel, [](float, cam86::CameraState) {});
    require(frame.pixels.size() == static_cast<std::size_t>(cam86::kSensorWidth * cam86::kSensorHeight),
            "Demo camera returned an incomplete frame");
    cam86::ImageProcessor processor;
    processor.accept(std::move(frame), cam86::DarkMode::Normal);
    processor.rebuild(false, -1);
    require(processor.previewRgba().size() ==
                static_cast<std::size_t>(processor.previewWidth() * processor.previewHeight() * 4),
            "Image preview pipeline returned the wrong RGBA size");
    camera->disconnect();
}

} // namespace

int main() {
    try {
        testFtdiStatusStripping();
        testSpiWaveformAndResponse();
        testUnbinnedFrameDecode();
        testBinnedFrameDecode();
        testDemoCapturePipeline();
        std::cout << "All CAM86 core tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Test failure: " << error.what() << '\n';
        return 1;
    }
}
