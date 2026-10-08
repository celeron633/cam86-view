#include "ui/MainWindow.hpp"

#include "cam86/image/FitsWriter.hpp"

#include "ui/ControlPanel.hpp"
#include "ui/ImagePanel.hpp"
#include "ui/LogPanel.hpp"
#include <QFormLayout>
#include <QFile>
#include <QGroupBox>
#include <QScrollArea>
#include <QScrollBar>
#include <QSlider>
#include <QLabel>
#include <QSplitter>
#include <QStatusBar>
#include <QTimer>
#include <QVBoxLayout>
#include <stdexcept>

static void initializeThemeResources() {
    Q_INIT_RESOURCE(theme);
}

#include <algorithm>
#include <chrono>
#include <exception>
#include <filesystem>
#include <iomanip>
#include <sstream>
#include <string>
#include <utility>

namespace cam86::ui {

MainWindow::MainWindow(AppState& state) : state_(state) {
    setWindowTitle("CAM86-View v0.2");
    initializeThemeResources();
    QFile stylesheet(":/ui/theme.qss");
    if (!stylesheet.open(QIODevice::ReadOnly)) throw std::runtime_error("Could not load the interface theme");
    setStyleSheet(QString::fromUtf8(stylesheet.readAll()));
    auto colors = palette();
    colors.setColor(QPalette::Window, QColor("#eef1f5"));
    colors.setColor(QPalette::WindowText, QColor("#283548"));
    setPalette(colors);
    resize(1180, 720);
    setMinimumSize(960, 600);
    auto* columns = new QSplitter(Qt::Horizontal, this);
    columns->setChildrenCollapsible(false);
    columns->setHandleWidth(5);
    setCentralWidget(columns);
    auto* left = new QWidget;
    left->setObjectName("imageColumn");
    auto* leftLayout = new QVBoxLayout(left);
    leftLayout->setContentsMargins(6, 6, 6, 6);
    leftLayout->setSpacing(6);
    mainImage_ = new ImagePanel(state, ImagePanel::View::Main);
    mainImage_->setObjectName("mainImage");
    leftLayout->addWidget(mainImage_, 1);
    auto* adjustments = new QFormLayout;
    const auto slider = [this, adjustments](const QString& text, int min, int max, int& value, auto setter) {
        auto* row = new QWidget;
        auto* layout = new QHBoxLayout(row);
        layout->setContentsMargins(0,0,0,0);
        auto* control = new QSlider(Qt::Horizontal);
        control->setRange(min, max);
        control->setValue(value);
        auto* label = new QLabel(QString::number(value));
        label->setMinimumWidth(32);
        layout->addWidget(control, 1);
        layout->addWidget(label);
        connect(control, &QSlider::valueChanged, this, [this, &value, label, setter](int v) {
            value = v; label->setNum(v); (state_.camera.*setter)(v);
        });
        adjustments->addRow(text, row);
        return control;
    };
    gain_ = slider("Gain", 0, 63, state.gain, &CameraController::setGain);
    offset_ = slider("Offset", -127, 127, state.offset, &CameraController::setOffset);
    leftLayout->addLayout(adjustments);
    columns->addWidget(left);
    auto* middle = new QSplitter(Qt::Vertical);
    middle->setChildrenCollapsible(false);
    middle->setHandleWidth(4);
    const auto group = [middle](const QString& title, QWidget* widget) {
        auto* box = new QGroupBox(title);
        auto* layout = new QVBoxLayout(box);
        layout->setContentsMargins(5, 5, 5, 5);
        layout->addWidget(widget);
        middle->addWidget(box);
    };
    log_ = new LogPanel(state);
    crop_ = new ImagePanel(state, ImagePanel::View::Crop);
    histogram_ = new ImagePanel(state, ImagePanel::View::Histogram);
    group("Log", log_);
    group("Selection - 50 x 50", crop_);
    group("RGB histogram (log scale)", histogram_);
    middle->setSizes({255,260,150});
    columns->addWidget(middle);
    controls_ = new ControlPanel(state);
    auto* scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setWidget(controls_);
    columns->addWidget(scroll);
    controls_->ensurePolished();
    scroll->setMinimumWidth(std::max(250, controls_->minimumSizeHint().width() +
        scroll->verticalScrollBar()->sizeHint().width()));
    columns->setSizes({660,235,275});
    columns->setStretchFactor(0, 1);
    columns->setStretchFactor(1, 0);
    columns->setStretchFactor(2, 0);
    auto* timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, [this] { tick(); });
    timer->start(50);
    tick();
}

void MainWindow::tick() {
    for (auto& message : state_.camera.takeMessages()) state_.addLog(std::move(message));
    if (auto temperature = state_.camera.takeTemperature()) state_.sensorTemperature = *temperature;
    if (auto frame = state_.camera.takeFrame()) processCompletedFrame(std::move(*frame));

    const auto now = std::chrono::steady_clock::now();
    if (state_.camera.isConnected() && !state_.busy() && now >= nextTemperatureRead_) {
        state_.camera.requestTemperature();
        nextTemperatureRead_ = now + std::chrono::seconds(2);
    }

    if (state_.continuous && (!state_.camera.isConnected() || state_.camera.state() == CameraState::Error)) {
        state_.continuous = false;
        state_.addLog("Continuous capture stopped: camera unavailable");
    }
    const bool belowLimit = state_.infiniteFrames || state_.frameNumber < state_.continuousFrames;
    if (state_.continuous && state_.camera.isConnected() && !state_.busy() &&
        belowLimit && now >= state_.nextCapture) {
        ExposureRequest request;
        request.bin2x2 = state_.bin2x2;
        request.roi = state_.roi;
        request.roiCenterY = state_.image.selectionY();
        request.seconds = state_.exposureSeconds();
        state_.camera.startCapture(request);
    } else if (state_.continuous && !state_.infiniteFrames && !belowLimit) {
        state_.continuous = false;
        state_.addLog("Continuous capture complete");
    }
    controls_->refresh();
    gain_->setEnabled(state_.camera.isConnected());
    offset_->setEnabled(state_.camera.isConnected());
    mainImage_->refresh(); crop_->refresh(); histogram_->refresh(); log_->refresh();
    statusBar()->showMessage(QString("%1 | Frames: %2 | Sensor: %3 C")
        .arg(state_.camera.isConnected() ? (state_.busy() ? "Capturing" : "Ready") : "Disconnected")
        .arg(state_.frameNumber).arg(state_.sensorTemperature, 0, 'f', 1));
}

void MainWindow::processCompletedFrame(Frame frame) {
    try {
        state_.sensorTemperature = frame.sensorTemperature;
        state_.image.accept(std::move(frame), state_.darkMode);
        state_.image.rebuild(state_.bin2x2, state_.isoShift());
        if (state_.writeFits) {
            const auto filename = QString::fromUtf8(state_.fileName.data()) + QString::number(state_.frameNumber) + ".fit";
#ifdef _WIN32
            const auto path = std::filesystem::path(filename.toStdWString());
#else
            const auto path = std::filesystem::path(filename.toStdString());
#endif
            writeFits(path, state_.image.frame(), state_.image.blackLevel(), state_.image.whiteLevel());
            state_.addLog("File " + filename.toStdString() + " is recorded");
        }
        if (state_.information) appendStatistics();
        ++state_.frameNumber;
        state_.nextCapture = std::chrono::steady_clock::now() + std::chrono::seconds(state_.delaySeconds);
    } catch (const std::exception& error) {
        state_.addLog(std::string("Frame processing failed: ") + error.what());
        state_.continuous = false;
    }
}

void MainWindow::appendStatistics() {
    const auto stats = state_.image.statistics();
    std::ostringstream text;
    text << std::fixed << std::setprecision(2)
         << "StdDev (frame) = " << stats.standardDeviation
         << ", StdDev (line) = " << stats.lineStandardDeviation;
    state_.addLog(text.str());
    text.str({}); text.clear();
    text << "MinValue = " << stats.minimum << ", MaxValue = " << stats.maximum
         << ", Mean = " << std::fixed << std::setprecision(2) << stats.mean;
    state_.addLog(text.str());
    text.str({}); text.clear();
    text << "Mean R/G/B = " << std::fixed << std::setprecision(2)
         << stats.redMean << " / " << stats.greenMean << " / " << stats.blueMean;
    state_.addLog(text.str());
}

} // namespace cam86::ui
