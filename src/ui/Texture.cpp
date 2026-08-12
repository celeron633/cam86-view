#include "ui/Texture.hpp"

#if defined(__APPLE__)
#include <OpenGL/gl3.h>
#else
#if defined(_WIN32)
#include <windows.h>
#endif
#include <GL/gl.h>
#endif

#include <cstdint>
#include <stdexcept>

#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE 0x812F
#endif

namespace cam86::ui {

Texture::~Texture() {
    if (id_ != 0) glDeleteTextures(1, &id_);
}

void Texture::upload(const int width, const int height, const std::span<const std::uint8_t> rgba) {
    if (width <= 0 || height <= 0 || rgba.size() != static_cast<std::size_t>(width * height * 4)) {
        throw std::invalid_argument("Invalid RGBA texture data");
    }
    if (id_ == 0) {
        glGenTextures(1, &id_);
        glBindTexture(GL_TEXTURE_2D, id_);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    } else {
        glBindTexture(GL_TEXTURE_2D, id_);
    }
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
    glBindTexture(GL_TEXTURE_2D, 0);
    width_ = width;
    height_ = height;
}

std::uint64_t Texture::imguiId() const noexcept {
    return static_cast<std::uint64_t>(id_);
}

} // namespace cam86::ui
