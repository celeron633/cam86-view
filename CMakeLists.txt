cmake_minimum_required(VERSION 3.24)

project(cam86_view VERSION 0.3.0 LANGUAGES C CXX)

option(CAM86_ENABLE_LIBUSB "Build the real CAM86/libusb backend when libusb is available" ON)
option(CAM86_BUILD_TESTS "Build protocol and image-processing tests" ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(Qt6 6.2 REQUIRED COMPONENTS Widgets)

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
  src/usb/D2xxFtdiTransport.cpp
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

add_library(cam86_ui
  src/ui/theme.qrc
  src/ui/ControlPanel.cpp
  src/ui/ImagePanel.cpp
  src/ui/HistogramDialog.cpp
  src/ui/SelectionDialog.cpp
  src/ui/LogPanel.cpp
  src/ui/MainWindow.cpp
)
set_target_properties(cam86_ui PROPERTIES AUTORCC ON)
target_include_directories(cam86_ui PUBLIC include src)
target_link_libraries(cam86_ui PUBLIC cam86_core Qt6::Widgets)
target_compile_definitions(cam86_ui PRIVATE CAM86_VERSION="${PROJECT_VERSION}")

add_executable(cam86-view src/main.cpp src/app/Application.cpp)
target_link_libraries(cam86-view PRIVATE cam86_ui)
if(MSVC)
  target_compile_options(cam86_ui PRIVATE /W4 /permissive- /utf-8)
  target_compile_options(cam86-view PRIVATE /utf-8)
endif()
if(WIN32)
  set_target_properties(cam86-view PROPERTIES WIN32_EXECUTABLE TRUE)
  target_compile_definitions(cam86-view PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
  if(MSVC)
    set(CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS_SKIP TRUE)
    include(InstallRequiredSystemLibraries)
    if(CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS)
      add_custom_command(TARGET cam86-view POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS}
                "$<TARGET_FILE_DIR:cam86-view>"
        COMMENT "Deploying Microsoft C++ runtime" COMMAND_EXPAND_LISTS VERBATIM)
    endif()
  endif()
  get_target_property(CAM86_QMAKE_EXECUTABLE Qt6::qmake IMPORTED_LOCATION)
  get_filename_component(CAM86_QT_BIN_DIR "${CAM86_QMAKE_EXECUTABLE}" DIRECTORY)
  find_program(CAM86_WINDEPLOYQT windeployqt HINTS "${CAM86_QT_BIN_DIR}" REQUIRED)
  add_custom_command(TARGET cam86-view POST_BUILD
    COMMAND "${CAM86_WINDEPLOYQT}" --no-translations --no-opengl-sw --no-system-d3d-compiler --no-compiler-runtime
            "$<TARGET_FILE:cam86-view>"
    COMMENT "Deploying Qt runtime" VERBATIM)
endif()

if(CAM86_BUILD_TESTS)
  enable_testing()
  add_executable(cam86-tests tests/CoreTests.cpp)
  target_link_libraries(cam86-tests PRIVATE cam86_core)
  add_test(NAME cam86-core-tests COMMAND cam86-tests)
  add_executable(cam86-ui-tests tests/UiTests.cpp)
  target_link_libraries(cam86-ui-tests PRIVATE cam86_ui)
  if(WIN32)
    add_test(NAME cam86-qt-ui-tests COMMAND cam86-ui-tests -platform windows)
  else()
    add_test(NAME cam86-qt-ui-tests COMMAND cam86-ui-tests -platform offscreen)
  endif()
  set_tests_properties(cam86-qt-ui-tests PROPERTIES TIMEOUT 60)
  if(WIN32)
    set_tests_properties(cam86-qt-ui-tests PROPERTIES
      ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${CAM86_QT_BIN_DIR}")
  endif()
endif()

message(STATUS "CAM86 libusb backend: ${CAM86_HAS_LIBUSB}")
