#pragma once
#include "app/AppState.hpp"
#include <QImage>
#include <QPointer>
#include <QWidget>

namespace cam86::ui {
class HistogramDialog;
class SelectionDialog;
class ImagePanel : public QWidget {
public:
    enum class View { Main, Crop, Histogram };
    ImagePanel(AppState& state, View view, QWidget* parent = nullptr);
    void refresh();
protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent* event) override;
private:
    QRect imageRect() const;
    AppState& state_;
    View view_;
    QImage image_;
    std::uint64_t revision_ = 0;
    QPointer<HistogramDialog> histogramDialog_;
    QPointer<SelectionDialog> selectionDialog_;
};
}
