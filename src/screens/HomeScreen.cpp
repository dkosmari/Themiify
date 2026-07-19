/*
 * Themiify - A theme manager for the Nintendo Wii U
 * Copyright (C) 2026 Fangal-Airbag
 * Copyright (C) 2026 AlphaCraft9658
 * Copyright (C) 2026 Daniel K. O. <dkosmari>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "HomeScreen.h"
#include "SettingsScreen.h"
#include "SettingsPopup.h"
#include "../IconsFontAwesome4.h"
#include "../ImageLoader.h"
#include "../NavBar.h"
#include "../PluginManager.h"
#include "../ThemeManager.h"
#include "../utils.h"

#include <iostream>
#include <filesystem>
#include <optional>

#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>

#include <imgui.h>
#include <imgui_raii.h>

#include <mocha/mocha.h>

using std::cout;
using std::cerr;
using std::endl;

// Define this to help seeing the padding and spacing values for windows.
// #define DEBUG_BG_COLOR

namespace HomeScreen {

    using ThemeManager::Theme;

    SDL_Renderer *home_renderer = nullptr;

    SDL_Texture *themiify_logo = nullptr;

    bool isFirstBoot = true;
    bool styleMiiUExists = true;
    bool queueStyleMiiUPrompt = false;

    void initialize(SDL_Renderer *renderer) {
        cout << "Hello from HomeScreen init!" << endl;

        home_renderer = renderer;

        themiify_logo = IMG_LoadTexture(renderer, "fs:/vol/content/ui/themiify-logo.png");

        styleMiiUExists = PluginManager::IsInstalled();
        if (!styleMiiUExists)
            queueStyleMiiUPrompt = true;

        cout << "styleMiiUExists: " << styleMiiUExists << endl;

        isFirstBoot = SettingsScreen::check_is_first_boot();
    }

    void finalize() {
        cout << "Hello from HomeScreen finalize!" << endl;

        if (themiify_logo) {
            SDL_DestroyTexture(themiify_logo);
            themiify_logo = nullptr;
        }
    }

    void show_current_theme() {
        using namespace ImGui::RAII;
        {
            Font font{nullptr, 35};
            ImGui::Text("Your current theme:");
        }

        // const ImVec2 img_size = {640, 360};
        // const ImVec2 img_size = {560, 315};
        const ImVec2 img_size = {512, 288};

        if (Child theme_frame{"theme_frame",
                              {0, 0},
                              ImGuiChildFlags_NavFlattened |
                              ImGuiChildFlags_FrameStyle |
                              ImGuiChildFlags_AutoResizeY,
                              ImGuiWindowFlags_NoSavedSettings}) {

            StyleVar no_border{ImGuiStyleVar_ImageBorderSize, 0};

            auto current_theme = ThemeManager::GetCurrentTheme();

            if (!current_theme) {
                auto cfg = PluginManager::GetConfig();
                if (cfg && cfg->shuffleThemes) {
                    auto img = ImageLoader::get("ui/theme-placeholder-random.png");
                    ImGui::Image((ImTextureID)img, img_size);
                    ImGui::SameLine();
                    ImGui::TextWrapped("StyleMiiU is shuffling themes.");
                } else {
                    auto img = ImageLoader::get("ui/theme-placeholder-no-theme.png");
                    ImGui::Image((ImTextureID)img, img_size);
                    ImGui::SameLine();
                    ImGui::TextWrapped("No theme set.");
                }
            }
            else {
                if (!current_theme->previews.empty()) {
                    auto img = ImageLoader::get(current_theme->previews.front());
                    ImGui::Image((ImTextureID)img, img_size);
                } else {
                    auto img = ImageLoader::get("ui/theme-placeholder-no-preview.png");
                    ImGui::Image((ImTextureID)img, img_size);
                }

                ImGui::SameLine();

                {
                    Group right_group;

                    {
                        Font font_guard{nullptr, 30};
                        ImGui::TextWrapped(current_theme->metadata.themeName);
                    }
                    if (current_theme->metadata.themeAuthor)
                        ImGui::TextWrapped("by: " +
                                           *current_theme->metadata.themeAuthor);
                }
            }
        }
    }

    void show_credits() {
        using namespace ImGui::RAII;
        {
            Font font_guard{nullptr, 45};
            ImGui::Text("Credits:");
        }

        ImGui::Spacing();

        ImGui::Text(ICON_FA_CODE " Developers:");

        ImGui::Indent();
        ImGui::BulletText("Fangal-Airbag");
        ImGui::BulletText("AlphaCraft9658");
        ImGui::BulletText("Daniel K. O. (dkosmari)");
        ImGui::Unindent();

        ImGui::Spacing();

        ImGui::Text(ICON_FA_PAINT_BRUSH " UI Design:");
        ImGui::Indent();
        ImGui::BulletText("Perrohuevo");
        ImGui::BulletText("dewgong");
        ImGui::BulletText("Daniel K. O. (dkosmari)");
        ImGui::Unindent();

        ImGui::Spacing();

        ImGui::Text(ICON_FA_FONT " Fonts:");
        ImGui::Indent();
        ImGui::BulletText("Wii U System Font");
        ImGui::BulletText("FontAwesome");
        ImGui::Unindent();

        ImGui::Spacing();

        ImGui::Text(ICON_FA_MUSIC " Music:");

        ImGui::Indent();
        ImGui::BulletText("OMORI OST - 029 Good For Health, Bad for Imagination");

        ImGui::Indent();
        ImGui::TextLink("https://www.youtube.com/watch?v=XeK_I0XQW6U");
        ImGui::Unindent();

        ImGui::Unindent();

        ImGui::Spacing();

        ImGui::Text(ICON_FA_GITHUB " GitHub:");

        ImGui::Indent();
        ImGui::Bullet();
        ImGui::TextLink("https://github.com/Themiify-hb/Themiify");
        ImGui::Unindent();

        ImGui::Spacing();

        ImGui::Text(ICON_FA_STAR " Special thanks:");

        ImGui::Indent();
        ImGui::BulletText("Juanen100 for the StyleMiiU Aroma Plugin!");
        ImGui::BulletText("The Theme Café Discord mods, devs and founders!");
        ImGui::BulletText("Gatto for the incredible Theme Café docs!");
        ImGui::BulletText("Migush and the whole Themezer team!");
        ImGui::BulletText("All the amazing Wii U theme creators!");
        ImGui::BulletText("And many more!");
        ImGui::Unindent();
    }

    void process_ui() {
        using namespace ImGui::RAII;

        const auto &style = ImGui::GetStyle();
#ifdef DEBUG_BG_COLOR
        StyleColor blue_bg{ImGuiCol_ChildBg, {0.0, 0.5, 0.0, 1.0}};
#endif
        Child home_content{"HomeContent", {0, 0}, ImGuiChildFlags_AlwaysUseWindowPadding};
        if (!home_content)
            return;

        if (themiify_logo) {

            ImGui::Image((ImTextureID)themiify_logo, {520, 124});

            ImGui::SameLine();

            {
                Font font{nullptr, 25};
                ImGui::Text("v%s", THEMIIFY_VERSION);
            }
        }

        {
            Font font{nullptr, 55};
            // Cute lil thing cause why not?
            isFirstBoot ? ImGui::Text("Welcome!") : ImGui::Text("Welcome back!");
        }

        if (queueStyleMiiUPrompt) {
            SettingsPopup::open(SettingsPopup::OpenState::stylemiiu);
            queueStyleMiiUPrompt = false;
        }

        if (styleMiiUExists) {
            // For checking integrity at boot
            SettingsScreen::run_first_boot_check();
            SettingsScreen::run_boot_integrity_check();
        }

        SettingsPopup::process_ui();

#ifdef DEBUG_BG_COLOR
        StyleColor brown_bg{ImGuiCol_ChildBg, {0.3, 0.3, 0.0, 1.0}};
#endif
        if (Child scrollable_content{"scrollable_content"}) {
            show_current_theme();

            if (ImGui::Button("Download Themes"))
                NavBar::set_current_tab(NavBar::Tab::themezer);

            ImGui::SameLine();

            {
                // align next button to the right
                auto available = ImGui::GetContentRegionAvail();
                std::string label = "Manage Installed Themes";
                ImVec2 button_size = ImGui::CalcTextSize(label) + 2 * style.FramePadding;
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + available.x - button_size.x);
                if (ImGui::Button(label, button_size))
                    NavBar::set_current_tab(NavBar::Tab::manage_themes);
            }

            ImGui::Separator();

            show_credits();
        }
    }

} // namespace HomeScreen
