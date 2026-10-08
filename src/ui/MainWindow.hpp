#pragma once
#include "app/AppState.hpp"
#include <QMainWindow>
class QSlider;
namespace cam86::ui {
class ImagePanel;
class ControlPanel;
class LogPanel;
class MainWindow : public QMainWindow {
public:
    explicit MainWindow(AppState& state);
    void tick();
private:
    void processCompletedFrame(Frame frame);
    void appendStatistics();
    AppState& state_;
    ImagePanel* mainImage_;
    ImagePanel* crop_;
    ImagePanel* histogram_;
    ControlPanel* controls_;
    LogPanel* log_;
    QSlider* gain_;
    QSlider* offset_;
    std::chrono::steady_clock::time_point nextTemperatureRead_ = std::chrono::steady_clock::now();
};
}
