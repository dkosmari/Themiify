/*
 * Themiify - A theme manager for the Nintendo Wii U
 * Copyright (C) 2026 Fangal-Airbag
 * Copyright (C) 2026 AlphaCraft9658
 * Copyright (C) 2026 Daniel K. O. <dkosmari>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <filesystem>
#include <string>
#include <unordered_set>

namespace PluginManager {

    struct Config {
        std::unordered_set<std::string> enabledThemes = {};
        bool mashupThemes = false;
        bool showNotification = false;
        bool shuffleThemes = false;
        bool themeManagerEnabled = false;
    };

    void initialize();
    void finalize();

    bool IsInstalled() noexcept;

    Config* GetConfig();

    void ReloadConfig();
    void DeleteConfig();

    std::string GetCurrentTheme();

    bool IsShuffling();
    void ToggleShuffling();

    bool IsEnabled(const std::string& folder_name);
    bool IsEnabled(const std::filesystem::path& theme_path);

    void Enable(const std::string& folder_name);
    void Enable(const std::filesystem::path& theme_path);

    void Disable(const std::string& folder_name);
    void Disable(const std::filesystem::path& theme_path);

} // namespace PluginManager
