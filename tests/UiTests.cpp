#include "app/AppState.hpp"
#include "ui/MainWindow.hpp"
#include "ui/ImagePanel.hpp"
#include "ui/HistogramDialog.hpp"
#include "ui/SelectionDialog.hpp"
#include <QCheckBox>
#include <QScrollArea>
#include <QScrollBar>
#include <QApplication>
#include <QComboBox>
#include <QButtonGroup>
#include <QAbstractButton>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QMouseEvent>
#include <QLabel>
#include <QPushButton>
#include <QTemporaryDir>
#include <QThread>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <functional>
#include <numeric>
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
        auto* histogram = window.findChild<QWidget*>("histogramPreview");
        require(histogram != nullptr, "Histogram preview missing");
        const QPointF histogramPoint(histogram->rect().center());
        const auto openHistogram = [&] {
            QMouseEvent event(QEvent::MouseButtonPress, histogramPoint, histogram->mapToGlobal(histogramPoint),
                Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(histogram, &event);
            QApplication::processEvents();
        };
        openHistogram();
        auto* dialog = dynamic_cast<cam86::ui::HistogramDialog*>(window.findChild<QWidget*>("histogramDialog"));
        require(dialog && dialog->isVisible() && !dialog->isModal(), "Detailed histogram did not open as a modeless window");
        require(dialog->findChild<QLabel*>("histogramSource")->text().contains("No image"),
            "Histogram empty state missing");
        dialog->close();
        openHistogram();
        require(window.findChildren<QWidget*>("histogramDialog").size() == 1 && dialog->isVisible(),
            "Repeated histogram clicks must reuse the window");
        auto* selection = window.findChild<QWidget*>("selectionPreview");
        require(selection != nullptr, "Selection preview missing");
        const auto openSelection = [&] {
            const QPointF position(selection->rect().center());
            QMouseEvent event(QEvent::MouseButtonPress, position, selection->mapToGlobal(position),
                Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(selection, &event);
            QApplication::processEvents();
        };
        openSelection();
        auto* inspector = dynamic_cast<cam86::ui::SelectionDialog*>(window.findChild<QWidget*>("selectionDialog"));
        require(inspector && inspector->isVisible() && !inspector->isModal(), "Pixel inspector did not open");
        require(inspector->findChild<QLabel*>("selectionSource")->text().contains("No image"), "Pixel inspector empty state missing");
        inspector->close();
        openSelection();
        require(window.findChildren<QWidget*>("selectionDialog").size() == 1, "Pixel inspector window not reused");
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
        window.tick();
        require(!inspector->findChild<QLabel*>("selectionSource")->text().contains("No image"),
            "Pixel inspector did not refresh after capture");
        require(dialog->findChild<QLabel*>("histogramSource")->text().contains("540000"),
            "Open histogram did not refresh after capture");
        auto* plot = dialog->findChild<QWidget*>("rgbHistogramPlot");
        auto* levelLabel = dialog->findChild<QLabel*>("histogramLevel");
        const auto moveToLevel = [&](QWidget* graph, int level) {
            const QPointF position(52 + level / 255.0 * (graph->width() - 68), 50);
            QMouseEvent move(QEvent::MouseMove, position, graph->mapToGlobal(position),
                Qt::NoButton, Qt::NoButton, Qt::NoModifier);
            QApplication::sendEvent(graph, &move);
        };
        for (const int level : {0, 128, 255}) {
            moveToLevel(plot, level);
            require(levelLabel->text() == QString("Level: %1 / 255").arg(level), "Histogram level mapping failed");
            const auto count = state.image.histogramR()[level];
            const auto reading = dialog->findChild<QLabel*>("redHistogramReading")->text();
            require(reading.startsWith(QString("Count %1   ").arg(count)), "Histogram hover count incorrect");
            if (level == 255) require(reading.contains("Cumulative 100.00%"), "Histogram cumulative percentage incorrect");
        }
        moveToLevel(dialog->findChild<QWidget*>("blueHistogramPlot"), 64);
        require(levelLabel->text() == "Level: 64 / 255", "Individual histogram tracking failed");
        auto* histogramScale = dialog->findChild<QComboBox*>("histogramScale");
        histogramScale->setCurrentIndex(1);
        require(dialog->grab().save("qt-histogram-linear.png"), "Linear histogram preview export failed");
        histogramScale->setCurrentIndex(0);
        moveToLevel(plot, 128);
        require(dialog->grab().save("qt-histogram-preview.png"), "Histogram preview export failed");
        QEvent leave(QEvent::Leave);
        QApplication::sendEvent(plot, &leave);
        require(dialog->findChild<QLabel*>("redHistogramReading")->text().contains("Count --"),
            "Histogram tracking did not clear when the mouse left");
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
        window.tick();
        auto* pixelView = inspector->findChild<QWidget*>("selectionPixelView");
        auto* pixelPosition = inspector->findChild<QLabel*>("selectionPosition");
        auto* pixelValues = inspector->findChild<QLabel*>("selectionValues");
        auto* pixelZoom = inspector->findChild<QComboBox*>("selectionZoom");
        const auto inspectPixel = [&](int x, int y, int scale) {
            const QPointF position((pixelView->width() - 50 * scale) / 2 + (x + 0.5) * scale,
                                   (pixelView->height() - 50 * scale) / 2 + (y + 0.5) * scale);
            QMouseEvent move(QEvent::MouseMove, position, pixelView->mapToGlobal(position),
                Qt::NoButton, Qt::NoButton, Qt::NoModifier);
            QApplication::sendEvent(pixelView, &move);
            const int originX = std::clamp(state.image.selectionX() - 25, 0, cam86::kSensorWidth - 50);
            const int originY = std::clamp(state.image.selectionY() - 25, 0, cam86::kSensorHeight - 50);
            require(pixelPosition->text() == QString("Crop: (%1, %2)   |   Sensor: (%3, %4)")
                .arg(x).arg(y).arg(originX + x).arg(originY + y), "Pixel coordinate mapping failed");
            const auto rgba = state.image.cropRgba();
            const auto offset = (y * 50 + x) * 4;
            const auto raw = state.image.frame().pixels[std::size_t(originY + y) * cam86::kSensorWidth + originX + x];
            require(pixelValues->text().startsWith(QString("R: %1   G: %2   B: %3   |")
                .arg(rgba[offset]).arg(rgba[offset + 1]).arg(rgba[offset + 2])) &&
                pixelValues->text().endsWith(QString("Raw: %1").arg(raw)), "Pixel RGB or raw reading incorrect");
        };
        inspectPixel(0, 0, (std::min(pixelView->width(), pixelView->height()) - 32) / 50);
        pixelZoom->setCurrentIndex(1); // 8x, with an exact integer scale.
        QApplication::processEvents();
        inspectPixel(49, 49, 8);
        state.image.select(0, 0);
        window.tick();
        inspectPixel(0, 0, 8);
        state.image.select(1, 1);
        window.tick();
        inspectPixel(49, 49, 8);
        state.image.select(0.25f, 0.25f);
        window.tick();
        pixelZoom->setCurrentIndex(4); // 24x must be scrollable.
        QApplication::processEvents();
        auto* pixelScroll = inspector->findChild<QScrollArea*>("selectionScroll");
        require(pixelScroll->horizontalScrollBar()->maximum() > 0 && pixelScroll->verticalScrollBar()->maximum() > 0,
            "Large pixel zoom is not scrollable");
        pixelZoom->setCurrentIndex(0);
        QApplication::processEvents();
        inspectPixel(25, 25, (std::min(pixelView->width(), pixelView->height()) - 32) / 50);
        inspector->findChild<QCheckBox*>("selectionGrid")->setChecked(false);
        require(inspector->grab().save("qt-selection-no-grid.png"), "Pixel inspector no-grid export failed");
        inspector->findChild<QCheckBox*>("selectionGrid")->setChecked(true);
        require(inspector->grab().save("qt-selection-preview.png"), "Pixel inspector preview export failed");
        QApplication::sendEvent(pixelView, &leave);
        require(pixelValues->text().contains("R: --"), "Pixel readings did not clear on leave");
        const auto revision = state.image.revision();
        auto* iso = window.findChild<QButtonGroup*>("isoGroup");
        require(iso && iso->buttons().size() == 10 && iso->checkedId() == 9,
            "ISO radio group missing or incorrect default");
        iso->button(8)->click();
        require(iso->checkedId() == 8 && !iso->button(9)->isChecked(), "ISO options are not exclusive");
        require(state.image.revision() > revision, "ISO change did not rebuild image");
        moveToLevel(plot, 128);
        window.tick();
        inspectPixel(25, 25, (std::min(pixelView->width(), pixelView->height()) - 32) / 50);
        inspector->close();
        require(dialog->findChild<QLabel*>("redHistogramReading")->text().startsWith(
            QString("Count %1   ").arg(state.image.histogramR()[128])), "Histogram did not refresh tracked level after ISO change");
        dialog->close();
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
