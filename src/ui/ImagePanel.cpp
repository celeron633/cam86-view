#include "ui/ImagePanel.hpp"
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <algorithm>
#include <cmath>

namespace cam86::ui {
ImagePanel::ImagePanel(AppState& state, View view, QWidget* parent)
    : QWidget(parent), state_(state), view_(view) {
    setMinimumSize(view == View::Main ? QSize(300, 200) : QSize(160, 100));
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}
void ImagePanel::refresh() {
    if (revision_ == state_.image.revision()) return;
    revision_ = state_.image.revision();
    if (state_.image.hasImage() && view_ != View::Histogram) {
        const bool crop = view_ == View::Crop;
        const auto rgba = crop ? state_.image.cropRgba() : state_.image.previewRgba();
        const int w = crop ? 50 : state_.image.previewWidth();
        const int h = crop ? 50 : state_.image.previewHeight();
        image_ = QImage(rgba.data(), w, h, w * 4, QImage::Format_RGBA8888).copy();
    }
    update();
}
QRect ImagePanel::imageRect() const {
    const QSize source = image_.isNull() ? QSize(3000, 2000) : image_.size();
    const QSize size = source.scaled(this->size(), Qt::KeepAspectRatio);
    return QRect(QPoint((width() - size.width()) / 2,
                        view_ == View::Main ? 0 : (height() - size.height()) / 2), size);
}
void ImagePanel::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.fillRect(rect(), view_ == View::Main ? palette().color(QPalette::Window) : QColor(28, 31, 35));
    if (view_ == View::Main) painter.fillRect(imageRect(), QColor(28, 31, 35));
    if (view_ == View::Histogram) {
        const auto& processor = state_.image;
        const std::array<const std::array<std::uint32_t, 256>*, 3> channels{
            &processor.histogramR(), &processor.histogramG(), &processor.histogramB()};
        const std::array<QColor, 3> colors{QColor(230,75,70), QColor(70,220,100), QColor(80,130,240)};
        double maximum = 1.0;
        for (const auto* channel : channels)
            for (auto count : *channel) maximum = std::max(maximum, std::log1p(double(count)));
        for (std::size_t c = 0; c < channels.size(); ++c) {
            QPainterPath path;
            for (int i = 0; i < 256; ++i) {
                QPointF point(i * (width() - 1) / 255.0,
                    height() - 1 - std::log1p(double((*channels[c])[i])) / maximum * (height() - 4));
                if (i == 0) path.moveTo(point); else path.lineTo(point);
            }
            painter.setPen(colors[c]);
            painter.drawPath(path);
        }
    } else if (!image_.isNull()) {
        painter.setRenderHint(QPainter::SmoothPixmapTransform, false);
        painter.drawImage(imageRect(), image_);
    } else {
        painter.setPen(QColor(160, 165, 170));
        painter.drawText(rect().adjusted(12,12,-12,-12), Qt::AlignCenter,
            view_ == View::Main ? "3000 x 2000 - No image" : "50 x 50 crop");
    }
}
void ImagePanel::mousePressEvent(QMouseEvent* event) {
    const auto target = imageRect();
    if (view_ == View::Main && state_.image.hasImage() && event->button() == Qt::LeftButton &&
        target.contains(event->position().toPoint())) {
        state_.image.select(float((event->position().x() - target.x()) / target.width()),
                            float((event->position().y() - target.y()) / target.height()));
        refresh();
    }
}
}
