#pragma once

#include "app/AppState.hpp"
#include "ui/Texture.hpp"

namespace cam86::ui {

class ImagePanel {
public:
    void drawMain(AppState& state);
    void drawCrop(AppState& state);
    void drawHistogram(const AppState& state);

private:
    void refreshTextures(AppState& state);
    Texture mainTexture_;
    Texture cropTexture_;
    std::uint64_t uploadedRevision_ = 0;
};

} // namespace cam86::ui

