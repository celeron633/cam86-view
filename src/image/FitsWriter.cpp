#include "cam86/image/FitsWriter.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cstdio>
#include <fstream>
#include <stdexcept>
#include <string>

namespace cam86 {
namespace {

std::string card(std::string text) {
    if (text.size() > 80) text.resize(80);
    text.resize(80, ' ');
    return text;
}

void appendCard(std::string& header, const std::string& text) { header += card(text); }

} // namespace

void writeFits(const std::filesystem::path& path, const Frame& frame,
               const std::uint16_t displayBlack, const std::uint16_t displayWhite) {
    if (frame.pixels.size() != static_cast<std::size_t>(frame.width * frame.height)) {
        throw std::invalid_argument("Cannot write an incomplete frame as FITS");
    }
    std::ofstream output(path, std::ios::binary);
    if (!output) throw std::runtime_error("Cannot create FITS file: " + path.string());

    char number[64]{};
    std::string header;
    appendCard(header, "SIMPLE  =                    T");
    appendCard(header, "BITPIX  =                   16");
    appendCard(header, "NAXIS   =                    2");
    std::snprintf(number, sizeof(number), "NAXIS1  = %20d", frame.width);
    appendCard(header, number);
    std::snprintf(number, sizeof(number), "NAXIS2  = %20d", frame.height);
    appendCard(header, number);
    appendCard(header, "BSCALE  =   1.0000000000000000");
    appendCard(header, "BZERO   =   32768.000000000000");
    std::snprintf(number, sizeof(number), "CBLACK  = %20u", displayBlack);
    appendCard(header, number);
    std::snprintf(number, sizeof(number), "CWHITE  = %20u", displayWhite);
    appendCard(header, number);
    std::snprintf(number, sizeof(number), "EXPOSURE= %20.6f", frame.exposureSeconds);
    appendCard(header, number);
    std::snprintf(number, sizeof(number), "CCD-TEMP= %20.2f", frame.sensorTemperature);
    appendCard(header, number);
    appendCard(header, "END");
    header.resize(((header.size() + 2879U) / 2880U) * 2880U, ' ');
    output.write(header.data(), static_cast<std::streamsize>(header.size()));

    for (const auto pixel : frame.pixels) {
        const auto signedValue = static_cast<std::int16_t>(static_cast<std::int32_t>(pixel) - 32768);
        const auto raw = static_cast<std::uint16_t>(signedValue);
        const std::array<char, 2> bigEndian{
            static_cast<char>((raw >> 8U) & 0xFFU), static_cast<char>(raw & 0xFFU)};
        output.write(bigEndian.data(), 2);
    }
    const auto dataBytes = frame.pixels.size() * 2U;
    const auto padding = (2880U - dataBytes % 2880U) % 2880U;
    const std::array<char, 2880> zeros{};
    output.write(zeros.data(), static_cast<std::streamsize>(padding));
    if (!output) throw std::runtime_error("Failed while writing FITS file: " + path.string());
}

} // namespace cam86

