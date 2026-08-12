#include "ui/MainWindow.hpp"

#include "cam86/image/FitsWriter.hpp"

#include <imgui.h>

#include <algorithm>
#include <chrono>
#include <exception>
#include <filesystem>
#include <iomanip>
#include <sstream>
#include <string>
#include <utility>

namespace cam86::ui {

void MainWindow::tick() {
    for (auto& message : state_.camera.takeMessages()) state_.addLog(std::move(message));
    if (auto temperature = state_.camera.takeTemperature()) state_.sensorTemperature = *temperature;
    if (auto frame = state_.camera.takeFrame()) processCompletedFrame(std::move(*frame));

    const auto now = std::chrono::steady_clock::now();
    if (state_.camera.isConnected() && !state_.busy() && now >= nextTemperatureRead_) {
        state_.camera.requestTemperature();
        nextTemperatureRead_ = now + std::chrono::seconds(2);
    }

    const bool belowLimit = state_.infiniteFrames || state_.frameNumber < state_.continuousFrames;
    if (state_.continuous && state_.camera.isConnected() && !state_.busy() &&
        belowLimit && now >= state_.nextCapture) {
        ExposureRequest request;
        request.bin2x2 = state_.bin2x2;
        request.roi = state_.roi;
        request.roiCenterY = state_.image.selectionY();
        request.seconds = state_.exposureSeconds();
        state_.camera.startCapture(request);
    } else if (state_.continuous && !state_.infiniteFrames && !belowLimit) {
        state_.continuous = false;
        state_.addLog("Continuous capture complete");
    }
}

void MainWindow::draw() {
    const auto* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    constexpr auto flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                           ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus;
    ImGui::Begin("CAM86-View", nullptr, flags);
    drawAcquisitionArea();
    ImGui::End();
}

void MainWindow::drawAcquisitionArea() {
    const auto available = ImGui::GetContentRegionAvail();
    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    const float controlsWidth = std::clamp(available.x * 0.225F, 250.0F, 310.0F);
    const float centerWidth = std::clamp(available.x * 0.205F, 210.0F, 300.0F);
    const float leftWidth = std::max(300.0F, available.x - controlsWidth - centerWidth - spacing * 2.0F);

    ImGui::BeginChild("left-column", ImVec2(leftWidth, 0), ImGuiChildFlags_Borders);
    constexpr float sliderArea = 105.0F;
    ImGui::BeginChild("image", ImVec2(0, -sliderArea), ImGuiChildFlags_Borders);
    imagePanel_.drawMain(state_);
    ImGui::EndChild();
    ImGui::Text("Gain %d", state_.gain);
    ImGui::SameLine(68);
    ImGui::BeginDisabled(!state_.camera.isConnected());
    ImGui::SetNextItemWidth(-1);
    if (ImGui::SliderInt("##gain", &state_.gain, 0, 63)) state_.camera.setGain(state_.gain);
    ImGui::Text("Offset %d", state_.offset);
    ImGui::SameLine(68);
    ImGui::SetNextItemWidth(-1);
    if (ImGui::SliderInt("##offset", &state_.offset, -127, 127)) state_.camera.setOffset(state_.offset);
    ImGui::EndDisabled();
    ImGui::EndChild();

    ImGui::SameLine();
    ImGui::BeginChild("center-column", ImVec2(centerWidth, 0));
    const float centerHeight = ImGui::GetContentRegionAvail().y;
    ImGui::BeginChild("log", ImVec2(0, centerHeight * 0.38F), ImGuiChildFlags_Borders,
                      ImGuiWindowFlags_HorizontalScrollbar);
    logPanel_.draw(state_);
    ImGui::EndChild();
    ImGui::BeginChild("crop", ImVec2(0, centerHeight * 0.39F), ImGuiChildFlags_Borders);
    imagePanel_.drawCrop(state_);
    ImGui::EndChild();
    ImGui::BeginChild("histogram", ImVec2(0, 0), ImGuiChildFlags_Borders);
    imagePanel_.drawHistogram(state_);
    ImGui::EndChild();
    ImGui::EndChild();

    ImGui::SameLine();
    ImGui::BeginChild("control-column", ImVec2(controlsWidth, 0), ImGuiChildFlags_Borders,
                      ImGuiWindowFlags_AlwaysVerticalScrollbar);
    controlPanel_.draw(state_);
    ImGui::EndChild();
}

void MainWindow::processCompletedFrame(Frame frame) {
    try {
        state_.sensorTemperature = frame.sensorTemperature;
        state_.image.accept(std::move(frame), state_.darkMode);
        state_.image.rebuild(state_.bin2x2, state_.isoShift());
        if (state_.writeFits) {
            const auto path = std::filesystem::path(
                std::string(state_.fileName.data()) + std::to_string(state_.frameNumber) + ".fit");
            writeFits(path, state_.image.frame(), state_.image.blackLevel(), state_.image.whiteLevel());
            state_.addLog("File " + path.string() + " is recorded");
        }
        if (state_.information) appendStatistics();
        ++state_.frameNumber;
        state_.nextCapture = std::chrono::steady_clock::now() + std::chrono::seconds(state_.delaySeconds);
    } catch (const std::exception& error) {
        state_.addLog(std::string("Frame processing failed: ") + error.what());
        state_.continuous = false;
    }
}

void MainWindow::appendStatistics() {
    const auto stats = state_.image.statistics();
    std::ostringstream text;
    text << std::fixed << std::setprecision(2)
         << "StdDev (frame) = " << stats.standardDeviation
         << ", StdDev (line) = " << stats.lineStandardDeviation;
    state_.addLog(text.str());
    text.str({}); text.clear();
    text << "MinValue = " << stats.minimum << ", MaxValue = " << stats.maximum
         << ", Mean = " << std::fixed << std::setprecision(2) << stats.mean;
    state_.addLog(text.str());
    text.str({}); text.clear();
    text << "Mean R/G/B = " << std::fixed << std::setprecision(2)
         << stats.redMean << " / " << stats.greenMean << " / " << stats.blueMean;
    state_.addLog(text.str());
}

} // namespace cam86::ui
