/*
 * Themiify - A theme manager for the Nintendo Wii U
 * Copyright (C) 2026 Fangal-Airbag
 * Copyright (C) 2026 AlphaCraft9658
 * Copyright (C) 2026 Daniel K. O. <dkosmari>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <iostream>
#include <optional>
#include <stdexcept>

#include <glaze/exceptions/json_exceptions.hpp>
#include <glaze/json.hpp>
#include <mocha/mocha.h>

#include "PluginManager.h"

#include "tracer.hpp"
#include "utils.h"

using std::cerr;
using std::cout;
using std::endl;
using namespace std::literals;

/*
 * NOTE: customized Config serialization:
 *
 * - enabledThemes is converted to string for the JSON.
 *
 * - shuffleThemes has an alias "suffleThemes" until StyleMiiU decides to fix the config
 *   key.
 */
template<>
struct glz::meta<PluginManager::Config> {

    using T = PluginManager::Config;

    static constexpr
    auto read_enabledThemes = [](T& self,
                                 const std::string& arg)
    {
        self.enabledThemes.clear();
        auto themes = split(arg, "|", true);
        for (const auto& theme : themes)
            if (!theme.empty())
                self.enabledThemes.insert(theme);
    };

    static constexpr
    auto write_enabledThemes = [](const T& self) -> std::string
    {
        std::string result;
        if (self.shuffleThemes) {
            // Keep them sorted on the JSON.
            std::vector<std::string> sorted{self.enabledThemes.begin(),
                                            self.enabledThemes.end()};
            std::ranges::sort(sorted, IgnoreCaseLess{});
            result = join(sorted, "|");
        } else {
            if (!self.enabledThemes.empty())
                result = *self.enabledThemes.begin();
        }
        return result;
    };

    static constexpr
    auto modify = glz::object(
        "enabledThemes", glz::custom<read_enabledThemes, write_enabledThemes>,

        "shuffleThemes", &T::shuffleThemes,
        "suffleThemes", [](auto& self) -> auto& { return self.shuffleThemes; }
    );

}; // struct glz::meta<PluginManager::Config>

namespace PluginManager {

    namespace {

        bool pluginFound;

        std::optional<Config> config;

        // Helper struct to match what Aroma serializes.
        struct AromaConfig {
            Config storageitems;
        };

        std::filesystem::path
        GetAromaPath()
        {
            char buffer[256];
            if (auto res = Mocha_GetEnvironmentPath(buffer, sizeof buffer)) {
                std::runtime_error e{"Could not get Aroma environment path: "s
                                     + Mocha_GetStatusStr(res)};
                cerr << "ERROR: " << e.what() << endl;
                throw e;
            }
            return buffer;
        }

        std::filesystem::path
        GetPluginPath()
        {
            return GetAromaPath() / "plugins/stylemiiu.wps";
        }

        std::filesystem::path
        GetConfigPath()
        {
            return GetAromaPath() / "plugins/config/style-mii-u.json";
        }

        void
        LoadConfig()
        {
            config.reset();
            auto cfg_path = GetConfigPath();
            if (!exists(cfg_path)) {
                // If config doesn't exist yet, it's not an error.
                config.emplace();
                return;
            }
            AromaConfig aroma_cfg;
            glz::ex::read_file_json(aroma_cfg,
                                    cfg_path.string(),
                                    std::string{});
            config = std::move(aroma_cfg.storageitems);
            // Constraint: at most one theme can be enabled when not shuffling.
            if (!config->shuffleThemes && config->enabledThemes.size() > 1) {
                auto first = *config->enabledThemes.begin();
                config->enabledThemes = { first };
            }
        }

        void
        SaveConfig()
        {
            if (!config)
                throw std::runtime_error{"No valid StyleMiiU config state to store."};
            AromaConfig aroma_cfg;
            aroma_cfg.storageitems = *config;
            glz::ex::write_file_json<glz::opts{.prettify = true}>(
                aroma_cfg,
                GetConfigPath().string(),
                std::string{});
        }

    } // namespace

    void
    initialize()
    {
        TRACE_FUNC;

        try {
            LoadConfig();
        }
        catch (std::exception& e) {
            cerr << "ERROR: failed to load StyleMiiU config: " << e.what() << endl;
        }

        try {
            pluginFound = exists(GetPluginPath());
        }
        catch (std::exception& e) {
            pluginFound = false;
            cerr << "ERROR: trying to check for plugin presence: " << e.what() << endl;
        }
    }

    void
    finalize()
    {
        TRACE_FUNC;

        try {
            SaveConfig();
        }
        catch (std::exception& e) {
            cerr << "ERROR: failed to save StyleMiiU config: " << e.what() << endl;
        }
    }

    bool
    IsInstalled()
        noexcept
    {
        return pluginFound;
    }

    Config*
    GetConfig()
    {
        if (!config)
            return nullptr;
        return &*config;
    }

    void
    ReloadConfig()
    {
        try {
            LoadConfig();
        }
        catch (std::exception& e) {
            cerr << "ERROR: failed to reload StyleMiiU config: " << e.what() << endl;
        }
    }

    void
    DeleteConfig()
    {
        try {
            auto cfg_path = GetConfigPath();
            if (exists(cfg_path))
                remove(cfg_path);
            config.emplace();
        }
        catch (std::exception& e) {
            cerr << "ERROR in DeleteConfig(): " << e.what() << endl;
        }
    }

    std::string
    GetCurrentTheme()
    {
        if (!config)
            return {};
        if (!config->themeManagerEnabled)
            return {};
        if (config->enabledThemes.size() != 1)
            return {};
        return *config->enabledThemes.begin();
    }

    bool
    IsShuffling()
    {
        if (!config)
            return false;
        return config->shuffleThemes;
    }

    void
    ToggleShuffling()
    {
        if (!config)
            return;
        config->shuffleThemes = !config->shuffleThemes;
        if (!config->shuffleThemes)
            config->enabledThemes.clear();
    }

    bool
    IsEnabled(const std::string& folder_name)
    {
        if (!config)
            return false;
        return config->enabledThemes.contains(folder_name);
    }

    bool
    IsEnabled(const std::filesystem::path& theme_path)
    {
        return IsEnabled(theme_path.filename().string());
    }

    void
    Enable(const std::string& folder_name)
    {
        if (!config)
            return;
        if (!config->shuffleThemes)
            config->enabledThemes.clear();
        config->enabledThemes.insert(folder_name);
    }

    void
    Enable(const std::filesystem::path& theme_path)
    {
        Enable(theme_path.filename().string());
    }

    void
    Disable(const std::string& folder_name)
    {
        if (!config)
            return;
        config->enabledThemes.erase(folder_name);
    }

    void
    Disable(const std::filesystem::path& theme_path)
    {
        Disable(theme_path.filename().string());
    }

} // namespace PluginManager
