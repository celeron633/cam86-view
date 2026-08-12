#pragma once

#include "app/AppState.hpp"
#include "ui/FileDialog.hpp"

namespace cam86::ui {

class ControlPanel {
public:
    void draw(AppState& state);

private:
    static void startOne(AppState& state);
    static void writeCurrentFits(AppState& state);
    FileDialog darkFileDialog_;
};

} // namespace cam86::ui
