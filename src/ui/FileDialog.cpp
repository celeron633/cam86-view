#include "ui/FileDialog.hpp"

#include <imgui.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <system_error>

namespace cam86::ui {

void FileDialog::open(std::filesystem::path initialPath, std::string extension) {
    extension_ = std::move(extension);
    if (!extension_.empty() && extension_.front() != '.') extension_.insert(extension_.begin(), '.');

    std::error_code error;
    if (initialPath.empty()) initialPath = std::filesystem::current_path(error);
    if (std::filesystem::is_directory(initialPath, error)) {
        directory_ = std::filesystem::absolute(initialPath, error);
        fileName_.front() = '\0';
    } else {
        auto parent = initialPath.parent_path();
        if (parent.empty() || !std::filesystem::is_directory(parent, error)) {
            parent = std::filesystem::current_path(error);
        }
        directory_ = std::filesystem::absolute(parent, error);
        const auto name = initialPath.filename().string();
        std::strncpy(fileName_.data(), name.c_str(), fileName_.size() - 1);
        fileName_.back() = '\0';
    }
    openPopup_ = true;
    refreshPending_ = true;
}

std::optional<std::filesystem::path> FileDialog::draw() {
    std::optional<std::filesystem::path> selected;
    if (openPopup_) {
        ImGui::OpenPopup("Open dark frame");
        openPopup_ = false;
    }

    ImGui::SetNextWindowSize(ImVec2(650, 430), ImGuiCond_Appearing);
    if (!ImGui::BeginPopupModal("Open dark frame", nullptr, ImGuiWindowFlags_NoSavedSettings)) {
        return selected;
    }
    if (refreshPending_) refresh();

    if (ImGui::Button("Up")) {
        const auto parent = directory_.parent_path();
        if (!parent.empty() && parent != directory_) enterDirectory(parent);
    }
    ImGui::SameLine();
    if (ImGui::Button("Refresh")) refreshPending_ = true;
    ImGui::SameLine();
    ImGui::TextUnformatted(directory_.string().c_str());

    ImGui::BeginChild("file-list", ImVec2(0, 300), ImGuiChildFlags_Borders);
    for (const auto& entry : entries_) {
        const auto name = entry.directory
            ? std::string("[") + entry.path.filename().string() + "]"
            : entry.path.filename().string();
        const bool active = !entry.directory && name == fileName_.data();
        if (ImGui::Selectable(name.c_str(), active, ImGuiSelectableFlags_AllowDoubleClick)) {
            if (entry.directory) {
                if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) enterDirectory(entry.path);
            } else {
                std::strncpy(fileName_.data(), name.c_str(), fileName_.size() - 1);
                fileName_.back() = '\0';
                if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                    selected = entry.path;
                    ImGui::CloseCurrentPopup();
                }
            }
        }
    }
    ImGui::EndChild();

    ImGui::SetNextItemWidth(-1);
    ImGui::InputText("##selected-file", fileName_.data(), fileName_.size());
    if (!error_.empty()) ImGui::TextColored(ImVec4(0.75F, 0.15F, 0.12F, 1.0F), "%s", error_.c_str());

    const bool hasName = fileName_.front() != '\0';
    ImGui::BeginDisabled(!hasName);
    if (ImGui::Button("Open", ImVec2(100, 0))) {
        auto path = directory_ / fileName_.data();
        if (path.extension().empty() && !extension_.empty()) path += extension_;
        std::error_code error;
        if (std::filesystem::is_regular_file(path, error)) {
            selected = std::move(path);
            ImGui::CloseCurrentPopup();
        } else {
            error_ = "File does not exist: " + path.string();
        }
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(100, 0))) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
    return selected;
}

void FileDialog::refresh() {
    entries_.clear();
    error_.clear();
    refreshPending_ = false;
    std::error_code error;
    for (std::filesystem::directory_iterator iterator(
             directory_, std::filesystem::directory_options::skip_permission_denied, error), end;
         iterator != end && !error; iterator.increment(error)) {
        const auto& path = iterator->path();
        const bool directory = iterator->is_directory(error);
        if (error) break;
        if (!directory && !extension_.empty()) {
            auto actualExtension = path.extension().string();
            auto wantedExtension = extension_;
            const auto lower = [](std::string& value) {
                std::transform(value.begin(), value.end(), value.begin(),
                               [](const unsigned char character) {
                                   return static_cast<char>(std::tolower(character));
                               });
            };
            lower(actualExtension);
            lower(wantedExtension);
            if (actualExtension != wantedExtension) continue;
        }
        entries_.push_back({path, directory});
    }
    if (error) error_ = "Cannot read directory: " + error.message();
    std::sort(entries_.begin(), entries_.end(), [](const Entry& left, const Entry& right) {
        if (left.directory != right.directory) return left.directory > right.directory;
        auto leftName = left.path.filename().string();
        auto rightName = right.path.filename().string();
#ifdef _WIN32
        std::transform(leftName.begin(), leftName.end(), leftName.begin(),
                       [](const unsigned char value) { return static_cast<char>(std::tolower(value)); });
        std::transform(rightName.begin(), rightName.end(), rightName.begin(),
                       [](const unsigned char value) { return static_cast<char>(std::tolower(value)); });
#endif
        return leftName < rightName;
    });
}

void FileDialog::enterDirectory(const std::filesystem::path& path) {
    directory_ = path;
    fileName_.front() = '\0';
    refreshPending_ = true;
}

} // namespace cam86::ui
