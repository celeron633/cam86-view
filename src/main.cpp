#include "app/Application.hpp"
#include <cstdio>
#include <exception>
#ifdef _WIN32
#include <windows.h>
#endif

int main(int argc, char** argv) {
    try {
        cam86::Application application;
        return application.run(argc, argv);
    } catch (const std::exception& error) {
#ifdef _WIN32
        MessageBoxA(nullptr, error.what(), "CAM86-View", MB_OK | MB_ICONERROR);
#else
        std::fprintf(stderr, "CAM86-View: %s\n", error.what());
#endif
        return 1;
    }
}
