#include "ui/ControlPanel.hpp"
#include "cam86/image/FitsWriter.hpp"
#include <QCheckBox>
#include <QButtonGroup>
#include <QComboBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QRadioButton>
#include <QGridLayout>
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
    layout->setContentsMargins(5, 5, 5, 5);
    layout->setSpacing(4);
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
        form->setContentsMargins(6, 5, 6, 6);
        form->setHorizontalSpacing(6);
        form->setVerticalSpacing(3);
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
    auto* sequence = new QWidget;
    auto* sequenceLayout = new QHBoxLayout(sequence);
    sequenceLayout->setContentsMargins(0, 0, 0, 0);
    sequenceLayout->setSpacing(6);
    sequenceLayout->addWidget(frames_, 1);
    auto* infinite = check("Infinite", state.infiniteFrames);
    infinite->setToolTip("Keep capturing until stopped, without a frame limit");
    sequenceLayout->addWidget(infinite);
    capture->addRow("Frames", sequence);
    capture->addRow("Delay (s)", spin(0, 86400, state.delaySeconds));
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
    auto* display = new QWidget;
    auto* displayLayout = new QHBoxLayout(display);
    displayLayout->setContentsMargins(0, 0, 0, 0);
    displayLayout->setSpacing(6);
    auto* isoBox = new QGroupBox("ISO");
    auto* isoLayout = new QVBoxLayout(isoBox);
    isoLayout->setContentsMargins(5, 4, 5, 5);
    isoLayout->setSpacing(0);
    auto* iso = new QButtonGroup(this);
    iso->setObjectName("isoGroup");
    for (int i = 0; i < static_cast<int>(kIsoChoices.size()); ++i) {
        auto* radio = new QRadioButton(kIsoChoices[static_cast<std::size_t>(i)], isoBox);
        radio->setObjectName(QString("isoRadio%1").arg(i));
        iso->addButton(radio, i);
        radio->setChecked(state.isoIndex == i);
        isoLayout->addWidget(radio);
    }
    isoLayout->addStretch();
    connect(iso, &QButtonGroup::idClicked, this, [this](int i) {
        state_.isoIndex = i;
        if (state_.image.hasImage()) state_.image.rebuild(state_.bin2x2, state_.isoShift());
    });
    displayLayout->addWidget(isoBox);
    auto* fileOptions = new QWidget;
    auto* fileLayout = new QVBoxLayout(fileOptions);
    fileLayout->setContentsMargins(0, 0, 0, 0);
    fileLayout->setSpacing(3);
    fileLayout->addWidget(check("Information", state.information));
    fileLayout->addWidget(new QLabel("Output prefix"));
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
    prefix->setMinimumWidth(0);
    fileLayout->addWidget(prefix);
    auto* autoFits = check("Write FITS", state.writeFits);
    autoFits->setToolTip("Automatically save each captured frame as FITS");
    connect(autoFits, &QCheckBox::toggled, this, [this](bool v) { if (v) state_.frameNumber = 0; });
    fileLayout->addWidget(autoFits);
    auto* write = button("Write FITS", [this] { writeCurrentFits(); });
    write->setObjectName("writeFitsButton");
    fileLayout->addWidget(write);
    auto* modeBox = new QGroupBox("Mode");
    auto* modeLayout = new QVBoxLayout(modeBox);
    modeLayout->setContentsMargins(5, 4, 5, 5);
    modeLayout->setSpacing(2);
    auto* mode = new QButtonGroup(this);
    mode->setObjectName("darkModeGroup");
    const std::array<const char*, 3> modeLabels{"Normal", "Dark", "Subdark"};
    const std::array<const char*, 3> modeTips{"Normal capture", "Accumulate dark frames", "Subtract the dark frame"};
    for (int i = 0; i < static_cast<int>(modeLabels.size()); ++i) {
        auto* radio = new QRadioButton(modeLabels[static_cast<std::size_t>(i)], modeBox);
        radio->setObjectName(QString("darkModeRadio%1").arg(i));
        radio->setToolTip(modeTips[static_cast<std::size_t>(i)]);
        mode->addButton(radio, i);
        radio->setChecked(static_cast<int>(state.darkMode) == i);
        modeLayout->addWidget(radio);
    }
    connect(mode, &QButtonGroup::idClicked, this, [this](int i) {
        const auto selected = static_cast<DarkMode>(i);
        if (selected == state_.darkMode) return;
        state_.darkMode = selected;
        if (selected == DarkMode::Accumulate) state_.image.clearDark();
    });
    fileLayout->addWidget(modeBox);
    fileLayout->addStretch();
    displayLayout->addWidget(fileOptions, 1);
    files->addRow(display);
    auto* darkActions = new QWidget;
    auto* darkLayout = new QGridLayout(darkActions);
    darkLayout->setContentsMargins(0, 0, 0, 0);
    darkLayout->setSpacing(3);
    auto* saveDark = button("Save dark", [this] {
        try { state_.image.saveDark(outputPrefix(state_)); state_.addLog("Dark file recorded"); }
        catch (const std::exception& e) { state_.addLog(e.what()); }
    });
    saveDark->setObjectName("saveDarkButton");
    darkLayout->addWidget(saveDark, 0, 0);
    darkLayout->addWidget(button("Load dark...", [this] {
        const auto selected = QFileDialog::getOpenFileName(this, "Load dark frame",
            QString::fromUtf8(state_.fileName.data()), "Dark frames (*.drk)");
        if (selected.isEmpty()) return;
        try {
            state_.image.loadDark(filePath(selected));
            state_.addLog("Dark file loaded: " + selected.toStdString() + " (" +
                std::to_string(state_.image.darkFrameCount()) + " frames)");
        } catch (const std::exception& e) { state_.addLog(e.what()); }
    }), 0, 1);
    darkLayout->addWidget(button("View dark", [this] {
        try { state_.image.viewDark(); state_.image.rebuild(state_.bin2x2, state_.isoShift()); }
        catch (const std::exception& e) { state_.addLog(e.what()); }
    }), 1, 0);
    files->addRow(darkActions);
    auto* temperature = section("Temperature");
    temperature_ = new QLabel;
    auto* sensor = new QWidget;
    auto* sensorLayout = new QHBoxLayout(sensor);
    sensorLayout->setContentsMargins(0, 0, 0, 0);
    sensorLayout->addWidget(temperature_, 1);
    auto* readTemperature = button("Read", [this] { state_.camera.requestTemperature(); });
    readTemperature->setToolTip("Read sensor temperature");
    sensorLayout->addWidget(readTemperature);
    temperature->addRow("Sensor", sensor);
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
