/*
 * Themiify - A theme manager for the Nintendo Wii U
 * Copyright (C) 2026 Fangal-Airbag
 * Copyright (C) 2026 AlphaCraft9658
 * Copyright (C) 2026 Daniel K. O. <dkosmari>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdio>
#include <filesystem>
#include <format>
#include <fstream>
#include <future>
#include <iostream>
#include <map>
#include <print>
#include <ranges>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#include <glaze/exceptions/json_exceptions.hpp>
#include <glaze/json.hpp>
#include <hips.hpp>
#include <mocha/mocha.h>

#include <coreinit/systeminfo.h>
#include <sysapp/title.h>

#include "ThemeManager.h"

#include "PluginManager.h"
#include "thread_safe.hpp"
#include "tracer.hpp"
#include "utils.h"
#include "async_queue.hpp"
#include "Zip.h"

using std::cout;
using std::cerr;
using std::endl;
using namespace std::literals;

// Note: to avoid errors, we rename "metadata" to "Metadata" by customizing the glaze
// metadata.
namespace {
    struct MetadataJson {
        ThemeManager::Metadata metadata;
    };
}

// The built-in glz::pascal_case transformer will make the first letter uppercase.
template<>
struct glz::meta<MetadataJson> : glz::pascal_case {};

namespace ThemeManager {

    namespace {

        // ----------------- //
        // Type declarations //
        // ----------------- //

        enum class RefreshThemesState : unsigned {
            idle,
            working,
        };

        struct InstallContext {
            const std::filesystem::path utheme_path;
            std::optional<Metadata> metadata;
            const bool enable_theme;
            ProgressFunction progress_func;
            SuccessFunction success_func;
            ErrorFunction error_func;
            std::filesystem::path theme_path;

            InstallContext(const std::filesystem::path& utheme_path,
                           const Metadata& metadata,
                           bool enable_theme,
                           ProgressFunction progress_func,
                           SuccessFunction success_func,
                           ErrorFunction error_func);
        };

        // RAII type to control Home Button Menu state.
        struct HomeButtonMenuDisabler {

            HomeButtonMenuDisabler()
                noexcept;

            ~HomeButtonMenuDisabler()
                noexcept;

        }; // struct HomeButtonMenuDisabler

        // RAII type to clean up theme installation on error.
        struct InstallCleaner {

            InstallCleaner(const std::filesystem::path& theme_path);

            ~InstallCleaner()
                noexcept;

            void
            disable();

        private:

            const std::filesystem::path theme_path;
            bool disabled = false;

        }; // struct InstallCleaner

        // ------------ //
        // Type aliases //
        // ------------ //

        using ThemesMap = std::map<std::filesystem::path, ConstThemePtr, IgnoreCaseLess>;

        using Task = std::future<void>;

        using InstallContextPtr = std::shared_ptr<InstallContext>;

        // --------- //
        // Variables //
        // --------- //

        const std::unordered_map<std::string, std::filesystem::path> regionLangMap = {
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

        thread_safe<ThemesMap> safe_themes_map;

        std::atomic<RefreshThemesState> refresh_themes_state{RefreshThemesState::idle};

        std::jthread refresh_themes_thread;

        async_queue<Task> pending_tasks;

        std::jthread install_thread;

        // --------------------- //
        // Function declarations //
        // --------------------- //

        // TODO: keep them sorted

        void
        TaskReportInstallProgress(InstallContextPtr ctx,
                                  std::string msg);
        void
        TaskReportInstallError(InstallContextPtr ctx,
                               std::string msg);

        std::filesystem::path GetMenuContentPath();

        void
        InstallThread(std::stop_token stopper,
                      InstallContextPtr ctx)
            noexcept;

        template<typename F,
                 typename... Args>
        void
        add_task(F&& func,
                 Args&& ...args);

        void
        exec_tasks();

        std::string
        to_string(Hips::Result res);

        void
        CreateCacheFile(std::ifstream &sourceFile,
                        const std::filesystem::path &outputPath);

        // -------------------- //
        // Function definitions //
        // -------------------- //

        // TODO: keep them sorted

        InstallContext::InstallContext(const std::filesystem::path& utheme_path,
                                       const Metadata& metadata,
                                       bool enable_theme,
                                       ProgressFunction progress_func,
                                       SuccessFunction success_func,
                                       ErrorFunction error_func) :
            utheme_path{utheme_path},
            metadata{metadata},
            enable_theme{enable_theme},
            progress_func{std::move(progress_func)},
            success_func{std::move(success_func)},
            error_func{std::move(error_func)}
        {}

        HomeButtonMenuDisabler::HomeButtonMenuDisabler()
            noexcept
        {
            OSEnableHomeButtonMenu(false);
        }

        HomeButtonMenuDisabler::~HomeButtonMenuDisabler()
            noexcept
        {
            OSEnableHomeButtonMenu(true);
        }

        InstallCleaner::InstallCleaner(const std::filesystem::path& theme_path) :
            theme_path{theme_path}
        {}

        InstallCleaner::~InstallCleaner()
            noexcept
        {
            if (!disabled) {
                cerr << "Deleting partially installed theme: " << theme_path << endl;
                DeletePath(theme_path);
            }
        }

        void
        InstallCleaner::disable()
        {
            disabled = true;
        }

        void
        TaskReportInstallError(InstallContextPtr ctx,
                               std::string msg)
        {
            cerr << "TaskReportInstallError: " << msg << endl;
            if (ctx->error_func)
                ctx->error_func(msg);
        }

        void
        TaskReportInstallProgress(InstallContextPtr ctx,
                                  std::string msg)
        {
            cout << "TaskReportInstallProgress: " << msg << endl;
            if (ctx->progress_func)
                ctx->progress_func(msg);
        }

        void
        TaskReportInstallSuccess(InstallContextPtr ctx)
        {
            cout << "TaskReportInstallSuccess: " << ctx->metadata->themeName << endl;
            if (ctx->enable_theme)
                PluginManager::Enable(ctx->theme_path);
            if (ctx->success_func)
                ctx->success_func();
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


        void
        InstallThread(std::stop_token stopper,
                      InstallContextPtr ctx)
            noexcept
        try {
            if (!ctx->metadata) {
                ctx->metadata = ReadUThemeMetadata(ctx->utheme_path);
                if (!ctx->metadata)
                    throw std::runtime_error{"could not get utheme metadata"};
            }

            ctx->theme_path = CalcThemePath(*ctx->metadata);
            const auto& themePath = ctx->theme_path;

            // Helper lambda
            auto reportProgress =
                [&ctx]<typename... Args>(std::format_string<Args...> fmt,
                                         Args&&... args)
            {
                std::string msg = std::format(fmt, std::forward<Args>(args)...);
                add_task(TaskReportInstallProgress, ctx, std::move(msg));
            };

            // Helper lambda
            auto throwIfStopped = [&stopper] {
                if (stopper.stop_requested())
                    throw std::runtime_error{"Installation canceled."};
            };

            HomeButtonMenuDisabler hbm_disabler;
            InstallCleaner cleaner{themePath};

            auto menuContentPath = GetMenuContentPath();

            throwIfStopped();

            Zip::Archive uthemeArchive{ctx->utheme_path, ZIP_RDONLY};

            throwIfStopped();

            reportProgress("Installing {}...", ctx->metadata->themeName);
            reportProgress("Installing theme to: \"{}\"", themePath.string());

            const std::string AllMessage_ = "AllMessage_";
            const std::uint64_t numEntries = uthemeArchive.get_num_entries(ZIP_FL_UNCHANGED);
            for (std::uint64_t i = 0; i < numEntries; ++i) {

                throwIfStopped();

                std::filesystem::path entryName = uthemeArchive.get_name(i, ZIP_FL_ENC_RAW);
                std::filesystem::path menuFilePath;
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
                        reportProgress("Unknown AllMessage Region and Language: \"{}\"",
                                       regionLangStr);
                        continue;
                    }

                    menuFilePath = it->second;
                }

                if (!menuFilePath.empty()) {
                    // Found a known patch.
                    reportProgress("Handling patch for \"{}\"", menuFilePath.string());

                    auto menuPath = menuContentPath / menuFilePath;
                    auto cachePath = THEMIIFY_ROOT / "cache" / menuFilePath;
                    auto patchPath = entryName;
                    auto outputPath = themePath / "content" / menuFilePath;

                    CreateParentDirectories(cachePath);

                    auto patchFile = uthemeArchive.fopen(i);

                    zip_stat_t patchStatData = uthemeArchive.stat(i);

                    throwIfStopped();

                    auto patchSize = patchStatData.size;

                    std::ifstream inputFile(cachePath, std::ios::binary | std::ios::ate);
                    if (!inputFile.is_open()) {
                        reportProgress("Cache does not exist for \"{}\"",
                                       menuPath.string());

                        inputFile.clear();
                        inputFile.open(menuPath, std::ios::binary | std::ios::ate);

                        if (!inputFile.is_open()) {
                            inputFile.clear();
                            // NOTE: don't stop installation, just report and continue
                            reportProgress("Could not open source file for \"{}\"",
                                           patchPath.string());
                            continue;
                        }
                        reportProgress("Creating cache for \"{}\"", menuPath.string());
                        CreateCacheFile(inputFile, cachePath);
                    }
                    else {
                        reportProgress("Found \"{}\" in cache at \"{}\"",
                                       menuFilePath.string(),
                                       cachePath.string());
                    }

                    throwIfStopped();

                    auto inputSize = inputFile.tellg();
                    // if (inputSize != inputFile.tellg())
                    //     throw std::runtime_error{"Input file is too large."};
                    inputFile.seekg(0, std::ios::beg);

                    throwIfStopped();

                    std::vector<uint8_t> patchData(patchSize);
                    std::vector<uint8_t> inputData(inputSize);

                    if (patchFile.read(std::span{patchData}) != patchData.size())
                        throw std::runtime_error{"Could not read the whole patch"};

                    patchFile.close();

                    throwIfStopped();

                    inputFile.read(reinterpret_cast<char*>(inputData.data()), inputData.size());
                    if (std::cmp_not_equal(inputFile.gcount(), inputData.size()))
                        throw std::runtime_error{"Could not read the whole input"};
                    inputFile.close();

                    throwIfStopped();

                    auto [bytes, result] = Hips::patch(
                        inputData.data(),
                        inputData.size(),
                        patchData.data(),
                        patchData.size(),
                        Hips::PatchType::BPS
                    );

                    reportProgress("Patch processed");

                    throwIfStopped();

                    if (result == Hips::Result::Success) {
                        CreateParentDirectories(outputPath);

                        throwIfStopped();

                        std::ofstream outputFile(outputPath, std::ios::binary);
                        outputFile.write(reinterpret_cast<char*>(bytes.data()), bytes.size());
                        outputFile.close();

                        reportProgress("File written to \"{}\"", outputPath.string());
                    }
                    else {
                        throw std::runtime_error{"Patch failed: " + to_string(result)};
                    }
                }
                else {
                    // Not a known patch: if it's not a .bps file, just copy it verbatim
                    // to themePath.
                    if (entryName.extension() != ".bps") {
                        reportProgress("Copying file: \"{}\"", entryName.string());
                        auto patchFile = uthemeArchive.fopen(i);
                        auto outputPath = themePath / entryName;
                        if (outputPath.has_parent_path())
                            create_directories(outputPath.parent_path());
                        std::filebuf output;
                        if (!output.open(outputPath,
                                         std::ios::out | std::ios::binary | std::ios::trunc))
                            throw std::runtime_error{"Could not create output file: \""s
                                                     + outputPath.string() + "\""s};
                        std::vector<char> buf(65536);
                        std::uint64_t total = 0;
                        while (auto read = patchFile.read(std::span{buf})) {
                            output.sputn(buf.data(), read);
                            total += read;
                        }
                        output.close();
                        patchFile.close();
                        reportProgress("File written to \"{}\" ({} bytes)",
                                       outputPath.string(),
                                       total);
                    }
                }
            }

            uthemeArchive.close();

            throwIfStopped();

            if (auto theme = ReadInstalledTheme(themePath))
                safe_themes_map.lock()
                    ->emplace(themePath.filename(), std::make_shared<Theme>(std::move(*theme)));
            else
                throw std::runtime_error{"Could not read metadata from installed theme!"};

            cleaner.disable();

            add_task(TaskReportInstallSuccess, ctx);
        }
        catch (std::exception& e) {
            std::string msg = e.what();
            add_task(TaskReportInstallError, std::move(ctx), std::move(msg));
        }

        template<typename F,
                 typename... Args>
        void
        add_task(F&& func,
                 Args&& ...args)
        {
            pending_tasks.push(std::async(std::launch::deferred,
                                          std::forward<F>(func),
                                          std::forward<Args>(args)...));
        }

        void
        exec_tasks()
        {
            while (auto task = pending_tasks.try_pop())
                task->get();
        }

        std::string
        to_string(Hips::Result res)
        {
            switch (res) {
                using enum Hips::Result;
                case Success:
                    return "success";
                case InvalidPatch:
                    return "invalid patch";
                case UnknownFormat:
                    return "unknown format";
                case SizeMismatch:
                    return "size mismatch";
                case ChecksumMismatch:
                    return "checksum mismatch";
                default:
                    return "unknown error";
            }
        }

        void
        CreateCacheFile(std::ifstream &sourceFile,
                             const std::filesystem::path &outputPath)
        {
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

    } // namespace


    void initialize()
    {
        TRACE_FUNC;

        refresh_themes_thread = {};
        refresh_themes_state = RefreshThemesState::idle;

        RefreshInstalledThemes();

        install_thread = {};
    }

    void finalize()
    {
        TRACE_FUNC;

        install_thread = {};
        refresh_themes_thread = {};
    }

    void process()
    {
        exec_tasks();
    }

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

    std::optional<Metadata>
    ReadUThemeMetadata(const std::filesystem::path &uthemePath)
    try {
        Zip::Archive uthemeArchive{uthemePath, ZIP_RDONLY};
        auto metadataStatData = uthemeArchive.stat("metadata.json", ZIP_FL_NOCASE);
        std::string buffer(metadataStatData.size, '\0');
        auto themeMetadataFile = uthemeArchive.fopen("metadata.json", ZIP_FL_NOCASE);
        if (themeMetadataFile.read(std::span{buffer}) != buffer.size())
            throw std::runtime_error{"could not read all metadata"};
        themeMetadataFile.close();
        uthemeArchive.close();
        MetadataJson json;
        glz::ex::read_json(json, buffer);
        return { std::move(json.metadata) };
    }
    catch (std::exception& e) {
        cerr << "ERROR: " << e.what() << endl;
        return {};
    }

    std::optional<Theme>
    ReadInstalledTheme(const std::filesystem::path &themePath)
    {
        static const std::array image_extensions{".webp", ".jpg", ".png"};
        try {
            Theme theme;
            theme.path = themePath;
            try {
                for (auto& entry
                         : std::filesystem::recursive_directory_iterator{theme.path}) {
                    if (entry.is_regular_file())
                        theme.files.push_back(entry.path());
                }
                std::ranges::sort(theme.files, IgnoreCaseLess{});
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
                auto preview_base = theme.path / name;
                for (auto ext : image_extensions) {
                    auto preview = preview_base;
                    preview += ext;
                    if (exists(preview)) {
                        theme.previews.push_back(preview);
                        break;
                    }
                }
            }
            MetadataJson metaJson;
            glz::ex::read_file_json(metaJson,
                                    (theme.path / "metadata.json").string(),
                                    std::string{});
            theme.metadata = std::move(metaJson.metadata);

            // If there was no preview image on the theme folder, look for a cached thumbnail.
            if (theme.path.empty() && theme.metadata.themeID) {
                auto preview = theme_id_to_cached_thumbnail_path(*theme.metadata.themeID);
                if (exists(preview))
                    theme.previews.push_back(preview);
            }

            return {std::move(theme)};
        }
        catch (std::exception& e) {
            // cout << "Failed to get new theme metadata: " << e.what() << endl;
            // Fallback: find a json (in SD:/themiify/installed/) that matches this theme.
            try {
                auto folder_name = themePath.filename();
                Theme theme;
                theme.path = themePath;
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
                            theme.metadata.themeID = leg_meta.themeID;
                            theme.metadata.themeName = leg_meta.themeName;
                            theme.metadata.themeAuthor = leg_meta.themeAuthor;
                            theme.metadata.themeVersion = leg_meta.themeVersion;
                            theme.legacyMetadataPath = entry.path();
                            auto preview =
                                theme_id_to_cached_thumbnail_path(*theme.metadata.themeID);
                            if (exists(preview))
                                theme.previews.push_back(preview);
                            return {std::move(theme)};
                        }
                    }
                    catch (std::exception& e3) {
                        cout << "Failed to parse " << entry.path()
                             << ": " << e3.what() << endl;
                    }
                }
                // No matching metadata found, just use the folder name as the name.
                theme.metadata.themeName = folder_name;
                return {std::move(theme)};
            }
            catch (std::exception& e2) {
                cerr << "ERROR: while looking for themiify metadata: " << e2.what() << endl;
                return {};
            }
            return {};
        }
    }

    void
    Install(const std::filesystem::path& utheme,
            const Metadata& metadata,
            bool enable_theme,
            ProgressFunction progress_func,
            SuccessFunction success_func,
            ErrorFunction error_func)
    {
        auto ctx = std::make_shared<InstallContext>(utheme,
                                                    metadata,
                                                    enable_theme,
                                                    std::move(progress_func),
                                                    std::move(success_func),
                                                    std::move(error_func));
        install_thread = std::jthread{InstallThread, std::move(ctx)};
    }

    void
    CancelInstall()
    {
        install_thread = {};
    }

    void
    Uninstall(ConstThemePtr theme)
    {
        PluginManager::Disable(theme->path);

        safe_themes_map.lock()
            ->erase(theme->path.filename());

        // If there's a cached thumbnail, delete it.
        if (theme->metadata.themeID) {
            auto thumbnail = theme_id_to_cached_thumbnail_path(*theme->metadata.themeID);
            if (exists(thumbnail))
                DeletePath(thumbnail);
        }

        // If there's legacy metadata, delete it.
        auto& leg_meta = theme->legacyMetadataPath;
        if (!leg_meta.empty()) {
            if (exists(leg_meta))
                DeletePath(leg_meta);
        }

        // Finally, delete the theme path with all its contents.
        DeletePath(theme->path);
    }

    std::filesystem::path
    CalcThemePath(const Metadata& meta)
    {
        return THEMES_ROOT / make_theme_folder_name(meta.themeName, meta.themeID);
    }

    void
    RefreshInstalledThemes()
    {
        refresh_themes_state = RefreshThemesState::working;
        safe_themes_map.lock()->clear();
        refresh_themes_thread = std::jthread{
            [](std::stop_token stopper)
            {
                try {
                    for (auto& entry : std::filesystem::directory_iterator{THEMES_ROOT}) {
                        if (stopper.stop_requested())
                            break;
                        if (!entry.is_directory())
                            continue;
                        if (auto theme = ReadInstalledTheme(entry.path()))
                            safe_themes_map.lock()->emplace(
                                entry.path().filename(),
                                std::make_shared<Theme>(std::move(*theme)));
                    }
                }
                catch (std::exception& e) {
                    cerr << "ERROR in RefreshInstalledThemes(): " << e.what() << endl;
                }
                refresh_themes_state = RefreshThemesState::idle;
            }
        };
    }

    bool
    IsRefreshingThemes()
    {
        return refresh_themes_state != RefreshThemesState::idle;
    }

    void
    ForEachInstalledTheme(const ThemeFunction& func)
    {
        auto themes = safe_themes_map.lock();
        for (auto [index, path_theme] : *themes | std::views::enumerate)
            func(index, path_theme.second);
    }

    ConstThemePtr
    GetCurrentTheme()
        noexcept
    {
        auto name = PluginManager::GetCurrentTheme();
        if (name.empty())
            return {};
        auto themes = safe_themes_map.lock();
        auto it = themes->find(name);
        if (it == themes->end())
            return {};
        return it->second;
    }

} // namespace ThemeManager
