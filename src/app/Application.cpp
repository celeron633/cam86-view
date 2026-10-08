#include "app/Application.hpp"

#include "app/AppState.hpp"
#include "cam86/util/Config.hpp"
#include "ui/MainWindow.hpp"

#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <exception>
#include <filesystem>
#include <stdexcept>
#include <string>

namespace cam86 {
namespace {

void glfwErrorCallback(const int error, const char* description) {
    std::fprintf(stderr, "GLFW error %d: %s\n", error, description);
}

void applyClassicLightTheme() {
    ImGui::StyleColorsLight();
    auto& style = ImGui::GetStyle();
    style.WindowPadding = ImVec2(6, 6);
    style.FramePadding = ImVec2(5, 4);
    style.ItemSpacing = ImVec2(6, 5);
    style.WindowRounding = 0.0F;
    style.ChildRounding = 0.0F;
    style.FrameRounding = 1.0F;
    style.ScrollbarRounding = 0.0F;
    style.GrabRounding = 1.0F;
    style.ChildBorderSize = 1.0F;
    style.FrameBorderSize = 1.0F;
    style.Colors[ImGuiCol_WindowBg] = ImVec4(0.78F, 0.79F, 0.80F, 1.0F);
    style.Colors[ImGuiCol_ChildBg] = ImVec4(0.82F, 0.82F, 0.82F, 1.0F);
    style.Colors[ImGuiCol_FrameBg] = ImVec4(0.93F, 0.93F, 0.93F, 1.0F);
    style.Colors[ImGuiCol_Border] = ImVec4(0.40F, 0.40F, 0.40F, 0.75F);
}

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

int Application::run() {
    glfwSetErrorCallback(glfwErrorCallback);
    if (!glfwInit()) throw std::runtime_error("Could not initialize GLFW");

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
#endif
    GLFWwindow* window = glfwCreateWindow(1180, 720, "CAM86-View v0.2", nullptr, nullptr);
    if (window == nullptr) {
        glfwTerminate();
        throw std::runtime_error("Could not create the OpenGL window");
    }
    glfwSetWindowSizeLimits(window, 960, 600, GLFW_DONT_CARE, GLFW_DONT_CARE);
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    applyClassicLightTheme();
    ImGui::GetIO().IniFilename = nullptr;
    if (!ImGui_ImplGlfw_InitForOpenGL(window, true) || !ImGui_ImplOpenGL3_Init("#version 330")) {
        ImGui::DestroyContext();
        glfwDestroyWindow(window);
        glfwTerminate();
        throw std::runtime_error("Could not initialize the ImGui backends");
    }

    AppState state;
    Config config(std::filesystem::current_path() / "cam86.ini");
    loadSettings(config, state);
    state.addLog("CAM86-View v0.2 ready");
    state.addLog(state.camera.hardwareAvailable()
        ? std::string("Hardware backend: ") + hardwareBackendName()
        : "Hardware backend is not built; demo camera remains available");
    ui::MainWindow mainWindow(state);

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();
        mainWindow.tick();

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
        mainWindow.draw();
        ImGui::Render();

        int displayWidth = 0;
        int displayHeight = 0;
        glfwGetFramebufferSize(window, &displayWidth, &displayHeight);
        glViewport(0, 0, displayWidth, displayHeight);
        glClearColor(0.18F, 0.20F, 0.22F, 1.0F);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(window);
    }

    state.camera.disconnect();
    try { saveSettings(config, state); }
    catch (const std::exception& error) { std::fprintf(stderr, "%s\n", error.what()); }
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}

} // namespace cam86
