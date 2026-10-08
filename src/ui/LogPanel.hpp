#pragma once
#include "app/AppState.hpp"
#include <QPlainTextEdit>
namespace cam86::ui {
class LogPanel : public QPlainTextEdit {
public:
    explicit LogPanel(AppState& state, QWidget* parent = nullptr);
    void refresh();
private:
    AppState& state_;
    std::vector<std::string> displayed_;
};
}
