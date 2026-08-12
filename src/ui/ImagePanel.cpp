#include "ui/ImagePanel.hpp"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace cam86::ui {

void ImagePanel::refreshTextures(AppState& state) {
    if (!state.image.hasImage() || uploadedRevision_ == state.image.revision()) return;
    mainTexture_.upload(state.image.previewWidth(), state.image.previewHeight(), state.image.previewRgba());
    cropTexture_.upload(50, 50, state.image.cropRgba());
    uploadedRevision_ = state.image.revision();
}

void ImagePanel::drawMain(AppState& state) {
    refreshTextures(state);
    const auto available = ImGui::GetContentRegionAvail();
    const auto width = std::max(1.0F, available.x);
    const auto height = std::max(1.0F, std::min(available.y, width * 2.0F / 3.0F));
    if (!mainTexture_.valid()) {
        const auto position = ImGui::GetCursorScreenPos();
        ImGui::InvisibleButton("main-image-empty", ImVec2(width, height));
        ImGui::GetWindowDrawList()->AddRectFilled(position, ImVec2(position.x + width, position.y + height),
                                                   IM_COL32(28, 31, 35, 255));
        ImGui::GetWindowDrawList()->AddText(ImVec2(position.x + 14, position.y + 12), IM_COL32(150, 155, 160, 255),
                                            "3000 x 2000 image");
        return;
    }
    ImGui::ImageButton("main-image", mainTexture_.imguiId(), ImVec2(width, height),
                       ImVec2(0, 0), ImVec2(1, 1), ImVec4(0, 0, 0, 1), ImVec4(1, 1, 1, 1));
    if (ImGui::IsItemClicked()) {
        const auto min = ImGui::GetItemRectMin();
        const auto mouse = ImGui::GetMousePos();
        state.image.select((mouse.x - min.x) / width, (mouse.y - min.y) / height);
    }
}

void ImagePanel::drawCrop(AppState& state) {
    refreshTextures(state);
    const auto available = ImGui::GetContentRegionAvail();
    const auto side = std::max(1.0F, std::min(available.x, available.y));
    if (cropTexture_.valid()) {
        ImGui::Image(cropTexture_.imguiId(), ImVec2(side, side));
    } else {
        const auto position = ImGui::GetCursorScreenPos();
        ImGui::Dummy(ImVec2(side, side));
        ImGui::GetWindowDrawList()->AddRectFilled(position, ImVec2(position.x + side, position.y + side),
                                                   IM_COL32(28, 31, 35, 255));
    }
}

void ImagePanel::drawHistogram(const AppState& state) {
    std::array<float, 256> red{};
    std::array<float, 256> green{};
    std::array<float, 256> blue{};
    const auto& hr = state.image.histogramR();
    const auto& hg = state.image.histogramG();
    const auto& hb = state.image.histogramB();
    float maximum = 1.0F;
    for (std::size_t i = 0; i < red.size(); ++i) {
        red[i] = std::log1p(static_cast<float>(hr[i]));
        green[i] = std::log1p(static_cast<float>(hg[i]));
        blue[i] = std::log1p(static_cast<float>(hb[i]));
        maximum = std::max({maximum, red[i], green[i], blue[i]});
    }

    const auto origin = ImGui::GetCursorScreenPos();
    const auto size = ImGui::GetContentRegionAvail();
    ImGui::InvisibleButton("histogram", ImVec2(size.x, std::max(60.0F, size.y)));
    auto* draw = ImGui::GetWindowDrawList();
    const auto end = ImGui::GetItemRectMax();
    draw->AddRectFilled(origin, end, IM_COL32(20, 22, 25, 255));
    const auto plot = [&](const std::array<float, 256>& values, const ImU32 color) {
        for (std::size_t i = 1; i < values.size(); ++i) {
            const auto x0 = origin.x + (static_cast<float>(i - 1) / 255.0F) * size.x;
            const auto x1 = origin.x + (static_cast<float>(i) / 255.0F) * size.x;
            const auto y0 = end.y - values[i - 1] / maximum * (end.y - origin.y - 3.0F);
            const auto y1 = end.y - values[i] / maximum * (end.y - origin.y - 3.0F);
            draw->AddLine(ImVec2(x0, y0), ImVec2(x1, y1), color, 1.0F);
        }
    };
    plot(red, IM_COL32(230, 75, 70, 220));
    plot(green, IM_COL32(70, 220, 100, 220));
    plot(blue, IM_COL32(80, 130, 240, 220));
}

} // namespace cam86::ui
