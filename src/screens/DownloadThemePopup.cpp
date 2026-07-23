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
#include <vector>

#include <imgui.h>
#include <imgui_raii.h>

#include "DownloadThemePopup.h"

#include "../DownloadManager.h"
#include "../humanize.hpp"
#include "../IconsFontAwesome4.h"
#include "../PluginManager.h"
#include "../ThemeManager.h"
#include "../utils.h"
#include "InstallThemePopup.h"

using std::cout;
using std::endl;
using namespace std::literals;

namespace DownloadThemePopup {

    namespace {

        enum class State {
            hidden,
            queued,
            confirmation,
            downloading,
            error,
            success,
        };

        State state;

        const std::string popup_id = "Download Theme";
        std::string utheme_url;
        std::filesystem::path utheme_filename;

        std::string transfer_name;
        std::string error_message;

        bool enable_theme = true;

    } // namespace

    void
    open(const ThemezerAPI::WiiuThemeSmall &theme_data) {
        state = State::queued;
        transfer_name = theme_data.name;
        utheme_url = theme_data.downloadUrl;
        utheme_filename = ThemeManager::CalcUThemePath(theme_data.slug, theme_data.hexId);
        error_message.clear();
    }

    void
    open(const std::string& url) {
        state = State::queued;
        transfer_name = "[QR] \"" + url + "\"";
        utheme_url = url;
        utheme_filename = ThemeManager::CalcUThemePath(url);
        error_message.clear();
    }

    void show_confirmation() {
        using namespace ImGui::RAII;

        const auto &style = ImGui::GetStyle();
        {
            Font title_font{nullptr, 35};
            ImGui::Text("Download Confirmation");
        }

        ImGui::TextWrapped("Would you like to download the theme:\n%s ?",
                           transfer_name.c_str());

        // Create two buttons with equal widths.
        const ImVec2 available = ImGui::GetContentRegionAvail();

        const std::string download_label = ICON_FA_DOWNLOAD " Download";
        const std::string cancel_label = ICON_FA_TIMES " Cancel";

        const ImVec2 download_size = ImGui::CalcTextSize(download_label);
        const ImVec2 cancel_size = ImGui::CalcTextSize(cancel_label);

        const ImVec2 button_size = max(download_size, cancel_size) + 2 * style.FramePadding;

        // Place the buttons on the bottom.
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + available.y - button_size.y);

        const float total_width = 2 * button_size.x + style.ItemSpacing.x;

        const float start_x = (available.x - total_width) / 2;

        if (start_x > 0)
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + start_x);

        if (ImGui::Button(download_label, button_size)) {
            state = State::downloading;
            if (!DownloadManager::add(utheme_url,
                                      utheme_filename,
                                      [](const DownloadManager::Info& info)
                                          {
                                              cout << "Finished " << info.filename << endl;
                                              state = State::success;
                                              ThemeManager::RefreshUThemes();
                                          },
                                          [](const std::exception& e)
                                          {
                                              state = State::error;
                                              error_message = e.what();
                                          })) {
                    state = State::error;
                    error_message = "Failed to queue download transfer";
                }
        }
        ImGui::SetItemDefaultFocus();

        ImGui::SameLine();

        if (ImGui::Button(cancel_label, button_size)) {
            ImGui::CloseCurrentPopup();
            state = State::hidden;
        }
    }

    void show_downloading() {
        using namespace ImGui::RAII;
        {
            Font title_font{nullptr, 35};
            ImGui::Text("Downloading Theme...");
        }

        ImGui::TextWrapped(transfer_name);

        ImGui::TextWrapped(utheme_url);

        ImGui::TextWrapped("Saving to: %s", utheme_filename.filename().c_str());

        auto info = DownloadManager::get_info(utheme_url);

        //auto speed = humanize::value_bin(info->speed) + "B/s";
        //ImGui::Text("DL speed: %s", speed.data());

        // Place the progress bar on the bottom.
        ImGui::SetCursorPosY(ImGui::GetCursorPosY()
                             + ImGui::GetContentRegionAvail().y
                             - ImGui::GetFrameHeight());

        ImGui::ProgressBar(info->progress);

        // TODO: downloads should be cancelable, should have a cancel button here.
    }

    void show_success() {
        using namespace ImGui::RAII;

        {
            Font title_font{nullptr, 50};
            ImGui::Text("Download successful!");
        }

        ImGui::TextWrapped("Would you like to install this theme for the StyleMiiU plugin?");

        std::string enable_label = (PluginManager::IsShuffling() ? "Enable"s : "Apply"s)
            + " theme after installation"s;
        ImGui::Checkbox(enable_label, enable_theme);

        // Make two buttons of equal size.
        const std::string install_label = ICON_FA_COGS " Install";
        const std::string cancel_label = ICON_FA_TIMES " Cancel";
        const ImVec2 install_size = ImGui::CalcTextSize(install_label);
        const ImVec2 cancel_size = ImGui::CalcTextSize(cancel_label);

        const auto &style = ImGui::GetStyle();
        const ImVec2 button_size = max(install_size, cancel_size) + 2 * style.FramePadding;

        const ImVec2 available = ImGui::GetContentRegionAvail();
        // Place the buttons on the bottom.
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + available.y - button_size.y);

        const float total_width = 2 * button_size.x + style.ItemSpacing.x;

        const float start_x = (available.x - total_width) / 2;
        if (start_x > 0.0f)
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + start_x);

        if (ImGui::Button(install_label, button_size)) {
            ImGui::CloseCurrentPopup();
            state = State::hidden;
            // TODO: report error if a .utheme is missing metadata.
            if (auto meta = ThemeManager::ReadUThemeMetadata(utheme_filename))
                InstallThemePopup::open(utheme_filename, meta, true, enable_theme);
        }
        ImGui::SetItemDefaultFocus();

        ImGui::SameLine();

        if (ImGui::Button(cancel_label, button_size)) {
            ImGui::CloseCurrentPopup();
            state = State::hidden;
        }
    }

    void show_error() {
        ImGui::Text("ERROR!");
        ImGui::TextWrapped(error_message);

        if (ImGui::Button("Close")) {
            DownloadManager::clear_all();
            state = State::hidden;
        }
    }

    void process_ui() {
        using namespace ImGui::RAII;
        if (state == State::hidden)
            return;

        if (state == State::queued) {
            // NOTE: open popup before we create the popup.
            ImGui::OpenPopup(popup_id);
            state = State::confirmation;
        }

        auto viewport = ImGui::GetMainViewport();
        // WORKAROUND: setting an initial size helps with the initial position not jumping around.
        ImGui::SetNextWindowSize(viewport->Size * 0.7f, ImGuiCond_Appearing);
        auto center = viewport->GetCenter();
        ImGui::SetNextWindowPos(center, ImGuiCond_Always, {0.5f, 0.5f});
        PopupModal popup{popup_id, nullptr,
                         ImGuiWindowFlags_NoSavedSettings |
                         ImGuiWindowFlags_NoMove |
                         ImGuiWindowFlags_NoCollapse |
                         ImGuiWindowFlags_NoTitleBar |
                         ImGuiWindowFlags_NoResize};

        if (!popup) {
            state = State::hidden;
            return;
        }

        switch (state) {
            case State::confirmation:
                show_confirmation();
                break;

            case State::downloading:
                show_downloading();
                break;

            case State::error:
                show_error();
                break;

            case State::success:
                show_success();
                break;

            default:
                ;
        }

    }
}
