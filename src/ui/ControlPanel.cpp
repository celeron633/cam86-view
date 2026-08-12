#include "ui/ControlPanel.hpp"

#include "cam86/image/FitsWriter.hpp"

#include <imgui.h>

#include <algorithm>
#include <chrono>
#include <exception>
#include <filesystem>
#include <string>

namespace cam86::ui {

void ControlPanel::draw(AppState& state) {
    const bool connected = state.camera.isConnected();
    const bool busy = state.busy();
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(5.0F, 2.0F));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6.0F, 3.0F));

    ImGui::BeginDisabled(connected || busy);
    int backend = state.simulation ? 0 : 1;
    const char* backends[] = {"Demo camera", "CAM86 / libusb"};
    ImGui::SetNextItemWidth(-1);
    if (ImGui::Combo("##backend", &backend, backends, 2)) {
        state.simulation = backend == 0;
        state.camera.useSimulation(state.simulation);
    }
    ImGui::EndDisabled();

    if (!connected) {
        if (ImGui::Button("CONNECT", ImVec2(-1, 30))) state.camera.connect();
    } else if (ImGui::Button("DISCONNECT", ImVec2(-1, 30))) {
        state.continuous = false;
        state.camera.disconnect();
    }

    ImGui::SeparatorText("Capture");
    if (ImGui::BeginTable("capture-options", 2, ImGuiTableFlags_SizingStretchSame)) {
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::Checkbox("bin 2*2", &state.bin2x2);
        ImGui::TableNextColumn();
        ImGui::Checkbox("ROI", &state.roi);
        ImGui::EndTable();
    }

    if (ImGui::BeginTable("sequence-options", 2, ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("label", ImGuiTableColumnFlags_WidthFixed, 72.0F);
        ImGui::TableSetupColumn("value", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Frames");
        ImGui::TableNextColumn();
        ImGui::SetNextItemWidth(-1);
        ImGui::InputInt("##frames", &state.continuousFrames, 0);
        state.continuousFrames = std::max(1, state.continuousFrames);
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Delay (s)");
        ImGui::TableNextColumn();
        ImGui::SetNextItemWidth(-1);
        ImGui::InputInt("##delay-seconds", &state.delaySeconds, 0);
        state.delaySeconds = std::max(0, state.delaySeconds);
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TableNextColumn();
        ImGui::Checkbox("Infinite capture", &state.infiniteFrames);
        ImGui::EndTable();
    }

    ImGui::Spacing();
    ImGui::TextDisabled("Exposure");
    state.exposureIndex = std::clamp(state.exposureIndex, 0, static_cast<int>(kExposureChoices.size()) - 1);
    ImGui::SetNextItemWidth(-1);
    if (ImGui::BeginCombo("##exposure", kExposureChoices[static_cast<std::size_t>(state.exposureIndex)].label)) {
        for (int i = 0; i < static_cast<int>(kExposureChoices.size()); ++i) {
            if (ImGui::Selectable(kExposureChoices[static_cast<std::size_t>(i)].label, state.exposureIndex == i)) {
                state.exposureIndex = i;
            }
        }
        ImGui::EndCombo();
    }

    ImGui::BeginDisabled(!connected);
    if (ImGui::Button(state.continuous ? "Continuous: ON" : "Continuously", ImVec2(-1, 30))) {
        state.continuous = !state.continuous;
        state.frameNumber = 0;
        state.nextCapture = std::chrono::steady_clock::now();
        if (!state.continuous) state.camera.stopCapture();
    }
    ImGui::EndDisabled();

    ImGui::BeginDisabled(!connected || busy);
    if (ImGui::Button("GET IMG", ImVec2(ImGui::GetContentRegionAvail().x * 0.5F - 3, 38))) startOne(state);
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(!busy);
    if (ImGui::Button("STOP", ImVec2(-1, 38))) {
        state.continuous = false;
        state.camera.stopCapture();
    }
    ImGui::EndDisabled();

    ImGui::ProgressBar(state.camera.progress(), ImVec2(-1, 0));

    if (ImGui::CollapsingHeader("iso / files", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (ImGui::BeginTable("iso-file", 2, ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchProp)) {
            ImGui::TableSetupColumn("iso", ImGuiTableColumnFlags_WidthFixed, 68.0F);
            ImGui::TableNextColumn();
            ImGui::TextUnformatted("iso");
            const int oldIso = state.isoIndex;
            for (int i = 0; i < static_cast<int>(kIsoChoices.size()); ++i) {
                ImGui::RadioButton(kIsoChoices[static_cast<std::size_t>(i)], &state.isoIndex, i);
            }
            if (oldIso != state.isoIndex && state.image.hasImage()) {
                state.image.rebuild(state.bin2x2, state.isoShift());
            }

            ImGui::TableNextColumn();
            ImGui::Checkbox("Information", &state.information);
            ImGui::TextDisabled("Output prefix");
            ImGui::SetNextItemWidth(-1);
            ImGui::InputText("##filename", state.fileName.data(), state.fileName.size());
            const bool previousWrite = state.writeFits;
            ImGui::Checkbox("Write FITS", &state.writeFits);
            if (!previousWrite && state.writeFits) state.frameNumber = 0;
            if (ImGui::Button("WriteFile", ImVec2(-1, 0))) writeCurrentFits(state);

            ImGui::SeparatorText("Mode");
            int mode = static_cast<int>(state.darkMode);
            ImGui::RadioButton("normal", &mode, static_cast<int>(DarkMode::Normal));
            ImGui::RadioButton("dark", &mode, static_cast<int>(DarkMode::Accumulate));
            ImGui::RadioButton("subdark", &mode, static_cast<int>(DarkMode::Subtract));
            if (mode != static_cast<int>(state.darkMode)) {
                state.darkMode = static_cast<DarkMode>(mode);
                if (state.darkMode == DarkMode::Accumulate) state.image.clearDark();
            }
            ImGui::EndTable();
        }
    }

    if (ImGui::BeginTable("dark-buttons", 2, ImGuiTableFlags_SizingStretchSame)) {
        ImGui::TableNextColumn();
        if (ImGui::Button("Save dark", ImVec2(-1, 0))) {
            try {
                state.image.saveDark(state.fileName.data());
                state.addLog("Dark file recorded");
            } catch (const std::exception& error) { state.addLog(error.what()); }
        }
        if (ImGui::Button("View dark", ImVec2(-1, 0))) {
            try {
                state.image.viewDark();
                state.image.rebuild(state.bin2x2, state.isoShift());
            } catch (const std::exception& error) { state.addLog(error.what()); }
        }
        ImGui::TableNextColumn();
        if (ImGui::Button("Load dark...", ImVec2(-1, 0)))
            darkFileDialog_.open(std::filesystem::path(state.fileName.data()), ".drk");
        ImGui::EndTable();
    }

    ImGui::SeparatorText("Temperature");
    ImGui::Text("Sensor: %.1f C", state.sensorTemperature);
    if (ImGui::Button("Read Temp", ImVec2(-1, 0))) state.camera.requestTemperature();
    const int oldTarget = state.targetTemperature;
    if (ImGui::BeginTable("temperature-setpoint", 2, ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("label", ImGuiTableColumnFlags_WidthFixed, 72.0F);
        ImGui::TableSetupColumn("value", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Setpoint");
        ImGui::TableNextColumn();
        ImGui::SetNextItemWidth(-1);
        ImGui::SliderInt("##temperature", &state.targetTemperature, -30, 26, "%d C");
        ImGui::EndTable();
    }
    if (oldTarget != state.targetTemperature) state.camera.setTargetTemperature(state.targetTemperature);
    const bool oldCooling = state.cooling;
    ImGui::Checkbox("Cooling", &state.cooling);
    if (oldCooling != state.cooling) state.camera.setCooling(state.cooling);

    ImGui::PopStyleVar(2);
    if (const auto selected = darkFileDialog_.draw()) {
        try {
            state.image.loadDark(*selected);
            state.addLog("Dark file loaded: " + selected->string() + " (" +
                         std::to_string(state.image.darkFrameCount()) + " frames)");
        } catch (const std::exception& error) { state.addLog(error.what()); }
    }
}

void ControlPanel::startOne(AppState& state) {
    ExposureRequest request;
    request.bin2x2 = state.bin2x2;
    request.roi = state.roi;
    request.roiCenterY = state.image.selectionY();
    request.seconds = state.exposureSeconds();
    state.camera.startCapture(request);
}

void ControlPanel::writeCurrentFits(AppState& state) {
    if (!state.image.hasImage()) {
        state.addLog("No image to write");
        return;
    }
    try {
        const auto path = std::filesystem::path(
            std::string(state.fileName.data()) + std::to_string(state.frameNumber) + ".fit");
        writeFits(path, state.image.frame(), state.image.blackLevel(), state.image.whiteLevel());
        state.addLog("File " + path.string() + " is recorded");
    } catch (const std::exception& error) { state.addLog(error.what()); }
}

} // namespace cam86::ui
