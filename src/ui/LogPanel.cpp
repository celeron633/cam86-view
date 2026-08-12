#include "ui/LogPanel.hpp"

#include <imgui.h>

namespace cam86::ui {

void LogPanel::draw(AppState& state) {
    if (ImGui::BeginPopupContextWindow("log-context")) {
        if (ImGui::MenuItem("Clear")) state.log.clear();
        ImGui::EndPopup();
    }
    for (const auto& line : state.log) ImGui::TextUnformatted(line.c_str());
    if (state.log.size() != lastSize_) {
        scrollToBottom_ = true;
        lastSize_ = state.log.size();
    }
    if (scrollToBottom_) {
        ImGui::SetScrollHereY(1.0F);
        scrollToBottom_ = false;
    }
}

} // namespace cam86::ui

