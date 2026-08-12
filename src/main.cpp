#include "app/Application.hpp"

#include <cstdio>
#include <exception>

#ifdef _WIN32
#include <windows.h>

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    try {
        cam86::Application application;
        return application.run();
    } catch (const std::exception& error) {
        MessageBoxA(nullptr, error.what(), "CAM86-View", MB_OK | MB_ICONERROR);
        return 1;
    }
}
#else
int main() {
    try {
        cam86::Application application;
        return application.run();
    } catch (const std::exception& error) {
        std::fprintf(stderr, "CAM86-View: %s\n", error.what());
        return 1;
    }
}
#endif
