# CAM86-View

详细设计、通信、图像采集与处理文档见 [`doc/README.md`](doc/README.md)。

这是对 `cam86-view-old` Delphi 7 工程的现代 C++20 重构。界面使用 Dear ImGui，构建使用 CMake；Windows 和 Linux 共用同一套相机、图像处理和 UI 代码。

当前实现保留了旧软件的主要布局和功能：

- 左侧 3000×2000 主图、Gain/Offset；
- 中间日志、点击位置的 50×50 放大图、RGB/灰度直方图；
- 右侧连接、2×2 bin、ROI、曝光、连拍、ISO 映射、暗场、FITS 和制冷；
- 无相机时可选 `Demo camera`，用于完整试跑 UI 和处理流程；
- 设置保存到工作目录的 `cam86.ini`。

## 目录结构

```text
include/cam86/
  camera/       相机接口、控制器、CAM86 协议
  image/        Bayer/灰度显示、统计、暗场、FITS
  usb/          FTDI 协议细节和抽象传输接口
src/
  app/          程序生命周期与状态
  camera/       真机和模拟相机
  image/        图像处理实现
  ui/           主窗口、图像、日志、控制面板、OpenGL 纹理
  usb/          libusb FT2232H 后端
tests/          FTDI 状态头、SPI 波形、帧解码测试
```

## 构建

首次配置会通过 CMake FetchContent 下载 GLFW 3.4 和 Dear ImGui 1.91.9b。

### Windows（推荐 vcpkg）

安装 Visual Studio 的“使用 C++ 的桌面开发”组件，并准备 vcpkg：

```powershell
$vcpkgRoot = 'C:\path\to\vcpkg'
cmake -S . -B build/windows -A x64 `
  -DCMAKE_TOOLCHAIN_FILE="$vcpkgRoot\scripts\buildsystems\vcpkg.cmake"
cmake --build build/windows --config Release --parallel
ctest --test-dir build/windows -C Release --output-on-failure
```

程序位于 `build/windows/Release/cam86-view.exe`。Windows 真机默认使用 D2XX，可加 `-DCAM86_ENABLE_LIBUSB=OFF` 构建，不需要 vcpkg；Linux 真机使用 libusb。

### Linux（Debian/Ubuntu）

```bash
sudo apt install build-essential cmake ninja-build libusb-1.0-0-dev \
  libgl1-mesa-dev libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev
cmake --preset release
cmake --build --preset release
ctest --test-dir build/release --output-on-failure
./build/release/cam86-view
```

也可以不用 preset：

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```

## USB / FT2232H 结论

旧工程不是普通串口通信：

- `MyD2XX.pas` 直接动态调用 `FTD2XX.DLL`；
- `Cam86.pas` 打开 `CAM86A` 和 `CAM86B` 两个 FT2232H 通道；
- A 通道通过 bulk IN 接收 CCD 帧；
- B 通道设成异步 bit-bang（旧代码为 `FT_SetBitMode(..., $bf, $4)`），生成 AD9822 和相机 SPI 控制波形；
- 相机命令字 `$4b/$5b/$8b/$6b/$1b/$bf/$ab/$9b` 已按旧代码移植。

因此可以使用 libusb 平替 D2XX，但要补上 D2XX 隐式完成的工作。本项目的后端已经实现 FTDI vendor control request、双接口/四端点、波特率除数、latency、purge，以及每个 bulk IN USB 包前两个 modem-status 字节的剥离。

### Windows 驱动注意事项

Windows 默认使用 D2XX，通过现有 FTDI 驱动打开同一相机的 `CAM86A/CAM86B` 通道，无需通过 Zadig 切换到 WinUSB。程序优先加载系统目录的 `ftd2xx.dll`，再查找 EXE 同目录；DLL 架构必须与程序一致，旧 EXE 附带的 32 位 DLL 不能用于 64 位客户端。连接前关闭其他占用相机的程序。

参考文件注意：`ref/cam8_view-src` 是 CAM8 工程，打开 `CAM8A/CAM8B` 并使用另一套控制时序；可用的 `ref/cam86-bin/cam86-view-01.exe` 包含 `CAM86A/CAM86B`。不能将 CAM8 初始化和采集协议直接套用到 CAM86。

### Linux 权限

程序会在可行时临时 detach 内核 FTDI 驱动，但普通用户仍需要 USB 权限。可按设备实际 serial 收紧规则，例如：

```udev
SUBSYSTEM=="usb", ATTR{idVendor}=="0403", ATTR{idProduct}=="6010", ATTR{serial}=="CAM86", MODE="0660", GROUP="plugdev"
```

写入 `/etc/udev/rules.d/60-cam86.rules` 后执行 `sudo udevadm control --reload-rules`，再重新插拔相机。

## 目前的硬件验证边界

代码已在 Windows 上分别完成“无 libusb”和“libusb 1.0.30”构建，并通过协议/帧解码测试；由于仓库中没有 CAM86 实机，尚未做真实曝光验证。真机首次联调建议先用短曝光和 ROI，重点核对：

1. USB EEPROM 是否仍是默认 FTDI VID/PID `0403:6010`，serial 是否以 `CAM86` 开头；
2. B 通道 bit-bang 时钟和温度读回；
3. 2×2 bin 数据行头。旧 Delphi bin 分支在 3008 字节行长度下用了 `+7` word 偏移，会跨行；新代码按自洽的 4-word 行头处理并有测试覆盖，但仍应以真机数据确认。

如设备枚举参数或 bin 行头与上述不同，只需调整 camera/USB 层，不需要改 UI 和图像模块。

## Delphi 设计器图标说明

旧界面截图底部的三个闹钟和文件夹不是运行时按钮，而是 Delphi 7 Form Designer 对非可视组件的标记：

- `Timer1`：曝光进度，现由相机工作线程的 progress callback 驱动；
- `Timer2`：检查相机状态并触发下一帧，现由每帧 UI tick 和 `CameraState` 驱动；
- `Timer3`：连拍间隔计时，现由 `std::chrono::steady_clock` 驱动；
- `OpenDialog1`：旧版只用于加载 `.drk`，现由右侧 `Load dark...` 打开跨平台 ImGui 文件选择器。

## 自动构建 Release ZIP

GitHub Actions 的 `Windows Release` 工作流会在 push、pull request 和手动触发时构建 Windows x64 Release，运行核心测试，并上传 `cam86-view-windows-x64-<commit>.zip`。在仓库 Actions 页面打开成功的运行，从 Artifacts 下载 ZIP（保留 30 天）。

ZIP 包含 EXE、运行说明及许可证。构建静态链接 Microsoft C++ 运行库；真机仍需安装 FTDI D2XX 驱动，程序使用系统的 64 位 DLL。

本地已完成 Release 构建时，也可打包：

```powershell
.github/scripts/package-windows.ps1 -Version 0.2.0
```

生成的 ZIP 位于 `dist/`。
