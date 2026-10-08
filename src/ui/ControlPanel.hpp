#pragma once
#include "app/AppState.hpp"
#include <QWidget>
class QComboBox;
class QPushButton;
class QProgressBar;
class QLabel;
class QSpinBox;
namespace cam86::ui {
class ControlPanel : public QWidget {
public:
    explicit ControlPanel(AppState& state, QWidget* parent = nullptr);
    void refresh();
private:
    void startOne();
    void writeCurrentFits();
    AppState& state_;
    QComboBox* backend_;
    QPushButton* connection_;
    QPushButton* capture_;
    QPushButton* stop_;
    QPushButton* continuous_;
    QProgressBar* progress_;
    QLabel* temperature_;
    QSpinBox* frames_;
};
}
