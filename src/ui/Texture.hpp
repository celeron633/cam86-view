#pragma once

#include <cstdint>
#include <span>

namespace cam86::ui {

class Texture {
public:
    Texture() = default;
    ~Texture();
    Texture(const Texture&) = delete;
    Texture& operator=(const Texture&) = delete;

    void upload(int width, int height, std::span<const std::uint8_t> rgba);
    [[nodiscard]] std::uint64_t imguiId() const noexcept;
    [[nodiscard]] bool valid() const noexcept { return id_ != 0; }

private:
    unsigned int id_ = 0;
    int width_ = 0;
    int height_ = 0;
};

} // namespace cam86::ui
