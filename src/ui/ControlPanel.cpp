#include "ui/ControlPanel.hpp"
#include "cam86/image/FitsWriter.hpp"
#include <QCheckBox>
#include <QComboBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>
#include <algorithm>
#include <cstring>
#include <exception>
#include <filesystem>

namespace cam86::ui {
namespace {
std::filesystem::path filePath(const QString& text) {
#ifdef _WIN32
    return std::filesystem::path(text.toStdWString());
#else
    return std::filesystem::path(text.toStdString());
#endif
}
std::filesystem::path outputPrefix(const AppState& state) {
    return filePath(QString::fromUtf8(state.fileName.data()));
}
}
ControlPanel::ControlPanel(AppState& state, QWidget* parent) : QWidget(parent), state_(state) {
    auto* layout = new QVBoxLayout(this);
    const auto button = [this](const QString& title, auto callback) {
        auto* control = new QPushButton(title);
        connect(control, &QPushButton::clicked, this, callback);
        return control;
    };
    const auto check = [this](const QString& title, bool& value) {
        auto* control = new QCheckBox(title);
        control->setChecked(value);
        connect(control, &QCheckBox::toggled, this, [&value](bool v) { value = v; });
        return control;
    };
    const auto spin = [this](int min, int max, int& value) {
        auto* control = new QSpinBox;
        control->setRange(min, max);
        control->setValue(value);
        connect(control, &QSpinBox::valueChanged, this, [&value](int v) { value = v; });
        return control;
    };
    const auto section = [layout](const QString& title) {
        auto* box = new QGroupBox(title);
        auto* form = new QFormLayout(box);
        layout->addWidget(box);
        return form;
    };
    backend_ = new QComboBox;
    backend_->addItem("Demo camera");
    backend_->addItem(hardwareBackendName());
    backend_->setCurrentIndex(state.simulation ? 0 : 1);
    connect(backend_, &QComboBox::currentIndexChanged, this, [this](int index) {
        state_.simulation = index == 0;
        state_.camera.useSimulation(state_.simulation);
    });
    layout->addWidget(backend_);
    connection_ = button("Connect", [this] {
        if (state_.camera.isConnected()) {
            state_.continuous = false; state_.camera.disconnect();
        } else if (state_.camera.connect()) {
            state_.camera.setGain(state_.gain);
            state_.camera.setOffset(state_.offset);
            state_.camera.setTargetTemperature(state_.targetTemperature);
            state_.camera.setCooling(state_.cooling);
        }
        refresh();
    });
    connection_->setObjectName("connectButton");
    layout->addWidget(connection_);
    auto* capture = section("Capture");
    auto* options = new QWidget;
    auto* optionsLayout = new QHBoxLayout(options);
    optionsLayout->setContentsMargins(0,0,0,0);
    optionsLayout->addWidget(check("Bin 2 x 2", state.bin2x2));
    optionsLayout->addWidget(check("ROI", state.roi));
    capture->addRow(options);
    auto* exposure = new QComboBox;
    for (const auto& choice : kExposureChoices) exposure->addItem(choice.label);
    exposure->setCurrentIndex(state.exposureIndex);
    connect(exposure, &QComboBox::currentIndexChanged, this, [this](int i) { state_.exposureIndex = i; });
    capture->addRow("Exposure", exposure);
    frames_ = spin(1, 1000000, state.continuousFrames);
    capture->addRow("Frames", frames_);
    capture->addRow("Delay (s)", spin(0, 86400, state.delaySeconds));
    capture->addRow(check("Infinite capture", state.infiniteFrames));
    continuous_ = button("Continuous", [this] {
        state_.continuous = !state_.continuous;
        if (state_.continuous) {
            state_.frameNumber = 0;
            state_.nextCapture = std::chrono::steady_clock::now();
        } else state_.camera.stopCapture();
        refresh();
    });
    continuous_->setObjectName("continuousButton");
    capture->addRow(continuous_);
    auto* actions = new QWidget;
    auto* actionsLayout = new QHBoxLayout(actions);
    actionsLayout->setContentsMargins(0,0,0,0);
    capture_ = button("Get image", [this] { startOne(); refresh(); });
    capture_->setObjectName("captureButton");
    stop_ = button("Stop", [this] {
        state_.continuous = false; state_.camera.stopCapture(); refresh();
    });
    stop_->setObjectName("stopButton");
    actionsLayout->addWidget(capture_);
    actionsLayout->addWidget(stop_);
    capture->addRow(actions);
    progress_ = new QProgressBar;
    progress_->setRange(0, 1000);
    capture->addRow(progress_);
    auto* files = section("Display and files");
    auto* iso = new QComboBox;
    for (auto choice : kIsoChoices) iso->addItem(choice);
    iso->setCurrentIndex(state.isoIndex);
    iso->setObjectName("isoCombo");
    connect(iso, &QComboBox::currentIndexChanged, this, [this](int i) {
        state_.isoIndex = i;
        if (state_.image.hasImage()) state_.image.rebuild(state_.bin2x2, state_.isoShift());
    });
    files->addRow("ISO", iso);
    files->addRow(check("Information", state.information));
    auto* prefix = new QLineEdit(QString::fromUtf8(state.fileName.data()));
    prefix->setMaxLength(255);
    connect(prefix, &QLineEdit::textChanged, this, [this](const QString& text) {
        const auto bytes = text.toUtf8();
        // Retain complete UTF-8 code points in the fixed-size legacy config field.
        int count = std::min(int(bytes.size()), int(state_.fileName.size() - 1));
        while (count > 0 && count < bytes.size() && (static_cast<unsigned char>(bytes[count]) & 0xc0) == 0x80) --count;
        std::memcpy(state_.fileName.data(), bytes.constData(), count);
        state_.fileName[static_cast<std::size_t>(count)] = '\0';
    });
    files->addRow("Output prefix", prefix);
    auto* autoFits = check("Write FITS automatically", state.writeFits);
    connect(autoFits, &QCheckBox::toggled, this, [this](bool v) { if (v) state_.frameNumber = 0; });
    files->addRow(autoFits);
    auto* write = button("Write FITS", [this] { writeCurrentFits(); });
    write->setObjectName("writeFitsButton");
    files->addRow(write);
    auto* mode = new QComboBox;
    mode->setObjectName("darkModeCombo");
    mode->addItems({"Normal", "Accumulate dark", "Subtract dark"});
    mode->setCurrentIndex(static_cast<int>(state.darkMode));
    connect(mode, &QComboBox::currentIndexChanged, this, [this](int i) {
        state_.darkMode = static_cast<DarkMode>(i);
        if (state_.darkMode == DarkMode::Accumulate) state_.image.clearDark();
    });
    files->addRow("Mode", mode);
    auto* saveDark = button("Save dark", [this] {
        try { state_.image.saveDark(outputPrefix(state_)); state_.addLog("Dark file recorded"); }
        catch (const std::exception& e) { state_.addLog(e.what()); }
    });
    saveDark->setObjectName("saveDarkButton");
    files->addRow(saveDark);
    files->addRow(button("Load dark...", [this] {
        const auto selected = QFileDialog::getOpenFileName(this, "Load dark frame",
            QString::fromUtf8(state_.fileName.data()), "Dark frames (*.drk)");
        if (selected.isEmpty()) return;
        try {
            state_.image.loadDark(filePath(selected));
            state_.addLog("Dark file loaded: " + selected.toStdString() + " (" +
                std::to_string(state_.image.darkFrameCount()) + " frames)");
        } catch (const std::exception& e) { state_.addLog(e.what()); }
    }));
    files->addRow(button("View dark", [this] {
        try { state_.image.viewDark(); state_.image.rebuild(state_.bin2x2, state_.isoShift()); }
        catch (const std::exception& e) { state_.addLog(e.what()); }
    }));
    auto* temperature = section("Temperature");
    temperature_ = new QLabel;
    temperature->addRow("Sensor", temperature_);
    temperature->addRow(button("Read temperature", [this] { state_.camera.requestTemperature(); }));
    auto* target = spin(-30, 26, state.targetTemperature);
    target->setSuffix(" C");
    connect(target, &QSpinBox::valueChanged, this, [this](int v) { state_.camera.setTargetTemperature(v); });
    temperature->addRow("Setpoint", target);
    auto* cooling = check("Cooling", state.cooling);
    connect(cooling, &QCheckBox::toggled, this, [this](bool v) { state_.camera.setCooling(v); });
    temperature->addRow(cooling);
    layout->addStretch();
    refresh();
}
void ControlPanel::refresh() {
    const bool connected = state_.camera.isConnected();
    backend_->setEnabled(!connected && !state_.busy());
    connection_->setText(connected ? "Disconnect" : "Connect");
    capture_->setEnabled(connected && !state_.busy() && !state_.continuous);
    stop_->setEnabled(state_.busy() || state_.continuous);
    continuous_->setEnabled(connected);
    continuous_->setText(state_.continuous ? "Stop continuous" : "Continuous");
    frames_->setEnabled(!state_.infiniteFrames);
    progress_->setValue(int(state_.camera.progress() * 1000));
    temperature_->setText(QString::number(state_.sensorTemperature, 'f', 1) + " C");
}
void ControlPanel::startOne() {
    ExposureRequest request;
    request.bin2x2 = state_.bin2x2;
    request.roi = state_.roi;
    request.roiCenterY = state_.image.selectionY();
    request.seconds = state_.exposureSeconds();
    state_.camera.startCapture(request);
}
void ControlPanel::writeCurrentFits() {
    if (!state_.image.hasImage()) { state_.addLog("No image to write"); return; }
    try {
        auto path = outputPrefix(state_);
        path += std::to_string(state_.frameNumber) + ".fit";
        writeFits(path, state_.image.frame(), state_.image.blackLevel(), state_.image.whiteLevel());
        state_.addLog("FITS file recorded");
    } catch (const std::exception& e) { state_.addLog(e.what()); }
}
}
