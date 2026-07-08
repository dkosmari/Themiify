/*
 * Themiify - A theme manager for the Nintendo Wii U
 * Copyright (C) 2026 Fangal-Airbag
 * Copyright (C) 2026 AlphaCraft9658
 * Copyright (C) 2026  Daniel K. O. <dkosmari>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <algorithm>
#include <atomic>
#include <iostream>
#include <optional>
#include <ranges>
#include <thread>

#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>

#include <imgui.h>
#include <imgui_raii.h>

#include "ManageThemesScreen.h"
#include "HomeScreen.h"
#include "InstallThemePopup.h"
#include "ThemeDetailsPopup.h"
#include "DeleteThemePopup.h"
#include "../installer.h"
#include "../utils.h"
#include "../IconsFontAwesome4.h"
#include "../ImageLoader.h"
#include "../tracer.hpp"
#include "../thread_safe.hpp"

// Define this to help seeing the padding and spacing values for windows.
// #define DEBUG_BG_COLOR

using std::cout;
using std::endl;
using namespace std::literals;

namespace ManageThemesScreen {

    using Installer::InstalledThemeMetadata;

    enum class Tab {
        manage_installed,
        install_local,
    };

    Tab current_tab = Tab::manage_installed;

    std::vector<std::filesystem::path> local_uthemes;
    bool local_uthemes_needs_refresh = true;

    std::jthread installed_themes_scanner;
    thread_safe<std::vector<InstalledThemeMetadata>> safe_installed_themes;

    enum class InstalledThemesScanState {
        idle,
        requested,
        scanning,
    };
    std::atomic<InstalledThemesScanState> installed_themes_scan_state{
        InstalledThemesScanState::requested
    };

    SDL_Renderer *manage_renderer;

    std::filesystem::path thumbnail_path;

    std::string search;

    int similarity_score(std::string haystack, std::string needle) {
        haystack = as_lower_case(haystack);
        needle = as_lower_case(needle);

        if (needle.empty())
            return 0;

        if (needle == haystack)
            return 10000;

        if (haystack.starts_with(needle))
            return 8000 - static_cast<int>(haystack.size());

        if (haystack.contains(needle))
            return 6000 - static_cast<int>(haystack.find(needle));

        int score = 0;
        std::size_t pos = 0;

        for (char c : needle) {
            pos = haystack.find(c, pos);
            if (pos == std::string::npos)
                return -1;

            score += 10;
            ++pos;
        }

        return score;
    }

    void scan_local_uthemes() {
        local_uthemes.clear();

        for (auto& entry : std::filesystem::directory_iterator(THEMES_ROOT)) {
            if (entry.is_regular_file() && entry.path().extension() == ".utheme") {
                local_uthemes.push_back(entry.path());
            }
        }
        std::ranges::sort(local_uthemes, {}, as_lower_case);
    }

    void scan_installed_themes() {
        installed_themes_scan_state = InstalledThemesScanState::scanning;
        installed_themes_scanner = std::jthread{
            [](std::stop_token stopper)
            {
                auto themes = Installer::GetInstalledThemes(stopper);
                if (!stopper.stop_requested()) {
                    safe_installed_themes.store(std::move(themes));
                    installed_themes_scan_state = InstalledThemesScanState::idle;
                }
            }
        };
    }

    void initialize(SDL_Renderer *renderer) {
        TRACE_FUNC;
        create_directories(THEMES_ROOT);
        create_directories(THEMIIFY_INSTALLED_THEMES);
        manage_renderer = renderer;
        installed_themes_scanner = {};
        safe_installed_themes.lock()->clear();
    }

    void finalize() {
        TRACE_FUNC;
        installed_themes_scanner = {};
        safe_installed_themes.lock()->clear();
    }

    void refresh_all() {
        refresh_installed_themes();
        refresh_local_uthemes();
    }

    void refresh_installed_themes() {
        installed_themes_scan_state = InstalledThemesScanState::requested;
    }

    void refresh_local_uthemes()
    {
        local_uthemes_needs_refresh = true;
    }

    static void text_limited(float width, const std::string& text) {
        // WORKAROUND: prevent tooltip.
        auto& io = ImGui::GetIO();
        auto old_mouse_pos = io.MousePos;
        ImGui::TextAligned(0.0f, width, text);
        io.MousePos = old_mouse_pos;
    }

    void show_installed_theme(const Installer::InstalledThemeMetadata& theme_data,
                              const ImVec2& inner_size,
                              const ImVec2& padding) {
        // NOTE: to create a complex button, we create a button with no text, then overlap
        // the contents.
        using namespace ImGui::RAII;

        ID id{theme_data.themePath.string()};

        const auto& style = ImGui::GetStyle();
        const ImVec2 outer_size = inner_size + 2 * padding;

        // Put everything inside a child window so we can bail out when not visibile.
        Child container{"container", outer_size,
                        ImGuiChildFlags_NavFlattened};
        if (!container)
            return;

        const ImVec2 start_pos = padding;

        bool clicked = false;
        ImGui::SetCursorPos({0, 0});
        if (ImGui::Button("##button", outer_size)) {
            // NOTE: delay opening the popup, gotta check if the user clicked on the star.
            clicked = true;
        }

        // NOTE: when hovered or activated, make text light.
        std::optional<StyleColor> dark_text;
        if (ImGui::IsItemHovered() || ImGui::IsItemActive()) {
            const auto& colors = style.Colors;
            auto dark_color = colors[ImGuiCol_WindowBg];
            dark_text.emplace(ImGuiCol_Text, dark_color);
        }

        ImGui::SetCursorPos(start_pos);
        Group grp;

        if (!theme_data.previewPaths.empty()) {
            StyleVar no_border{ImGuiStyleVar_ImageBorderSize, 0};
            auto img = ImageLoader::get(theme_data.previewPaths.front());
            ImVec2 img_size = {inner_size.x, inner_size.x * 9.0f / 16.0f};
            ImGui::Image((ImTextureID)img, img_size);
        }

        bool is_shuffling = Installer::IsShuffling();
        bool is_active = Installer::IsActive(theme_data);

        // NOTE: Measure size for the active marker, but don't place it yet, to not mess
        // with the cursor position.

        const std::string active_label = is_shuffling
            ? (is_active ? ICON_FA_CHECK_CIRCLE_O : ICON_FA_CIRCLE_O)
            : (is_active ? ICON_FA_STAR : ICON_FA_STAR_O);

        const float active_font_size = 48;
        ImVec2 active_size;
        {
            Font active_font{nullptr, active_font_size};
            active_size = ImGui::CalcTextSize(active_label);
        }

        {
            Font font{nullptr, 24};
            // If there's an image, make sure to limit the width, so it doesn't get
            // covered by the star.
            float name_width = inner_size.x;
            if (!theme_data.previewPaths.empty())
                name_width -= active_size.x + style.ItemSpacing.x;
            text_limited(name_width, theme_data.uthemeMetadata.themeName);
        }

        if (theme_data.uthemeMetadata.themeAuthor) {
            Font font{nullptr, 18};
            float author_width = inner_size.x;
            // If there's an image, make sure to limit the width, so it doesn't get
            // covered by the star.
            if (!theme_data.previewPaths.empty())
                author_width -= active_size.x + style.ItemSpacing.x;
            text_limited(author_width, "by " + *theme_data.uthemeMetadata.themeAuthor);
        }

        // Put active marker on bottom right.
        {
            Font active_font{nullptr, active_font_size};
            StyleColor active_color{ImGuiCol_Text, {1.0f, 0.9f, 0.0f, 1.0f}};
            ImGui::SetCursorPos(inner_size - active_size);
            ImGui::Text(active_label);
            if (clicked && ImGui::IsItemHovered()) {
                clicked = false; // cancel the click
                if (is_active) {
                    Installer::UnsetActive(theme_data);
                } else {
                    Installer::SetActive(theme_data);
                }
                HomeScreen::force_refresh();
            }
        }

        if (clicked)
            ThemeDetailsPopup::open_local(theme_data);
    }

    void show_utheme(const std::filesystem::path& utheme_path) {
        using namespace ImGui::RAII;

        Child theme_frame{utheme_path.string(),
                          {0, 0},
                          ImGuiChildFlags_NavFlattened |
                          ImGuiChildFlags_AutoResizeY |
                          ImGuiChildFlags_FrameStyle,
                          ImGuiWindowFlags_NoSavedSettings};

        if (!theme_frame)
            return;

        const auto &style = ImGui::GetStyle();

        const std::string install_label = ICON_FA_COGS " Install";
        const std::string remove_label = ICON_FA_TRASH;

        auto install_size = ImGui::CalcTextSize(install_label) + 2 * style.FramePadding;
        auto remove_size = ImGui::CalcTextSize(remove_label) + 2 * style.FramePadding;

        const float text_wrap_pos =
            ImGui::GetCursorPosX()
            + ImGui::GetContentRegionAvail().x
            - style.ItemSpacing.x
            - install_size.x
            - style.ItemSpacing.x
            - remove_size.x;
        {
            TextWrapPos wrap_at{text_wrap_pos};
            ImGui::AlignTextToFramePadding();
            ImGui::Text(utheme_path.filename().string());
        }

        ImGui::SameLine();

        ImGui::SetCursorPosX(text_wrap_pos + style.ItemSpacing.x);

        if (ImGui::Button(install_label, install_size)) {
            Installer::UThemeMetadata theme_data;
            Installer::GetUThemeMetadata(utheme_path, theme_data);
            InstallThemePopup::open(utheme_path, theme_data, false, true);
        }

        ImGui::SameLine();

        if (ImGui::Button(remove_label, remove_size)) {
            DeletePath(utheme_path);
            refresh_local_uthemes();
        }
    }

    void show_tab_manage_installed() {
        using namespace ImGui::RAII;

        const auto &style = ImGui::GetStyle();

        SDL_WiiUSetSWKBDHintText("Write some search terms...");
        SDL_WiiUSetSWKBDOKLabel("Search");
        SDL_WiiUSetSWKBDHighlightInitialText(SDL_TRUE);

        // Let search widget expand, leaving space for the buttons.
        const std::string shuffle_label = "Shuffle";
        const float shuffle_width =
            ImGui::CalcTextSize(shuffle_label).x +
            style.ItemInnerSpacing.x +
            ImGui::GetTextLineHeight() + // defines the size of the box
            2 * style.FramePadding.x;

        const std::string enable_all_label = ICON_FA_CHECK_SQUARE_O " All";
        const float enable_all_width =
            ImGui::CalcTextSize(enable_all_label).x +
            2 * style.FramePadding.x;

        const std::string disable_all_label = ICON_FA_SQUARE_O " All";
        const float disable_all_width =
            ImGui::CalcTextSize(disable_all_label).x +
            2 * style.FramePadding.x;

        const float buttons_width =
            shuffle_width +
            style.ItemSpacing.x +
            enable_all_width +
            style.ItemSpacing.x +
            disable_all_width;

        auto available = ImGui::GetContentRegionAvail();
        ImGui::SetNextItemWidth(available.x - style.ItemSpacing.x - buttons_width);
        ImGui::InputTextWithHint("##local_search"s, "Search..."s, search);

        if (installed_themes_scan_state == InstalledThemesScanState::requested) {
            scan_installed_themes();
        }

        std::vector<std::size_t> visible_indexes;

        auto installed_themes = safe_installed_themes.lock();

        for (std::size_t i = 0; i < installed_themes->size(); ++i) {
            int score = similarity_score((*installed_themes)[i].uthemeMetadata.themeName,
                                         search);

            if (search.empty() || score >= 0)
                visible_indexes.push_back(i);
        }

        if (!search.empty()) {
            std::ranges::sort(
                visible_indexes,
                [&](std::size_t a, std::size_t b) {
                    const auto& ta = (*installed_themes)[a];
                    const auto& tb = (*installed_themes)[b];
                    auto sa = similarity_score(ta.uthemeMetadata.themeName, search);
                    auto sb = similarity_score(tb.uthemeMetadata.themeName, search);
                    return sa > sb;
                }
            );
        }

        ImGui::SameLine();

        bool is_shuffling = Installer::IsShuffling();
        if (ImGui::Checkbox("Shuffle", is_shuffling)) {
            Installer::ToggleShuffling();
        }

        if (is_shuffling) {
            ImGui::SameLine();
            if (ImGui::Button(enable_all_label)) {
                auto installed_themes = safe_installed_themes.lock();
                for (auto& theme : *installed_themes)
                    Installer::SetActive(theme);
            }
            ImGui::SameLine();
            if (ImGui::Button(disable_all_label)) {
                auto installed_themes = safe_installed_themes.lock();
                for (auto& theme : *installed_themes)
                    Installer::UnsetActive(theme);
            }
        }

        // To keep the search widget visible, put the search results inside
        // another child.
        if (Child search_results{"ThemeGrid"}) {

            if (installed_themes_scan_state == InstalledThemesScanState::idle) {

                const ImVec2 grid_start_pos = ImGui::GetCursorPos();
                const ImVec2 inner_size = {320, 260};
                const ImVec2 padding = {12, 12};
                const ImVec2 outer_size = inner_size + 2 * padding;
                const ImVec2 spacing = {18, 18};

                for (auto [counter, index] : visible_indexes | std::views::enumerate) {
                    auto& theme_data = (*installed_themes)[index];
                    ImVec2 grid_pos = { float(counter % 3), float(counter / 3) };
                    ImVec2 pos = grid_pos * (outer_size + spacing);
                    ImGui::SetCursorPos(grid_start_pos + pos);
                    show_installed_theme(theme_data,
                                         inner_size,
                                         padding);
                }
            } else {
                ImGui::Text("Scanning installed themes...");
            }
        }
    }

    void show_tab_install_local() {
        ImGui::Text("Install .utheme files from sd:/wiiu/themes here.");

        ImGui::Separator();

        if (local_uthemes_needs_refresh) {
            scan_local_uthemes();
            local_uthemes_needs_refresh = false;
        }

        for (const auto& utheme_path : local_uthemes)
            show_utheme(utheme_path);
    }

    void process_ui() {
        using namespace ImGui::RAII;

        {
            // NOTE: use a scope to contain all the temporary style changes, so they don't
            // leak into the popups at the bottom.
#ifdef DEBUG_BG_COLOR
            StyleColor green_bg{ImGuiCol_ChildBg, {0.0, 0.5, 0.0, 1.0}};
#endif
            const auto &style = ImGui::GetStyle();
            // Remove horizontal padding.
            StyleVar no_hori_padding{ImGuiStyleVar_WindowPadding, {0, style.WindowPadding.y}};
            if (Child manage_content{"ManageThemesContent",
                                     {0, 0},
                                     ImGuiChildFlags_NavFlattened |
                                     ImGuiChildFlags_AlwaysUseWindowPadding}) {

                float tab_width = (ImGui::GetContentRegionAvail().x - style.ItemSpacing.x) * 0.50f;
                const float tab_height = ImGui::GetFrameHeight();


                if (ImGui::Selectable("Manage Installed Themes",
                                      current_tab == Tab::manage_installed,
                                      0,
                                      {tab_width, tab_height})) {
                    current_tab = Tab::manage_installed;
                    refresh_installed_themes();
                }

                ImGui::SameLine();

                if (ImGui::Selectable("Install Local Themes",
                                      current_tab == Tab::install_local,
                                      0,
                                      {tab_width, tab_height})) {
                    current_tab = Tab::install_local;
                    refresh_local_uthemes();
                }

                ImGui::Separator();

#ifdef DEBUG_BG_COLOR
                StyleColor brown_bg{ImGuiCol_ChildBg, {0.3, 0.3, 0.0, 1.0}};
#endif
                if (Child tab_contents{"tab_contents",
                                       {0, 0},
                                       ImGuiChildFlags_NavFlattened}) {

                    switch (current_tab) {
                        case Tab::manage_installed:
                            show_tab_manage_installed();
                            break;
                        case Tab::install_local:
                            show_tab_install_local();
                            break;
                    }
                }
            }
        }

        ThemeDetailsPopup::process_ui();
        InstallThemePopup::process_ui();
        DeleteThemePopup::process_ui();
    }

}
