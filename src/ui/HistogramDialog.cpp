#include "ui/HistogramDialog.hpp"
#include <QComboBox>
#include <QDialogButtonBox>
#include <QGridLayout>
#include <QLabel>
#include <QLinearGradient>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>
#include <functional>
#include <numeric>

namespace cam86::ui {
namespace {
using Bins = std::array<std::uint32_t, 256>;
const std::array<QColor, 3> colors{QColor(244, 97, 97), QColor(78, 211, 128), QColor(99, 157, 255)};
const std::array<QString, 3> names{"Red", "Green", "Blue"};
std::array<const Bins*, 3> channels(const AppState& state) {
    return {&state.image.histogramR(), &state.image.histogramG(), &state.image.histogramB()};
}
}

class HistogramPlot : public QWidget {
public:
    HistogramPlot(AppState& state, int channel, QWidget* parent)
        : QWidget(parent), state_(state), channel_(channel) {
        setMouseTracking(true);
        setCursor(Qt::CrossCursor);
        setMinimumSize(190, channel < 0 ? 220 : 155);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        setObjectName(channel < 0 ? "rgbHistogramPlot" : names[channel].toLower() + "HistogramPlot");
    }
    QRectF plotRect() const { return QRectF(rect()).adjusted(52, 28, -16, -46); }
    std::function<void(int)> onTrack;
    int level = -1;
    bool logarithmic = true;
protected:
    void mouseMoveEvent(QMouseEvent* event) override {
        const auto area = plotRect();
        const int value = area.contains(event->position()) && state_.image.hasImage()
            ? std::clamp(int(std::lround((event->position().x() - area.left()) * 255 / area.width())), 0, 255)
            : -1;
        if (onTrack) onTrack(value);
    }
    void leaveEvent(QEvent*) override { if (onTrack) onTrack(-1); }
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.fillRect(rect(), QColor("#1c1f23"));
        const auto area = plotRect();
        const auto bins = channels(state_);
        std::uint32_t peak = 0;
        for (int c = 0; c < 3; ++c)
            if (channel_ < 0 || c == channel_)
                peak = std::max(peak, *std::max_element(bins[c]->begin(), bins[c]->end()));
        const auto scaled = [this](double count) { return logarithmic ? std::log1p(count) : count; };
        const double maximum = std::max(1.0, scaled(peak));
        p.setPen(channel_ < 0 ? QColor("#d9e0ea") : colors[channel_]);
        p.drawText(QRectF(12, 4, width() - 24, 20), Qt::AlignLeft | Qt::AlignVCenter,
                   channel_ < 0 ? "RGB overlay" : names[channel_]);
        for (int tick = 0; tick <= 4; ++tick) {
            const double fraction = tick / 4.0;
            const double y = area.bottom() - fraction * area.height();
            p.setPen(QColor("#363c44"));
            p.drawLine(QPointF(area.left(), y), QPointF(area.right(), y));
            const double count = logarithmic ? std::expm1(fraction * maximum) : fraction * maximum;
            const QString label = count >= 1000 ? QString::number(count / 1000, 'f', count >= 10000 ? 0 : 1) + "k"
                                               : QString::number(count, 'f', 0);
            p.setPen(QColor("#aeb8c6"));
            p.drawText(QRectF(0, y - 8, 46, 16), Qt::AlignRight | Qt::AlignVCenter, label);
        }
        for (const int tick : {0, 64, 128, 192, 255}) {
            const double x = area.left() + tick / 255.0 * area.width();
            p.setPen(QColor("#363c44"));
            p.drawLine(QPointF(x, area.top()), QPointF(x, area.bottom()));
            p.setPen(QColor("#aeb8c6"));
            p.drawText(QRectF(x - 15, area.bottom() + 3, 30, 18), Qt::AlignCenter, QString::number(tick));
        }
        p.save();
        p.setClipRect(area.adjusted(-1, -1, 1, 1));
        for (int c = 0; c < 3; ++c) {
            if (channel_ >= 0 && c != channel_) continue;
            QPainterPath path;
            for (int i = 0; i < 256; ++i) {
                const QPointF point(area.left() + i / 255.0 * area.width(),
                    area.bottom() - scaled((*bins[c])[i]) / maximum * area.height());
                if (i == 0) path.moveTo(point); else path.lineTo(point);
            }
            auto fill = path;
            fill.lineTo(area.bottomRight());
            fill.lineTo(area.bottomLeft());
            fill.closeSubpath();
            auto color = colors[c];
            color.setAlpha(channel_ < 0 ? 35 : 65);
            p.fillPath(fill, color);
            p.setPen(QPen(colors[c], 1.4));
            p.drawPath(path);
        }
        if (level >= 0) {
            const double x = area.left() + level / 255.0 * area.width();
            p.setPen(QPen(QColor("#f4f6fa"), 1, Qt::DashLine));
            p.drawLine(QPointF(x, area.top()), QPointF(x, area.bottom()));
            for (int c = 0; c < 3; ++c) {
                if (channel_ >= 0 && c != channel_) continue;
                const double y = area.bottom() - scaled((*bins[c])[level]) / maximum * area.height();
                p.setPen(QPen(colors[c], 1));
                p.setBrush(colors[c]);
                p.drawEllipse(QPointF(x, y), 3, 3);
            }
        }
        p.restore();
        QLinearGradient ramp(area.left(), 0, area.right(), 0);
        ramp.setColorAt(0, Qt::black);
        ramp.setColorAt(1, channel_ < 0 ? QColor(Qt::white) : colors[channel_]);
        p.fillRect(QRectF(area.left(), area.bottom() + 25, area.width(), 8), ramp);
        if (!state_.image.hasImage()) {
            p.setPen(QColor("#d9e0ea"));
            p.drawText(area, Qt::AlignCenter, "No image - capture a frame to view its histogram");
        }
    }
private:
    AppState& state_;
    int channel_;
};

HistogramDialog::HistogramDialog(AppState& state, QWidget* parent) : QDialog(parent), state_(state) {
    setObjectName("histogramDialog");
    setWindowTitle("RGB Histogram - CAM86-View");
    setModal(false);
    resize(920, 740);
    setMinimumSize(760, 650);
    auto* layout = new QVBoxLayout(this);
    auto* toolbar = new QHBoxLayout;
    source_ = new QLabel;
    source_->setObjectName("histogramSource");
    toolbar->addWidget(source_, 1);
    toolbar->addWidget(new QLabel("Scale:"));
    auto* scale = new QComboBox;
    scale->setObjectName("histogramScale");
    scale->addItems({"Logarithmic", "Linear"});
    toolbar->addWidget(scale);
    layout->addLayout(toolbar);
    plots_[0] = new HistogramPlot(state, -1, this);
    layout->addWidget(plots_[0], 3);
    auto* separate = new QHBoxLayout;
    for (int c = 0; c < 3; ++c) {
        plots_[c + 1] = new HistogramPlot(state, c, this);
        separate->addWidget(plots_[c + 1], 1);
    }
    layout->addLayout(separate, 2);
    level_ = new QLabel;
    level_->setObjectName("histogramLevel");
    layout->addWidget(level_);
    auto* details = new QGridLayout;
    for (int c = 0; c < 3; ++c) {
        auto* name = new QLabel(names[c]);
        name->setStyleSheet("color: " + colors[c].darker(150).name() + "; font-weight: bold;");
        details->addWidget(name, c, 0);
        readings_[c] = new QLabel;
        readings_[c]->setObjectName(names[c].toLower() + "HistogramReading");
        statistics_[c] = new QLabel;
        statistics_[c]->setObjectName(names[c].toLower() + "HistogramStatistics");
        details->addWidget(readings_[c], c, 1);
        details->addWidget(statistics_[c], c, 2);
    }
    layout->addLayout(details);
    auto* note = new QLabel("Move over any graph to inspect a level. Cumulative % counts pixels at or below that level.\n"
                            "Overlay uses a shared vertical scale; individual graphs scale to each channel's peak.");
    note->setWordWrap(true);
    layout->addWidget(note);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
    for (auto* plot : plots_) plot->onTrack = [this](int value) { trackLevel(value); };
    connect(scale, &QComboBox::currentIndexChanged, this, [this](int index) {
        for (auto* plot : plots_) { plot->logarithmic = index == 0; plot->update(); }
    });
    refresh();
}

void HistogramDialog::refresh() {
    if (revision_ == state_.image.revision()) return;
    revision_ = state_.image.revision();
    const auto bins = channels(state_);
    const auto total = std::accumulate(bins[0]->begin(), bins[0]->end(), std::uint64_t{0});
    source_->setText(state_.image.hasImage()
        ? QString("Preview RGB | %1 x %2 | %3 pixels/channel | 8-bit (0-255)")
            .arg(state_.image.previewWidth()).arg(state_.image.previewHeight()).arg(qulonglong(total))
        : "Preview RGB | No image");
    for (int c = 0; c < 3; ++c) {
        double sum = 0, squared = 0;
        std::uint64_t cumulative = 0;
        int median = -1;
        for (int i = 0; i < 256; ++i) {
            sum += double(i) * (*bins[c])[i];
            squared += double(i) * i * (*bins[c])[i];
            cumulative += (*bins[c])[i];
            if (median < 0 && total && cumulative >= (total + 1) / 2) median = i;
        }
        const double mean = total ? sum / total : 0;
        const double deviation = total ? std::sqrt(std::max(0.0, squared / total - mean * mean)) : 0;
        statistics_[c]->setText(total ? QString("Mean %1   Std dev %2   Median %3")
            .arg(mean, 0, 'f', 2).arg(deviation, 0, 'f', 2).arg(median) : "Mean --   Std dev --   Median --");
    }
    trackLevel(trackedLevel_);
}

void HistogramDialog::trackLevel(int level) {
    trackedLevel_ = state_.image.hasImage() ? level : -1;
    level_->setText(trackedLevel_ < 0 ? "Move the mouse over a graph to inspect RGB values" : QString("Level: %1 / 255").arg(trackedLevel_));
    const auto bins = channels(state_);
    for (int c = 0; c < 3; ++c) {
        if (trackedLevel_ < 0) { readings_[c]->setText("Count --   % --   Cumulative --"); continue; }
        const auto total = std::accumulate(bins[c]->begin(), bins[c]->end(), std::uint64_t{0});
        const auto cumulative = std::accumulate(bins[c]->begin(), bins[c]->begin() + trackedLevel_ + 1, std::uint64_t{0});
        const auto count = (*bins[c])[trackedLevel_];
        readings_[c]->setText(QString("Count %1   %2%   Cumulative %3%")
            .arg(count).arg(total ? 100.0 * count / total : 0, 0, 'f', 2)
            .arg(total ? 100.0 * cumulative / total : 0, 0, 'f', 2));
    }
    for (auto* plot : plots_) { plot->level = trackedLevel_; plot->update(); }
}
}
