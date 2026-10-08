#include "app/Application.hpp"
#include "app/AppState.hpp"
#include "cam86/util/Config.hpp"
#include "ui/MainWindow.hpp"
#include <QApplication>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <exception>
#include <filesystem>
#include <string>

namespace cam86 {
namespace {
void loadSettings(Config& config, AppState& state) {
    config.load();
    state.simulation = config.getBool("simulation", true);
    state.exposureIndex = std::clamp(config.getInt("exposure", config.getInt("exp", 0)),
                                     0, static_cast<int>(kExposureChoices.size()) - 1);
    state.offset = std::clamp(config.getInt("offset", 0), -127, 127);
    state.gain = std::clamp(config.getInt("gain", 0), 0, 63);
    state.isoIndex = std::clamp(config.getInt("iso", 9), 0, 9);
    state.targetTemperature = std::clamp(config.getInt("target_temperature", 0), -30, 26);
    state.bin2x2 = config.getBool("bin2x2", false);
    state.roi = config.getBool("roi", false);
    const auto filename = config.getString("filename", config.getString("name", "FileName"));
    std::strncpy(state.fileName.data(), filename.c_str(), state.fileName.size() - 1);
    state.fileName.back() = '\0';
    if (!state.camera.hardwareAvailable() && !state.simulation) state.simulation = true;
    state.camera.useSimulation(state.simulation);
}

void saveSettings(Config& config, const AppState& state) {
    config.set("simulation", state.simulation);
    config.set("exposure", state.exposureIndex);
    config.set("offset", state.offset);
    config.set("gain", state.gain);
    config.set("iso", state.isoIndex);
    config.set("target_temperature", state.targetTemperature);
    config.set("bin2x2", state.bin2x2);
    config.set("roi", state.roi);
    config.set("filename", std::string(state.fileName.data()));
    config.save();
}

} // namespace

int Application::run(int argc, char** argv) {
    QApplication application(argc, argv);
    QApplication::setApplicationName("CAM86-View");
    QApplication::setApplicationVersion("0.2.0");
    AppState state;
    Config config(std::filesystem::current_path() / "cam86.ini");
    loadSettings(config, state);
    state.addLog("CAM86-View v0.2 ready");
    state.addLog(state.camera.hardwareAvailable()
        ? std::string("Hardware backend: ") + hardwareBackendName()
        : "Hardware backend is not built; demo camera remains available");
    ui::MainWindow window(state);
    window.show();
    const int result = application.exec();
    state.camera.disconnect();
    try { saveSettings(config, state); }
    catch (const std::exception& error) { std::fprintf(stderr, "%s\n", error.what()); }
    return result;
}
} // namespace cam86
