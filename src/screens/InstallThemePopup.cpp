/*
 * Themiify - A theme manager for the Nintendo Wii U
 * Copyright (C) 2026 Fangal-Airbag
 * Copyright (C) 2026 AlphaCraft9658
 * Copyright (C) 2026 Daniel K. O. <dkosmari>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <iostream>
#include <filesystem>
#include <string>

#include <imgui.h>
#include <imgui_raii.h>

#include "InstallThemePopup.h"

#include "../IconsFontAwesome4.h"
#include "../PluginManager.h"
#include "../utils.h"
#include "HomeScreen.h"
#include "ManageThemesScreen.h"

using std::cerr;
using std::cout;
using std::endl;
using namespace std::literals;

namespace InstallThemePopup {

    namespace {

        enum class State {
            hidden,
            confirmation,
            start_install,
            installing,
            error,
            success,
        };

        State state = State::hidden;

        bool popup_queued;
        const std::string popup_id = "Install Theme"s;

        std::filesystem::path utheme_path;
        ThemeManager::Metadata theme_meta;
        bool enable_theme = true;

        std::vector<std::string> progress_messages;
        std::string error_message;
        bool scroll_to_bottom;

        void
        progress_handler(const std::string& msg)
        {
            cout << "PROGRESS: " << msg << endl;
            progress_messages.push_back(msg);
            scroll_to_bottom = true;
        }

        void
        success_handler()
        {
            state = State::success;
            scroll_to_bottom = true;
        }

        void
        error_handler(const std::string& msg)
        {
            cerr << "ERROR: " << msg << endl;
            error_message = msg;
            state = State::error;
            scroll_to_bottom = true;
        }

        void
        show_messages()
        {
            using namespace ImGui::RAII;

            // NOTE: take up all available space except for a row of buttons at the bottom.
            ImVec2 size{0.0f, -ImGui::GetFrameHeightWithSpacing()};
            if (Child messages_box{"messages_box",
                                   size,
                                   ImGuiChildFlags_None,
                                   ImGuiWindowFlags_NoSavedSettings}) {

                {
                    Font msg_font{nullptr, 24};

                    // Show progress messages.
                    if (!progress_messages.empty()) {
                        for (const auto& msg : progress_messages)
                            ImGui::TextWrapped(msg);
                        ImGui::Spacing();
                    }

                    // Show error message.
                    {
                        StyleColor red_text{ImGuiCol_Text, {1.0f, 0.25f, 0.25f, 1.0f}};
                        if (!error_message.empty())
                            ImGui::TextWrapped(error_message);
                    }

                    if (scroll_to_bottom) {
                        ImGui::SetScrollHereY(1.0f);
                        scroll_to_bottom = false;
                    }
                }
            }
        }

    } // namespace

    void open(const std::filesystem::path &uthemePath,
              const ThemeManager::Metadata &meta,
              bool skipConfirmation,
              bool enableTheme) {
        create_directories(THEMES_ROOT);

        utheme_path = uthemePath;
        theme_meta = meta;

        if (skipConfirmation)
            state = State::start_install;
        else
            state = State::confirmation;

        enable_theme = enableTheme;
        popup_queued = true;

        progress_messages.clear();
        error_message.clear();
    }

    void show_state_confirmation() {
        const auto &style = ImGui::GetStyle();

        ImGui::TextWrapped("Would you like to install the theme:\n%s ?",
                           theme_meta.themeName.c_str());

        std::string enable_label = (PluginManager::IsShuffling() ? "Enable"s : "Apply"s)
            + " theme after installation"s;
        ImGui::Checkbox(enable_label, enable_theme);

        ImVec2 button_size{180, 0};

        float spacing = style.ItemSpacing.x;
        float total_width = button_size.x * 2.0f + spacing;

        float start_x = (ImGui::GetContentRegionAvail().x - total_width) * 0.5f;

        if (start_x > 0.0f)
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + start_x);

        if (ImGui::Button(ICON_FA_COGS " Install", button_size)) {
            state = State::start_install;
        }
        ImGui::SetItemDefaultFocus();

        ImGui::SameLine();

        if (ImGui::Button(ICON_FA_TIMES " Cancel", button_size)) {
            ImGui::CloseCurrentPopup();
            state = State::hidden;
        }
    }

    void show_state_start_install() {
        state = State::installing;
        ThemeManager::Install(utheme_path,
                              theme_meta,
                              enable_theme,
                              progress_handler,
                              success_handler,
                              error_handler);
    }

    void show_state_installing() {
        using namespace ImGui::RAII;

        // Dummy to have nicer window width here
        ImGui::SetCursorPosX(800.0f);
        ImGui::Dummy({0.0f, 0.0f});
        {
            Font title_font{nullptr, 40};
            ImGui::TextWrapped("Installing %s...", theme_meta.themeName.c_str());
        }

        ImGui::Separator();

        ImGui::TextWrapped("This may take time, do not turn off your Wii U.");

        show_messages();

        ImVec2 button_size{180, 0};
        float button_x = (ImGui::GetContentRegionAvail().x - button_size.x) * 0.5f;
        if (button_x > 0.0f)
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + button_x);
        if (ImGui::Button("Cancel", button_size))
            ThemeManager::CancelInstall();
    }

    void show_state_error() {
        using namespace ImGui::RAII;

        {
            Font title_font{nullptr, 50};
            ImGui::Text("Installation failed!");
        }

        show_messages();

        ImVec2 button_size{180, 0};

        float start_x =
            (ImGui::GetContentRegionAvail().x - button_size.x) * 0.5f;

        if (start_x > 0.0f)
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + start_x);

        if (ImGui::Button("Close", button_size)) {
            ImGui::CloseCurrentPopup();
            state = State::hidden;
        }
    }

    void show_state_success() {
        using namespace ImGui::RAII;

        const auto &style = ImGui::GetStyle();

        {
            Font title_font{nullptr, 50};
            ImGui::Text("Installation successful!");
        }

        ImGui::TextWrapped(std::format("This file is not needed anymore:\n\"{}\".",
                                       utheme_path.filename().string()));
        ImGui::TextWrapped("Would you like to delete it?");

        ImVec2 button_size{180, 0};

        float total_width = 2 * button_size.x + style.ItemSpacing.x;

        float start_x = (ImGui::GetContentRegionAvail().x - total_width) / 2;

        if (start_x > 0.0f)
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + start_x);

        if (ImGui::Button("Delete", button_size)) {
            DeletePath(utheme_path);
            ImGui::CloseCurrentPopup();
            state = State::hidden;
            ManageThemesScreen::refresh_local_uthemes();
        }
        ImGui::SetItemDefaultFocus();

        ImGui::SameLine();

        if (ImGui::Button("Keep", button_size)) {
            ImGui::CloseCurrentPopup();
            state = State::hidden;
            ManageThemesScreen::refresh_local_uthemes();
        }
    }

    void process_ui() {
        using namespace ImGui::RAII;
        if (state == State::hidden)
            return;

        if (popup_queued) {
            ImGui::OpenPopup(popup_id);
            popup_queued = false;
        }

        auto center = ImGui::GetMainViewport()->GetCenter();
        ImGui::SetNextWindowPos(center, ImGuiCond_Always, {0.5f, 0.5f});

        PopupModal popup{
            popup_id,
            nullptr,
            ImGuiWindowFlags_NoSavedSettings |
            ImGuiWindowFlags_AlwaysAutoResize |
            ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoScrollbar |
            ImGuiWindowFlags_NoScrollWithMouse |
            ImGuiWindowFlags_NoCollapse |
            ImGuiWindowFlags_NoTitleBar
        };

        if (!popup) {
            state = State::hidden;
            return;
        }

        switch (state) {
            case State::confirmation:
                show_state_confirmation();
                break;

            case State::start_install:
                show_state_start_install();
                break;

            case State::installing:
                show_state_installing();
                break;

            case State::error:
                show_state_error();
                break;

            case State::success:
                show_state_success();
                break;

            default:
                ;
        }
    }
}
