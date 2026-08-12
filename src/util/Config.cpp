#include "cam86/util/Config.hpp"

#include <charconv>
#include <fstream>
#include <stdexcept>

namespace cam86 {

Config::Config(std::filesystem::path path) : path_(std::move(path)) {}

void Config::load() {
    std::ifstream input(path_);
    if (!input) return;
    std::string line;
    while (std::getline(input, line)) {
        if (line.empty() || line[0] == '#' || line[0] == ';' || line[0] == '[') continue;
        const auto separator = line.find('=');
        if (separator == std::string::npos) continue;
        values_[line.substr(0, separator)] = line.substr(separator + 1);
    }
}

void Config::save() const {
    std::ofstream output(path_, std::ios::trunc);
    if (!output) throw std::runtime_error("Cannot save configuration: " + path_.string());
    output << "# CAM86-View settings\n";
    for (const auto& [key, value] : values_) output << key << '=' << value << '\n';
}

int Config::getInt(const std::string& key, const int fallback) const {
    const auto found = values_.find(key);
    if (found == values_.end()) return fallback;
    int value = fallback;
    const auto result = std::from_chars(found->second.data(), found->second.data() + found->second.size(), value);
    return result.ec == std::errc{} ? value : fallback;
}

bool Config::getBool(const std::string& key, const bool fallback) const {
    return getInt(key, fallback ? 1 : 0) != 0;
}

std::string Config::getString(const std::string& key, std::string fallback) const {
    const auto found = values_.find(key);
    return found == values_.end() ? std::move(fallback) : found->second;
}

void Config::set(const std::string& key, const int value) { values_[key] = std::to_string(value); }
void Config::set(const std::string& key, const bool value) { set(key, value ? 1 : 0); }
void Config::set(const std::string& key, std::string value) { values_[key] = std::move(value); }

} // namespace cam86

