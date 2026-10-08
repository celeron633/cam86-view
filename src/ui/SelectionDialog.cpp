#include "ui/SelectionDialog.hpp"
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QImage>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QScrollArea>
#include <QVBoxLayout>
#include <algorithm>
#include <functional>

namespace cam86::ui {
namespace {
constexpr int cropSize = 50;
QPoint cropOrigin(const AppState& state) {
    return {std::clamp(state.image.selectionX() - cropSize / 2, 0, kSensorWidth - cropSize),
            std::clamp(state.image.selectionY() - cropSize / 2, 0, kSensorHeight - cropSize)};
}
}

class PixelSelectionView : public QWidget {
public:
    explicit PixelSelectionView(QWidget* parent) : QWidget(parent) {
        setObjectName("selectionPixelView");
        setMouseTracking(true);
        setCursor(Qt::CrossCursor);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        setZoom(0);
    }
    void setZoom(int zoom) {
        zoom_ = zoom;
        const int minimum = zoom ? cropSize * zoom + 32 : 282;
        setMinimumSize(minimum, minimum);
        if (onTrack) onTrack({-1, -1});
        update();
    }
    QRect pixelRect() const {
        const int scale = zoom_ ? zoom_ : std::max(1, (std::min(width(), height()) - 32) / cropSize);
        const int side = scale * cropSize;
        return {(width() - side) / 2, (height() - side) / 2, side, side};
    }
    std::function<void(QPoint)> onTrack;
    QImage image;
    QPoint tracked{-1, -1};
    bool grid = true;
protected:
    void mouseMoveEvent(QMouseEvent* event) override {
        const auto area = pixelRect();
        const auto point = event->position();
        QPoint pixel(-1, -1);
        if (!image.isNull() && point.x() >= area.left() && point.y() >= area.top() &&
            point.x() < area.x() + area.width() && point.y() < area.y() + area.height()) {
            const int scale = area.width() / cropSize;
            pixel = {int((point.x() - area.x()) / scale), int((point.y() - area.y()) / scale)};
        }
        if (onTrack) onTrack(pixel);
    }
    void leaveEvent(QEvent*) override { if (onTrack) onTrack({-1, -1}); }
    void resizeEvent(QResizeEvent*) override { if (onTrack) onTrack({-1, -1}); }
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.fillRect(rect(), QColor("#1c1f23"));
        const auto area = pixelRect();
        if (image.isNull()) {
            p.setPen(QColor("#cbd3df"));
            p.drawText(rect(), Qt::AlignCenter, "No image - capture a frame to inspect pixels");
            return;
        }
        p.setRenderHint(QPainter::SmoothPixmapTransform, false);
        p.drawImage(area, image);
        const int scale = area.width() / cropSize;
        if (grid) {
            p.setPen(QColor(0, 0, 0, 80));
            for (int i = 0; i <= cropSize; ++i) {
                p.drawLine(area.x() + i * scale, area.y(), area.x() + i * scale, area.y() + area.height());
                p.drawLine(area.x(), area.y() + i * scale, area.x() + area.width(), area.y() + i * scale);
            }
        }
        p.setPen(QColor("#9ca9ba"));
        p.drawRect(area.adjusted(-1, -1, 0, 0));
        if (tracked.x() >= 0) {
            const QRect cell(area.x() + tracked.x() * scale, area.y() + tracked.y() * scale, scale, scale);
            p.setPen(QPen(Qt::white, 1, Qt::DashLine));
            p.drawLine(area.left(), cell.center().y(), area.right(), cell.center().y());
            p.drawLine(cell.center().x(), area.top(), cell.center().x(), area.bottom());
            p.setPen(QPen(Qt::black, 3));
            p.drawRect(cell.adjusted(1, 1, -1, -1));
            p.setPen(QPen(Qt::white, 1));
            p.drawRect(cell.adjusted(1, 1, -1, -1));
        }
    }
private:
    int zoom_ = 0;
};

SelectionDialog::SelectionDialog(AppState& state, QWidget* parent) : QDialog(parent), state_(state) {
    setObjectName("selectionDialog");
    setWindowTitle("Selection 50 x 50 - Pixel Inspector - CAM86-View");
    setModal(false);
    resize(740, 780);
    setMinimumSize(650, 540);
    auto* layout = new QVBoxLayout(this);
    source_ = new QLabel;
    source_->setObjectName("selectionSource");
    layout->addWidget(source_);
    auto* toolbar = new QHBoxLayout;
    toolbar->addWidget(new QLabel("Zoom:"));
    auto* zoom = new QComboBox;
    zoom->setObjectName("selectionZoom");
    zoom->addItem("Fit", 0);
    for (int scale : {8, 12, 16, 24}) zoom->addItem(QString("%1x").arg(scale), scale);
    toolbar->addWidget(zoom);
    auto* grid = new QCheckBox("Pixel grid");
    grid->setObjectName("selectionGrid");
    grid->setChecked(true);
    toolbar->addWidget(grid);
    toolbar->addStretch();
    layout->addLayout(toolbar);
    auto* scroll = new QScrollArea;
    scroll->setObjectName("selectionScroll");
    scroll->setWidgetResizable(true);
    pixels_ = new PixelSelectionView(this);
    scroll->setWidget(pixels_);
    layout->addWidget(scroll, 1);
    position_ = new QLabel;
    position_->setObjectName("selectionPosition");
    layout->addWidget(position_);
    auto* readings = new QHBoxLayout;
    swatch_ = new QLabel;
    swatch_->setObjectName("selectionSwatch");
    swatch_->setFixedSize(28, 28);
    readings->addWidget(swatch_);
    values_ = new QLabel;
    values_->setObjectName("selectionValues");
    readings->addWidget(values_, 1);
    layout->addLayout(readings);
    auto* note = new QLabel("Move over a pixel to inspect it. Coordinates start at 0. RGB: displayed 8-bit values.\n"
                           "Raw: 16-bit frame sample after dark processing, before display mapping / Bayer interpolation.");
    note->setWordWrap(true);
    layout->addWidget(note);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
    pixels_->onTrack = [this](QPoint pixel) { trackPixel(pixel); };
    connect(zoom, &QComboBox::currentIndexChanged, this, [this, zoom] { pixels_->setZoom(zoom->currentData().toInt()); });
    connect(grid, &QCheckBox::toggled, this, [this](bool checked) { pixels_->grid = checked; pixels_->update(); });
    refresh();
}

void SelectionDialog::refresh() {
    if (revision_ == state_.image.revision()) return;
    revision_ = state_.image.revision();
    if (state_.image.hasImage()) {
        const auto rgba = state_.image.cropRgba();
        pixels_->image = QImage(rgba.data(), cropSize, cropSize, cropSize * 4, QImage::Format_RGBA8888).copy();
        const auto origin = cropOrigin(state_);
        source_->setText(QString("50 x 50 pixels | Sensor X: %1-%2   Y: %3-%4 | Display RGB (8-bit)")
            .arg(origin.x()).arg(origin.x() + 49).arg(origin.y()).arg(origin.y() + 49));
    } else {
        pixels_->image = QImage();
        source_->setText("50 x 50 pixels | No image");
    }
    trackPixel(trackedPixel_);
}

void SelectionDialog::trackPixel(QPoint pixel) {
    if (!state_.image.hasImage() || pixel.x() < 0 || pixel.y() < 0 || pixel.x() >= cropSize || pixel.y() >= cropSize)
        pixel = {-1, -1};
    trackedPixel_ = pixel;
    pixels_->tracked = pixel;
    pixels_->update();
    if (pixel.x() < 0) {
        position_->setText("Move the mouse over the image to inspect a pixel");
        values_->setText("R: --   G: --   B: --   |   Raw: --");
        swatch_->setStyleSheet("background: #1c1f23; border: 1px solid #9ca9ba;");
        return;
    }
    const auto sensor = cropOrigin(state_) + pixel;
    position_->setText(QString("Crop: (%1, %2)   |   Sensor: (%3, %4)")
        .arg(pixel.x()).arg(pixel.y()).arg(sensor.x()).arg(sensor.y()));
    const auto rgb = pixels_->image.pixelColor(pixel);
    const auto raw = state_.image.frame().pixels[std::size_t(sensor.y()) * kSensorWidth + sensor.x()];
    values_->setText(QString("R: %1   G: %2   B: %3   |   %4   |   Raw: %5")
        .arg(rgb.red()).arg(rgb.green()).arg(rgb.blue()).arg(rgb.name().toUpper()).arg(raw));
    swatch_->setStyleSheet("background: " + rgb.name() + "; border: 1px solid #9ca9ba;");
}
}
