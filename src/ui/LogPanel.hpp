#pragma once

#include "app/AppState.hpp"

#include <cstddef>

namespace cam86::ui {

class LogPanel {
public:
    void draw(AppState& state);

private:
    bool scrollToBottom_ = false;
    std::size_t lastSize_ = 0;
};

} // namespace cam86::ui

