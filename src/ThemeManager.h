/*
 * Themiify - A theme manager for the Nintendo Wii U
 * Copyright (C) 2026 Fangal-Airbag
 * Copyright (C) 2026 AlphaCraft9658
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <exception>
#include <filesystem>
#include <functional>
#include <optional>
#include <stop_token>
#include <string>
#include <unordered_set>
#include <vector>

namespace ThemeManager {

    struct UThemeMetadata {
        std::optional<std::string> themeID;
        std::string themeName;
        std::optional<std::string> themeAuthor;
        std::optional<std::string> themeVersion;
    };

    struct InstalledThemeMetadata {
        UThemeMetadata uthemeMetadata;
        std::filesystem::path themePath;
        std::vector<std::filesystem::path> previewPaths;
        std::filesystem::path legacyMetadataPath;
        std::vector<std::filesystem::path> files;
    };

    struct StyleMiiUCfg {
        std::unordered_set<std::string> enabledThemes = {};
        bool mashupThemes = false;
        bool showNotification = false;
        bool shuffleThemes = false;
        bool themeManagerEnabled = false;
    };

    using progress_function_sig = void (const std::string &msg);
    using progress_function_t = std::function<progress_function_sig>;

    using success_function_sig = void ();
    using success_function_t = std::function<success_function_sig>;

    using error_function_sig = void (const std::exception &e);
    using error_function_t = std::function<error_function_sig>;

    using InstalledThemeCallbackSignature = void (std::size_t index,
                                                  const InstalledThemeMetadata& meta);
    using InstalledThemeFunction = std::function<InstalledThemeCallbackSignature>;

    void initialize();
    void finalize();

    void process();

    bool GetUThemeMetadata(const std::filesystem::path &themePath,
                           UThemeMetadata &meta);

    bool GetInstalledThemeMetadata(const std::filesystem::path &installedThemePath,
                                   InstalledThemeMetadata &imeta);

    std::vector<InstalledThemeMetadata> GetInstalledThemes(std::stop_token& stopper);

    void InstallTheme(std::stop_token &stopper,
                      const std::filesystem::path &themePath,
                      UThemeMetadata themeMetadata,
                      progress_function_t progressCallback,
                      success_function_t successCallback,
                      error_function_t errorCallback);
    void UninstallTheme(const InstalledThemeMetadata& meta);

    std::string GetCurrentThemeName();
    std::optional<InstalledThemeMetadata> GetCurrentTheme();

    std::filesystem::path GetThemePath(const UThemeMetadata& meta);

    bool IsShuffling();
    void ToggleShuffling();

    bool IsEnabled(const InstalledThemeMetadata& meta);
    void Enable(const InstalledThemeMetadata& meta);
    void Disable(const InstalledThemeMetadata& meta);

    void ReloadStyleMiiUCfg();

    StyleMiiUCfg* GetStyleMiiUCfg();

    void DeleteStyleMiiUCfg();

    void RefreshInstalledThemes();

    bool IsRefreshingThemes();

    void ForEachInstalledTheme(const InstalledThemeFunction& func);

} // namespace ThemeManager
