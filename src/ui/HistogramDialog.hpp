#pragma once
#include "app/AppState.hpp"
#include <QDialog>
#include <array>
#include <limits>

class QLabel;
namespace cam86::ui {
class HistogramPlot;
class HistogramDialog : public QDialog {
public:
    explicit HistogramDialog(AppState& state, QWidget* parent = nullptr);
    void refresh();
private:
    void trackLevel(int level);
    AppState& state_;
    std::array<HistogramPlot*, 4> plots_{};
    std::array<QLabel*, 3> statistics_{};
    std::array<QLabel*, 3> readings_{};
    QLabel* source_ = nullptr;
    QLabel* level_ = nullptr;
    int trackedLevel_ = -1;
    std::uint64_t revision_ = std::numeric_limits<std::uint64_t>::max();
};
}
