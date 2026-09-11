#pragma once

#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace pl::modmenu {

enum class ConfigType { Toggle, SliderInt, SliderFloat, Radio, Color, Keybind, Text, Button };

struct ConfigEntry {
    std::string key;
    std::string displayName;
    ConfigType type{};
    std::string defaultValue;
    std::string minValue;
    std::string maxValue;
    std::string dependsOn;
};

// ABI-compatible with the current LeviLauncher/preloader ModuleInfo.
struct ModuleInfo {
    std::string moduleId;
    std::string displayName;
    std::string description;
    std::string modId;
    bool defaultEnabled{};
    bool hideInHudEditor{};
    std::vector<ConfigEntry> configs;
    std::function<void(std::string_view moduleId, bool enabled)> onToggle;
    std::function<void(std::string_view moduleId, std::string_view key,
                       std::string_view value)> onConfigChanged;
    std::function<void(std::string_view moduleId, std::string_view key,
                       bool isDown)> onKeybind;
};

bool registerModule(const ModuleInfo& info);
void unregisterModule(std::string_view moduleId);

class ModuleBuilder {
public:
    ModuleBuilder(std::string moduleId, std::string displayName) {
        mInfo.moduleId = std::move(moduleId);
        mInfo.displayName = std::move(displayName);
    }
    ModuleBuilder& description(std::string value) { mInfo.description = std::move(value); return *this; }
    ModuleBuilder& modId(std::string value) { mInfo.modId = std::move(value); return *this; }
    ModuleBuilder& defaultEnabled(bool value) { mInfo.defaultEnabled = value; return *this; }
    ModuleBuilder& hideInHudEditor(bool value = true) { mInfo.hideInHudEditor = value; return *this; }
    ModuleBuilder& onToggle(std::function<void(std::string_view, bool)> cb) { mInfo.onToggle = std::move(cb); return *this; }
    ModuleBuilder& onConfigChanged(std::function<void(std::string_view, std::string_view, std::string_view)> cb) { mInfo.onConfigChanged = std::move(cb); return *this; }
    ModuleBuilder& onKeybind(std::function<void(std::string_view, std::string_view, bool)> cb) { mInfo.onKeybind = std::move(cb); return *this; }
    ModuleBuilder& config(std::string key, std::string displayName, ConfigType type,
                          std::string defaultValue = {}, std::string minValue = {},
                          std::string maxValue = {}, std::string dependsOn = {}) {
        mInfo.configs.push_back(ConfigEntry{std::move(key), std::move(displayName), type,
                                            std::move(defaultValue), std::move(minValue),
                                            std::move(maxValue), std::move(dependsOn)});
        return *this;
    }
    [[nodiscard]] bool registerModule() const { return pl::modmenu::registerModule(mInfo); }
private:
    ModuleInfo mInfo;
};

} // namespace pl::modmenu
