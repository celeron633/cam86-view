#pragma once

#include <filesystem>
#include <string>
#include <unordered_map>

namespace cam86 {

class Config {
public:
    explicit Config(std::filesystem::path path);

    void load();
    void save() const;
    [[nodiscard]] int getInt(const std::string& key, int fallback) const;
    [[nodiscard]] bool getBool(const std::string& key, bool fallback) const;
    [[nodiscard]] std::string getString(const std::string& key, std::string fallback) const;
    void set(const std::string& key, int value);
    void set(const std::string& key, bool value);
    void set(const std::string& key, std::string value);

private:
    std::filesystem::path path_;
    std::unordered_map<std::string, std::string> values_;
};

} // namespace cam86

