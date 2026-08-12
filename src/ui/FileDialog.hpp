#pragma once

#include <array>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace cam86::ui {

class FileDialog {
public:
    void open(std::filesystem::path initialPath, std::string extension);
    [[nodiscard]] std::optional<std::filesystem::path> draw();

private:
    struct Entry {
        std::filesystem::path path;
        bool directory = false;
    };

    void refresh();
    void enterDirectory(const std::filesystem::path& path);

    bool openPopup_ = false;
    bool refreshPending_ = false;
    std::filesystem::path directory_;
    std::string extension_;
    std::vector<Entry> entries_;
    std::array<char, 512> fileName_{};
    std::string error_;
};

} // namespace cam86::ui

