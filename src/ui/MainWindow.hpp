#pragma once

#include "app/AppState.hpp"
#include "ui/ControlPanel.hpp"
#include "ui/ImagePanel.hpp"
#include "ui/LogPanel.hpp"

namespace cam86::ui {

class MainWindow {
public:
    explicit MainWindow(AppState& state) : state_(state) {}
    void tick();
    void draw();

private:
    void drawAcquisitionArea();
    void processCompletedFrame(Frame frame);
    void appendStatistics();

    AppState& state_;
    ImagePanel imagePanel_;
    ControlPanel controlPanel_;
    LogPanel logPanel_;
    std::chrono::steady_clock::time_point nextTemperatureRead_ = std::chrono::steady_clock::now();
};

} // namespace cam86::ui

