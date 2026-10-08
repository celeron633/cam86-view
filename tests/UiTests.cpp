#include "app/AppState.hpp"
#include "ui/MainWindow.hpp"
#include "ui/ImagePanel.hpp"
#include <QApplication>
#include <QComboBox>
#include <QButtonGroup>
#include <QAbstractButton>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QMouseEvent>
#include <QPushButton>
#include <QTemporaryDir>
#include <QThread>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <functional>
#include <stdexcept>

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
bool waitFor(const std::function<bool()>& predicate, int timeout = 10000) {
    QElapsedTimer timer;
    timer.start();
    while (!predicate() && timer.elapsed() < timeout) {
        QApplication::processEvents();
        QThread::msleep(10);
    }
    return predicate();
}
}
int main(int argc, char** argv) {
    QApplication app(argc, argv);
    try {
        cam86::AppState state;
        cam86::ui::MainWindow window(state);
        window.setAttribute(Qt::WA_DontShowOnScreen);
        window.show();
        QApplication::processEvents();
        auto* connect = window.findChild<QPushButton*>("connectButton");
        auto* capture = window.findChild<QPushButton*>("captureButton");
        auto* stop = window.findChild<QPushButton*>("stopButton");
        auto* continuous = window.findChild<QPushButton*>("continuousButton");
        require(connect && capture && stop && continuous, "Capture controls missing");
        require(!capture->isEnabled(), "Capture must be disabled before connection");
        connect->click();
        require(state.camera.isConnected() && capture->isEnabled(), "Demo connection failed");
        capture->click();
        require(!capture->isEnabled(), "Capture must be disabled while busy");
        require(waitFor([&] { return state.frameNumber == 1; }), "Demo frame not processed");
        require(state.image.hasImage() && state.image.previewRgba().size() == 900 * 600 * 4,
            "Missing image preview");
        auto* image = dynamic_cast<cam86::ui::ImagePanel*>(window.findChild<QWidget*>("mainImage"));
        require(image != nullptr, "Main image widget missing");
        const QSize fitted = QSize(900,600).scaled(image->size(), Qt::KeepAspectRatio);
        QPointF point((image->width() - fitted.width()) / 2.0 + fitted.width() * 0.25,
                      fitted.height() * 0.25);
        QMouseEvent click(QEvent::MouseButtonPress, point, image->mapToGlobal(point),
            Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(image, &click);
        require(std::abs(state.image.selectionX() - 750) < 5 &&
                std::abs(state.image.selectionY() - 500) < 5, "Image selection mapping failed");
        const auto revision = state.image.revision();
        auto* iso = window.findChild<QButtonGroup*>("isoGroup");
        require(iso && iso->buttons().size() == 10 && iso->checkedId() == 9,
            "ISO radio group missing or incorrect default");
        iso->button(8)->click();
        require(iso->checkedId() == 8 && !iso->button(9)->isChecked(), "ISO options are not exclusive");
        require(state.image.revision() > revision, "ISO change did not rebuild image");
        QTemporaryDir directory;
        require(directory.isValid(), "Temporary output directory unavailable");
        const auto prefix = (directory.path() + "/capture").toUtf8();
        std::memcpy(state.fileName.data(), prefix.constData(), std::size_t(prefix.size() + 1));
        window.findChild<QPushButton*>("writeFitsButton")->click();
        require(QFileInfo::exists(directory.path() + "/capture1.fit"), "Manual FITS write failed");
        state.writeFits = true;
        state.infiniteFrames = false;
        state.continuousFrames = 2;
        state.delaySeconds = 0;
        continuous->click();
        require(waitFor([&] { return !state.continuous && state.frameNumber == 2; }),
            "Finite sequence did not finish");
        require(QFileInfo::exists(directory.path() + "/capture0.fit"), "Automatic FITS write failed");
        state.writeFits = false;
        state.infiniteFrames = true;
        state.delaySeconds = 30;
        continuous->click();
        require(waitFor([&] { return state.frameNumber == 1 && !state.busy(); }),
            "Continuous frame failed");
        require(stop->isEnabled(), "Stop must be enabled during sequence delay");
        stop->click();
        require(!state.continuous, "Stop did not cancel sequence during delay");
        auto* darkMode = window.findChild<QButtonGroup*>("darkModeGroup");
        require(darkMode && darkMode->buttons().size() == 3 && darkMode->checkedId() == 0,
            "Dark mode radio group missing or incorrect default");
        darkMode->button(1)->click();
        capture->click();
        require(waitFor([&] { return state.image.darkFrameCount() == 1; }), "Dark accumulation failed");
        darkMode->button(1)->click();
        require(state.image.darkFrameCount() == 1, "Clicking selected dark mode discarded dark frames");
        window.findChild<QPushButton*>("saveDarkButton")->click();
        require(QFileInfo::exists(directory.path() + "/capture.drk"), "Dark save failed");
        darkMode->button(2)->click();
        capture->click();
        const int beforeSubtract = state.frameNumber;
        require(waitFor([&] { return state.frameNumber > beforeSubtract; }), "Dark subtraction capture failed");
        darkMode->button(0)->click();
        state.exposureIndex = 29; // 60 seconds: stop must cancel without waiting for exposure.
        capture->click();
        require(state.busy(), "Long exposure did not start");
        stop->click();
        require(waitFor([&] { return !state.busy(); }), "Stop failed to cancel exposure");
        window.tick();
        iso->button(9)->click();
        require(iso->checkedId() == 9 && darkMode->checkedId() == 0,
            "Radio selection did not return to auto ISO and normal mode");
        window.tick();
        // Allow the native widget selection animations to settle before visual review.
        QElapsedTimer settle;
        settle.start();
        while (settle.elapsed() < 250) {
            QApplication::processEvents();
            QThread::msleep(10);
        }
        require(window.grab().save("qt-ui-preview.png"), "UI preview export failed");
        window.resize(960, 600);
        QApplication::processEvents();
        require(window.grab().save("qt-ui-preview-small.png"), "Small UI preview export failed");
        connect->click();
        require(!state.camera.isConnected() && !capture->isEnabled(), "Disconnect failed");
        std::puts("Qt UI tests passed");
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "Qt UI test failed: %s\n", error.what());
        return 1;
    }
}
