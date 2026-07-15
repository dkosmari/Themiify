/*
 * Themiify - A theme manager for the Nintendo Wii U
 * Copyright (C) 2026 Fangal-Airbag
 * Copyright (C) 2026 AlphaCraft9658
 * Copyright (C) 2026  Daniel K. O. <dkosmari>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <algorithm>
#include <array>
#include <cstdio>
#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <memory>
#include <print>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

#include <zip.h>
#include <glaze/json.hpp>
#include <glaze/exceptions/json_exceptions.hpp>
#include <hips.hpp>
#include <mocha/mocha.h>

#include <sysapp/title.h>
#include <coreinit/systeminfo.h>

#include "ThemeManager.h"
#include "utils.h"
#include "tracer.hpp"
#include "thread_safe.hpp"

using std::cout;
using std::cerr;
using std::endl;
using namespace std::literals;

/*
 * NOTE: customized StyleMiiUCfg serialization:
 *
 * - enabledThemes is converted to string for the JSON.
 *
 * - shuffleThemes has an alias "suffleThemes" until StyleMiiU decides to fix the config
 *   key.
 */
template<>
struct glz::meta<ThemeManager::StyleMiiUCfg> {

    using T = ThemeManager::StyleMiiUCfg;

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
            std::ranges::sort(sorted, {}, as_lower_case);
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
}; // struct glz::meta<ThemeManager::StyleMiiUCfg>

namespace ThemeManager {

    std::unordered_map<std::string, std::filesystem::path> regionLangMap = {
        {"UsEn", "UsEnglish/Message/AllMessage.szs"},
        {"UsFr", "UsFrench/Message/AllMessage.szs"},
        {"UsPt", "UsPortuguese/Message/AllMessage.szs"},
        {"UsEs", "UsSpanish/Message/AllMessage.szs"},
        {"EuNl", "EuDutch/Message/AllMessage.szs"},
        {"EuEn", "EuEnglish/Message/AllMessage.szs"},
        {"EuFr", "EuFrench/Message/AllMessage.szs"},
        {"EuDe", "EuGerman/Message/AllMessage.szs"},
        {"EuIt", "EuItalian/Message/AllMessage.szs"},
        {"EuPt", "EuPortuguese/Message/AllMessage.szs"},
        {"EuRu", "EuRussian/Message/AllMessage.szs"},
        {"EuEs", "EuSpanish/Message/AllMessage.szs"},
        {"JpJa", "JpJapanese/Message/AllMessage.szs"}
    };

    struct StyleMiiUJson {
        StyleMiiUCfg storageitems;
    };

    std::optional<StyleMiiUCfg> styleMiiUCfg;

    using InstalledThemesList = std::vector<InstalledThemeMetadata>;

    thread_safe<InstalledThemesList> safe_themes_list;

    namespace {

        std::filesystem::path
        realGetStyleMiiUConfigPath()
        {
            char buffer[256];
            auto res = Mocha_GetEnvironmentPath(buffer, sizeof buffer);
            if (res) {
                std::runtime_error e{"Could not get Aroma environment path: "s
                                     + Mocha_GetStatusStr(res)};
                cerr << "ERROR: " << e.what() << endl;
                throw e;
            }
            return std::filesystem::path{buffer} / "plugins/config/style-mii-u.json";
        }

        std::filesystem::path
        GetStyleMiiUConfigPath()
        {
            static const std::filesystem::path result = realGetStyleMiiUConfigPath();
            return result;
        }

        static void LoadStyleMiiUCfg() {
            styleMiiUCfg.reset();
            auto stylemiiu_cfg_path = GetStyleMiiUConfigPath();
            if (!exists(stylemiiu_cfg_path)) {
                // If config doesn't exist yet, it's not an error.
                styleMiiUCfg.emplace();
                return;
            }
            StyleMiiUJson json;
            glz::ex::read_file_json(json, stylemiiu_cfg_path.string(), std::string{});
            styleMiiUCfg = std::move(json.storageitems);
            // Constraint: at most one theme can be enabled when not shuffling.
            if (!styleMiiUCfg->shuffleThemes && styleMiiUCfg->enabledThemes.size() > 1) {
                auto first = *styleMiiUCfg->enabledThemes.begin();
                styleMiiUCfg->enabledThemes = { first };
            }
        }

        void SaveStyleMiiUCfg() {
            if (!styleMiiUCfg)
                throw std::runtime_error{"No valid StyleMiiU config state to store."};
            StyleMiiUJson json;
            json.storageitems = *styleMiiUCfg;
            glz::ex::write_file_json<glz::opts{.prettify = true}>(
                json,
                GetStyleMiiUConfigPath().string(),
                std::string{}
            );
        }

        std::filesystem::path GetMenuContentPath() {
            uint64_t menuTitleID = _SYSGetSystemApplicationTitleId(SYSTEM_APP_ID_WII_U_MENU);

            uint32_t menuIDParentDir = menuTitleID >> 32;
            uint32_t menuIDChildDir = menuTitleID;

            char splitMenuID[18];
            snprintf(splitMenuID, sizeof splitMenuID,
                     "%08x/%08x",
                     menuIDParentDir,
                     menuIDChildDir);

            return "storage_mlc:/sys/title" / std::filesystem::path{splitMenuID} / "content";
        }

    } // namespace


    void initialize()
    {
        TRACE_FUNC;

        try {
            LoadStyleMiiUCfg();
        }
        catch (std::exception &e) {
            cerr << "ERROR: failed to load StyleMiiU config: " << e.what() << endl;
        }
    }

    void finalize()
    {
        TRACE_FUNC;
        try {
            SaveStyleMiiUCfg();
        }
        catch (std::exception &e) {
            cerr << "ERROR: failed to save StyleMiiU config: " << e.what() << endl;
        }
    }

    void process()
    {
        // TODO
    }

    void CreateCacheFile(std::ifstream &sourceFile, const std::filesystem::path &outputPath) {
        if (!sourceFile.is_open()) {
            cerr << "Invalid or unopened source file" << endl;
            return;
        }

        std::size_t sourceSize = static_cast<std::size_t>(sourceFile.tellg());
        sourceFile.seekg(0, std::ios::beg);

        std::vector<unsigned char> buffer(sourceSize);
        sourceFile.read(reinterpret_cast<char*>(buffer.data()), sourceSize);

        if (!sourceFile) {
            cerr << "Error reading source file to create cache file." << endl;
            return;
        }

        std::ofstream outputFile(outputPath, std::ios::binary | std::ios::trunc);
        if (!outputFile.is_open()) {
            cerr << "Failed to open output file for writing: " << outputPath << endl;
            return;
        }

        outputFile.write(reinterpret_cast<const char*>(buffer.data()), buffer.size());

        if (!outputFile) {
            cerr << "Error writing cache file to " << outputPath << endl;
            return;
        }

        cout << "Successfully cached file to: " << outputPath << endl;
    }

    struct MetadataJson {
        UThemeMetadata Metadata;
    };

    struct LegacyMetadataJson {
        struct {
            std::string themeName;
            std::string themeAuthor;
            std::string themeID;
            std::string themeIDPath;
            std::string themeVersion;
            std::string themeInstallPath;
        } ThemeData;
    };

    bool GetUThemeMetadata(const std::filesystem::path &themePath,
                           UThemeMetadata &umeta) {
        try {
            zip_t *themeArchive;
            zip_error_t error;
            int err;

            umeta = {};

            if (!(themeArchive = zip_open(themePath.c_str(), 0, &err))) {
                zip_error_init_with_code(&error, err);
                cerr << "Cannot open theme archive. Error Code: "
                     << zip_error_strerror(&error) << endl;
                zip_error_fini(&error);
                return false;
            }

            zip_file_t *themeMetadataFile;
            if (!(themeMetadataFile = zip_fopen(themeArchive, "metadata.json", ZIP_RDONLY))) {
                zip_error_init_with_code(&error, err);
                cerr << "Cannot open theme metadata. Error Code: "
                     << zip_error_strerror(&error) << endl;
                zip_error_fini(&error);
                zip_close(themeArchive);
                return false;
            }

            zip_stat_t metadataStatData;
            if (zip_stat(themeArchive, "metadata.json", 0, &metadataStatData) != 0) {
                zip_error_init_with_code(&error, err);
                cerr << "Cannot stat theme metadata! Error Code: "
                     << zip_error_strerror(&error) << endl;
                zip_error_fini(&error);
                zip_fclose(themeMetadataFile);
                zip_close(themeArchive);
                return 0;
            }

            std::string buffer(metadataStatData.size, '\0');
            zip_fread(themeMetadataFile, buffer.data(), metadataStatData.size);
            zip_fclose(themeMetadataFile);
            zip_close(themeArchive);

            MetadataJson metaJson;
            glz::ex::read_json(metaJson, buffer);
            umeta = std::move(metaJson.Metadata);
            return true;
        }
        catch (std::exception& e) {
            cerr << "ERROR: " << e.what() << endl;
            return false;
        }
    }

    bool GetInstalledThemeMetadata(const std::filesystem::path &installedThemePath,
                                   InstalledThemeMetadata &imeta) {

        static const std::array image_extensions{".webp", ".jpg", ".png"};
        try {
            imeta = {};
            imeta.themePath = installedThemePath;
            try {
                for (auto& entry
                         : std::filesystem::recursive_directory_iterator{imeta.themePath}) {
                    if (entry.is_regular_file())
                        imeta.files.push_back(entry.path());
                }
                std::ranges::sort(imeta.files, {}, as_lower_case);
            }
            catch (std::exception &e) {
                cerr << "Error listing files inside theme. SD card may be corrupted." << endl;
            }

            static const std::array image_names{
                "preview-collage",
                "preview-launcher",
                "preview-warawara"
            };
            for (auto name : image_names) {
                auto preview_base = imeta.themePath / name;
                for (auto ext : image_extensions) {
                    auto preview = preview_base;
                    preview += ext;
                    if (exists(preview)) {
                        imeta.previewPaths.push_back(preview);
                        break;
                    }
                }
            }
            MetadataJson metaJson;
            glz::ex::read_file_json(metaJson,
                                    (installedThemePath / "metadata.json").string(),
                                    std::string{});
            imeta.uthemeMetadata = std::move(metaJson.Metadata);

            // If there was no preview image on the theme folder, look for a cached thumbnail.
            if (imeta.themePath.empty() && imeta.uthemeMetadata.themeID) {
                auto preview = theme_id_to_cached_thumbnail_path(*imeta.uthemeMetadata.themeID);
                if (exists(preview))
                    imeta.previewPaths.push_back(preview);
            }

            return true;
        }
        catch (std::exception& e) {
            // cout << "Failed to get new theme metadata: " << e.what() << endl;

            // Fallback: find a json (in SD:/themiify/installed/) that matches this theme.
            auto folder_name = installedThemePath.filename();
            try {
                for (auto entry
                         : std::filesystem::directory_iterator{THEMIIFY_INSTALLED_THEMES}) {
                    if (!entry.is_regular_file())
                        continue;
                    if (entry.path().extension() != ".json")
                        continue;
                    try {
                        LegacyMetadataJson legacyMetaJson;
                        glz::ex::read_file_json(legacyMetaJson, entry.path().string(), std::string{});
                        auto& leg_meta = legacyMetaJson.ThemeData;
                        if (leg_meta.themeID.empty()) // not a Themezer theme, skip it
                            continue;
                        auto meta_folder_name = std::filesystem::path{leg_meta.themeInstallPath}.filename();
                        if (folder_name == meta_folder_name) {
                            imeta.uthemeMetadata.themeID = leg_meta.themeID;
                            imeta.uthemeMetadata.themeName = leg_meta.themeName;
                            imeta.uthemeMetadata.themeAuthor = leg_meta.themeAuthor;
                            imeta.uthemeMetadata.themeVersion = leg_meta.themeVersion;
                            imeta.legacyMetadataPath = entry.path();
                            auto preview =
                                theme_id_to_cached_thumbnail_path(*imeta.uthemeMetadata.themeID);
                            if (exists(preview))
                                imeta.previewPaths.push_back(preview);
                            return true;
                        }
                    }
                    catch (std::exception& e3) {
                        cout << "Failed to parse " << entry.path()
                             << ": " << e3.what() << endl;
                    }
                }
                // No matching metadata found, just use the folder name as the name.
                imeta.uthemeMetadata.themeName = folder_name;
                return true;
            }
            catch (std::exception& e2) {
                cerr << "ERROR: while looking for themiify metadata: " << e2.what() << endl;
                return false;
            }
            return false;
        }
    }

    std::vector<InstalledThemeMetadata> GetInstalledThemes(std::stop_token& stopper) {
        std::vector<InstalledThemeMetadata> result;
        try {
            for (auto& entry : std::filesystem::directory_iterator{THEMES_ROOT}) {
                if (stopper.stop_requested())
                    break;
                if (!entry.is_directory())
                    continue;
                InstalledThemeMetadata meta;
                if (GetInstalledThemeMetadata(entry.path(), meta))
                    result.push_back(std::move(meta));
            }
            if (stopper.stop_requested())
                return result;
            // Now sort them by path.
            std::ranges::sort(result,
                              {},
                              [](const InstalledThemeMetadata& meta) -> std::string
                              {
                                  return as_lower_case(meta.themePath);
                              });
        }
        catch (std::exception &e) {
            cerr << "ERROR in Installer::GetInstalledThemes(): "
                 << e.what() << endl;
        }
        return result;
    }

    void InstallTheme(std::stop_token &stopper,
                      const std::filesystem::path &uthemePath,
                      UThemeMetadata themeMetadata,
                      progress_function_t progressCallback,
                      success_function_t successCallback,
                      error_function_t errorCallback) {

        zip_t *themeArchive = nullptr;
        zip_file_t *patchFile = nullptr;
        std::filesystem::path themePath;

        try {

            auto reportProgress = [&progressCallback](const std::string &msg) {
                if (progressCallback)
                    progressCallback(msg);
                else
                    cout << msg << endl;
            };

            auto throwIfStopped = [&stopper] {
                if (stopper.stop_requested())
                    throw std::runtime_error{"Installation canceled."};
            };

            OSEnableHomeButtonMenu(FALSE);

            auto menuContentPath = GetMenuContentPath();

            zip_error_t error;
            int err;

            throwIfStopped();

            if (!(themeArchive = zip_open(uthemePath.c_str(), 0, &err))) {
                zip_error_init_with_code(&error, err);
                std::string msg = "Cannot open theme archive:"s + zip_error_strerror(&error);
                zip_error_fini(&error);
                throw std::runtime_error{msg};
            }

            throwIfStopped();

            reportProgress(std::format("Installing {}...", themeMetadata.themeName));

            themePath = GetThemePath(themeMetadata);

            reportProgress(std::format("Installing theme to: \"{}\"", themePath.string()));

            int64_t numEntries;
            if ((numEntries = zip_get_num_entries(themeArchive, ZIP_FL_UNCHANGED)) < 0) {
                zip_close(themeArchive);
                throw std::runtime_error{"themeArchive is NULL"};
            }

            const std::string AllMessage_ = "AllMessage_";
            for (uint64_t i = 0; i < static_cast<uint64_t>(numEntries); ++i) {

                throwIfStopped();

                std::filesystem::path menuFilePath;
                std::filesystem::path entryName = zip_get_name(themeArchive, i, ZIP_FL_ENC_RAW);
                std::string entryStem = entryName.stem().string();

                if (entryName == "Men.bps") {
                    menuFilePath = MEN_PATH;
                }
                else if (entryName == "Men2.bps") {
                    menuFilePath = MEN2_PATH;
                }
                else if (entryName == "cafe_barista_men.bps") {
                    menuFilePath = CAFE_BARISTA_MEN_PATH;
                }
                else if (entryStem.starts_with(AllMessage_) && entryName.extension() == ".bps") {
                    std::string regionLangStr = entryStem.substr(AllMessage_.size(), 4);
                    auto it = regionLangMap.find(regionLangStr);
                    if (it == regionLangMap.end()) {
                        reportProgress(std::format("Unknown AllMessage Region and Language: \"{}\"", regionLangStr));
                        continue;
                    }

                    menuFilePath = it->second;
                }

                if (!menuFilePath.empty()) {
                    // Found a known patch.
                    reportProgress(std::format("Handling patch for \"{}\"",
                                               menuFilePath.string()));

                    auto menuPath = menuContentPath / menuFilePath;
                    auto cachePath = THEMIIFY_ROOT / "cache" / menuFilePath;
                    auto patchPath = entryName;
                    auto outputPath = themePath / "content" / menuFilePath;

                    CreateParentDirectories(cachePath);

                    if (!(patchFile = zip_fopen(themeArchive, patchPath.c_str(), ZIP_RDONLY))) {
                        zip_error_init_with_code(&error, err);
                        std::string msg = std::format("Cannot open \"{}\"!. Error: {}",
                                                      patchPath.string(),
                                                      zip_error_strerror(&error));
                        zip_error_fini(&error);
                        throw std::runtime_error{msg};
                    }

                    zip_stat_t patchStatData;
                    if (zip_stat(themeArchive, patchPath.c_str(), 0, &patchStatData) != 0) {
                        zip_error_init_with_code(&error, err);
                        std::string msg = std::format("Cannot stat \"{}\"!. Error: {}",
                                                      patchPath.string(),
                                                      zip_error_strerror(&error));
                        zip_error_fini(&error);
                        zip_fclose(patchFile);
                        patchFile = nullptr;
                        throw std::runtime_error{msg};
                    }

                    throwIfStopped();

                    std::size_t patchSize = patchStatData.size;

                    std::ifstream inputFile(cachePath, std::ios::binary | std::ios::ate);
                    if (!inputFile.is_open()) {
                        reportProgress(std::format("Cache does not exist for \"{}\"",
                                                   menuPath.string()));

                        inputFile.clear();
                        inputFile.open(menuPath, std::ios::binary | std::ios::ate);

                        if (!inputFile.is_open()) {
                            inputFile.clear();
                            zip_fclose(patchFile);
                            patchFile = nullptr;
                            // NOTE: don't error out, just report
                            reportProgress(std::format("Could not open source file for \"{}\"",
                                                       patchPath.string()));
                            continue;
                        }
                        reportProgress(std::format("Creating cache for \"{}\"",
                                                   menuPath.string()));
                        CreateCacheFile(inputFile, cachePath);
                    }
                    else {
                        reportProgress(std::format("Found \"{}\" in cache at \"{}\"",
                                                   menuFilePath.string(),
                                                   cachePath.string()));
                    }

                    throwIfStopped();

                    std::size_t inputSize = inputFile.tellg();
                    if (inputSize != inputFile.tellg())
                        throw std::runtime_error{"Input file is too large."};
                    inputFile.seekg(0, std::ios::beg);

                    throwIfStopped();

                    std::vector<uint8_t> patchData(patchSize);
                    std::vector<uint8_t> inputData(inputSize);

                    zip_fread(patchFile, patchData.data(), patchData.size());
                    zip_fclose(patchFile);
                    patchFile = nullptr;

                    throwIfStopped();

                    inputFile.read(reinterpret_cast<char*>(inputData.data()), inputData.size());
                    inputFile.close();

                    throwIfStopped();

                    auto [bytes, result] = Hips::patch(
                        inputData.data(),
                        inputData.size(),
                        patchData.data(),
                        patchData.size(),
                        Hips::PatchType::BPS
                    );

                    throwIfStopped();

                    if (result == Hips::Result::Success) {
                        CreateParentDirectories(outputPath);

                        std::ofstream outputFile(outputPath, std::ios::binary);
                        outputFile.write(reinterpret_cast<char*>(bytes.data()), bytes.size());
                        outputFile.close();

                        reportProgress(std::format("File written to \"{}\"",
                                                   outputPath.string()));
                    }
                    else {
                        throw std::runtime_error{std::format("Patch failed! Hips result: {}",
                                                             static_cast<unsigned>(result))};
                    }
                }
                else {
                    // Not a known patch: if it's not a .bps file, just copy it verbatim
                    // to themePath.
                    if (entryName.extension() != ".bps") {
                        reportProgress(format("Copying file: \"{}\"", entryName.string()));
                        if (!(patchFile = zip_fopen(themeArchive,
                                                    entryName.c_str(),
                                                    ZIP_RDONLY))) {
                            zip_error_init_with_code(&error, err);
                            std::string msg = std::format("Cannot open \"{}\"!. Error: {}",
                                                          entryName.string(),
                                                          zip_error_strerror(&error));
                            zip_error_fini(&error);
                            throw std::runtime_error{msg};
                        }
                        auto outputPath = themePath / entryName;
                        if (outputPath.has_parent_path())
                            create_directories(outputPath.parent_path());
                        std::filebuf output;
                        if (!output.open(outputPath,
                                         std::ios::out | std::ios::binary | std::ios::trunc))
                            throw std::runtime_error{"Could not create output file: \""s
                                                     + outputPath.string() + "\""s};
                        std::vector<char> buf(65536);
                        zip_int64_t read = 0;
                        zip_int64_t total = 0;
                        while ((read = zip_fread(patchFile, buf.data(), buf.size())) > 0) {
                            output.sputn(buf.data(), read);
                            total += read;
                        }
                        output.close();
                        zip_fclose(patchFile);
                        patchFile = nullptr;
                        reportProgress(std::format("File written to \"{}\" ({} bytes)",
                                                   outputPath.string(),
                                                   total));
                    }
                }
            }

            zip_close(themeArchive);
            themeArchive = nullptr;

            throwIfStopped();

            OSEnableHomeButtonMenu(TRUE);

            if (successCallback)
                successCallback();
        }
        catch (std::exception &e) {
            if (patchFile)
                zip_fclose(patchFile);
            if (themeArchive)
                zip_close(themeArchive);
            cerr << "Deleting partially installed theme: " << themePath << endl;
            if (!themePath.empty())
                DeletePath(themePath);
            if (errorCallback)
                errorCallback(e);
            else
                cerr << "ERROR: " << e.what() << endl;
            OSEnableHomeButtonMenu(TRUE);
        }
    }

    void DeleteTheme(const InstalledThemeMetadata& meta) {
        // If there's a cached thumbnail, delete it.
        if (meta.uthemeMetadata.themeID) {
            auto thumbnail = theme_id_to_cached_thumbnail_path(*meta.uthemeMetadata.themeID);
            if (exists(thumbnail))
                DeletePath(thumbnail);
        }

        // If there's legacy metadata, delete it.
        if (!meta.legacyMetadataPath.empty()) {
            if (exists(meta.legacyMetadataPath))
                DeletePath(meta.legacyMetadataPath);
        }

        // Finally, delete the theme path with all its contents.
        DeletePath(meta.themePath);
    }

    std::string
    GetCurrentThemeName()
    {
        auto cfg = GetStyleMiiUCfg();
        if (!cfg)
            return {};
        if (!cfg->themeManagerEnabled)
            return {};
        if (cfg->enabledThemes.size() != 1)
            return {};
        return *cfg->enabledThemes.begin();
    }

    std::optional<InstalledThemeMetadata>
    GetCurrentTheme()
    try {
        auto cfg = GetStyleMiiUCfg();
        if (!cfg)
            return {};
        if (!cfg->themeManagerEnabled)
            return {};
        auto themeName = GetCurrentThemeName();
        cout << "current theme name: \"" << themeName << "\"" << endl;
        if (themeName.empty())
            return {};
        auto themePath = THEMES_ROOT / themeName;
        if (!exists(themePath))
            return {};
        InstalledThemeMetadata result;
        if (!GetInstalledThemeMetadata(themePath, result))
            return {};
        return { std::move(result) };
    }
    catch (std::exception& e) {
        cerr << "ERROR in GetCurrentTheme(): " << e.what() << endl;
        return {};
    }

    std::filesystem::path
    GetThemePath(const UThemeMetadata& meta)
    {
        return THEMES_ROOT / make_theme_folder_name(meta.themeName, meta.themeID);
    }

    bool IsShuffling() {
        auto cfg = GetStyleMiiUCfg();
        if (!cfg)
            return false;
        return cfg->shuffleThemes;
    }

    void ToggleShuffling() {
        auto cfg = GetStyleMiiUCfg();
        if (!cfg)
            return;
        cfg->shuffleThemes = !cfg->shuffleThemes;
        if (!cfg->shuffleThemes)
            cfg->enabledThemes.clear();
    }

    bool IsEnabled(const InstalledThemeMetadata& meta) {
        auto cfg = GetStyleMiiUCfg();
        if (!cfg)
            return false;
        auto theme_str = meta.themePath.filename().string();
        return cfg->enabledThemes.contains(theme_str);
    }

    void Enable(const InstalledThemeMetadata& meta) {
        auto cfg = GetStyleMiiUCfg();
        if (!cfg)
            return;
        auto theme_str = meta.themePath.filename().string();
        if (!cfg->shuffleThemes)
            cfg->enabledThemes.clear();
        cfg->enabledThemes.insert(theme_str);
    }

    void Disable(const InstalledThemeMetadata& meta) {
        auto cfg = GetStyleMiiUCfg();
        if (!cfg)
            return;
        auto theme_str = meta.themePath.filename().string();
        cfg->enabledThemes.erase(theme_str);
    }

    StyleMiiUCfg*
    GetStyleMiiUCfg()
    {
        if (!styleMiiUCfg)
            return nullptr;
        return &*styleMiiUCfg;
    }

    void
    DeleteStyleMiiUCfg()
    {
        try {
            auto cfg_path = GetStyleMiiUConfigPath();
            if (exists(cfg_path))
                remove(cfg_path);
            styleMiiUCfg.emplace();
        }
        catch (std::exception& e) {
            cerr << "ERROR in DeleteStyleMiiUCfg(): " << e.what() << endl;
        }
    }

} // namespace ThemeManager
