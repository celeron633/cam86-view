#pragma once
#include "app/AppState.hpp"
#include <QDialog>
#include <QPoint>
#include <limits>

class QLabel;
namespace cam86::ui {
class PixelSelectionView;
class SelectionDialog : public QDialog {
public:
    explicit SelectionDialog(AppState& state, QWidget* parent = nullptr);
    void refresh();
private:
    void trackPixel(QPoint pixel);
    AppState& state_;
    PixelSelectionView* pixels_ = nullptr;
    QLabel* source_ = nullptr;
    QLabel* position_ = nullptr;
    QLabel* values_ = nullptr;
    QLabel* swatch_ = nullptr;
    QPoint trackedPixel_{-1, -1};
    std::uint64_t revision_ = std::numeric_limits<std::uint64_t>::max();
};
}
