cmake_minimum_required(VERSION 3.24)

project(cam86_view VERSION 0.2.0 LANGUAGES C CXX)

option(CAM86_ENABLE_LIBUSB "Build the real CAM86/libusb backend when libusb is available" ON)
option(CAM86_BUILD_TESTS "Build protocol and image-processing tests" ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(FetchContent)

set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
  glfw
  GIT_REPOSITORY https://github.com/glfw/glfw.git
  GIT_TAG 3.4
  GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(glfw)

FetchContent_Declare(
  imgui
  GIT_REPOSITORY https://github.com/ocornut/imgui.git
  GIT_TAG v1.91.9b
  GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(imgui)

find_package(OpenGL REQUIRED)

add_library(imgui STATIC
  ${imgui_SOURCE_DIR}/imgui.cpp
  ${imgui_SOURCE_DIR}/imgui_demo.cpp
  ${imgui_SOURCE_DIR}/imgui_draw.cpp
  ${imgui_SOURCE_DIR}/imgui_tables.cpp
  ${imgui_SOURCE_DIR}/imgui_widgets.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
)
target_include_directories(imgui PUBLIC
  ${imgui_SOURCE_DIR}
  ${imgui_SOURCE_DIR}/backends
)
target_link_libraries(imgui PUBLIC glfw OpenGL::GL)

set(CAM86_HAS_LIBUSB OFF)
if(CAM86_ENABLE_LIBUSB)
  find_package(unofficial-libusb-1.0 CONFIG QUIET)
  if(TARGET unofficial::libusb::libusb)
    set(CAM86_LIBUSB_TARGET unofficial::libusb::libusb)
    set(CAM86_HAS_LIBUSB ON)
  else()
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
      pkg_check_modules(LIBUSB QUIET IMPORTED_TARGET libusb-1.0)
      if(TARGET PkgConfig::LIBUSB)
        set(CAM86_LIBUSB_TARGET PkgConfig::LIBUSB)
        set(CAM86_HAS_LIBUSB ON)
      endif()
    endif()
  endif()
endif()

add_library(cam86_core
  src/camera/Cam86Camera.cpp
  src/camera/CameraController.cpp
  src/camera/SimulatedCamera.cpp
  src/image/FitsWriter.cpp
  src/image/ImageProcessor.cpp
  src/usb/FtdiProtocol.cpp
  src/usb/LibusbFtdiTransport.cpp
  src/util/Config.cpp
)
target_include_directories(cam86_core PUBLIC include)
target_compile_definitions(cam86_core PUBLIC CAM86_HAS_LIBUSB=$<BOOL:${CAM86_HAS_LIBUSB}>)
if(WIN32)
  target_compile_definitions(cam86_core PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
endif()
if(CAM86_HAS_LIBUSB)
  target_link_libraries(cam86_core PRIVATE ${CAM86_LIBUSB_TARGET})
endif()

if(MSVC)
  target_compile_options(cam86_core PRIVATE /W4 /permissive- /utf-8)
else()
  target_compile_options(cam86_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(cam86-view
  src/main.cpp
  src/app/Application.cpp
  src/ui/ControlPanel.cpp
  src/ui/FileDialog.cpp
  src/ui/ImagePanel.cpp
  src/ui/LogPanel.cpp
  src/ui/MainWindow.cpp
  src/ui/Texture.cpp
)
target_include_directories(cam86-view PRIVATE include src)
target_link_libraries(cam86-view PRIVATE cam86_core imgui)
if(WIN32)
  set_target_properties(cam86-view PROPERTIES WIN32_EXECUTABLE TRUE)
  target_compile_definitions(cam86-view PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
endif()

if(CAM86_BUILD_TESTS)
  enable_testing()
  add_executable(cam86-tests tests/CoreTests.cpp)
  target_link_libraries(cam86-tests PRIVATE cam86_core)
  add_test(NAME cam86-core-tests COMMAND cam86-tests)
endif()

message(STATUS "CAM86 libusb backend: ${CAM86_HAS_LIBUSB}")
