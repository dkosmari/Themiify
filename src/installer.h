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

namespace Installer {
    struct UThemeMetadata {
        std::optional<std::string> themeID;
        std::string themeName;
        std::optional<std::string> themeAuthor;
        std::optional<std::string> themeVersion;
    };

    struct InstalledThemeMetadata {
        UThemeMetadata uthemeMetadata;
        std::filesystem::path themePath;
        std::filesystem::path previewPath;
        std::filesystem::path legacyMetadataPath;
    };

    bool GetUThemeMetadata(const std::filesystem::path &themePath,
                           UThemeMetadata &meta);
    bool GetInstalledThemeMetadata(const std::filesystem::path &installedThemePath,
                                   InstalledThemeMetadata &imeta);

    using progress_function_sig = void (const std::string &msg);
    using progress_function_t = std::function<progress_function_sig>;

    using success_function_sig = void ();
    using success_function_t = std::function<success_function_sig>;

    using error_function_sig = void (const std::exception &e);
    using error_function_t = std::function<error_function_sig>;

    void InstallTheme(std::stop_token &stopper,
                      const std::filesystem::path &themePath,
                      UThemeMetadata themeMetadata,
                      progress_function_t progressCallback,
                      success_function_t successCallback,
                      error_function_t errorCallback);
    bool DeleteTheme(const std::filesystem::path &themePath,
                     const std::filesystem::path &metadataPath);
    bool SetCurrentTheme(const std::filesystem::path &themePath);
    std::string GetCurrentThemeName();
    std::optional<InstalledThemeMetadata> GetCurrentTheme();

    std::filesystem::path GetThemePath(const UThemeMetadata& meta);
    // std::filesystem::path GetThumbnailPath(const InstalledThemeMetadata& imeta);
} // namespace Installer
