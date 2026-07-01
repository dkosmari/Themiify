/*
 * Themiify - A theme manager for the Nintendo Wii U
 * Copyright (C) 2026 Fangal-Airbag
 * Copyright (C) 2026 AlphaCraft9658
 * Copyright (C) 2026  Daniel K. O. <dkosmari>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <cmath>
#include <iostream>
#include <filesystem>
#include <string>
#include <vector>

#include <imgui.h>
#include <imgui_raii.h>

#include "DownloadThemePopup.h"
#include "InstallThemePopup.h"
#include "../utils.h"
#include "../DownloadManager.h"
#include "../humanize.hpp"
#include "../IconsFontAwesome4.h"
#include "../installer.h"

using std::cout;
using std::endl;
using namespace std::literals;

namespace DownloadThemePopup {
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
    std::vector<std::string> error_messages;

    struct Transfer {
        std::string url;
        std::filesystem::path filename;
    };

    std::vector<Transfer> transfers;
    std::size_t success_count = 0;
    std::size_t error_count = 0;

    bool set_current = true;

    void open(const ThemezerAPI::WiiuThemeSmall &theme_data) {
        state = State::queued;
        transfer_name = theme_data.name;
        transfers.clear();
        // transfers.emplace_back(theme_data.collagePreview.tinyUrl,
        //                        make_thumbnail_filename(theme_data.hexId));
        transfers.emplace_back(theme_data.downloadUrl,
                               make_utheme_filename(theme_data.slug));
        utheme_url = theme_data.downloadUrl;
        utheme_filename = make_utheme_filename(theme_data.slug);
        error_messages.clear();
        success_count = 0;
        error_count = 0;
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

        const ImVec2 button_size =
            ImVec2{ std::fmax(download_size.x, cancel_size.x),
                    std::fmax(download_size.y, cancel_size.y) }
            + 2 * style.FramePadding;

        // Place the buttons on the bottom.
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + available.y - button_size.y);

        const float total_width = 2 * button_size.x + style.ItemSpacing.x;

        const float start_x = (available.x - total_width) / 2;

        if (start_x > 0)
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + start_x);

        if (ImGui::Button(download_label, button_size)) {
            state = State::downloading;
            for (const auto& transfer : transfers)
                if (!DownloadManager::add(transfer.url,
                                          transfer.filename,
                                          [](const DownloadManager::Info& info)
                                          {
                                              cout << "Finished " << info.filename << endl;
                                              ++success_count;
                                              if (success_count + error_count == transfers.size()) {
                                                  if (error_count == 0)
                                                      state = State::success;
                                                  else
                                                      state = State::error;
                                              }
                                          },
                                          [](const std::exception& e)
                                          {
                                              ++error_count;
                                              error_messages.push_back(e.what());
                                              state = State::error;
                                          })) {
                    state = State::error;
                    error_messages.push_back("Failed to queue download transfer");
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

        if (info->progress >= 1.0f) {
            DownloadManager::clear_finished();
            state = State::success;
        }

        // TODO: downloads should be cancelable, should have a cancel button here.
    }

    void show_success() {
        using namespace ImGui::RAII;

        {
            Font title_font{nullptr, 50};
            ImGui::Text("Download successful!");
        }

        ImGui::TextWrapped("Would you like to install this theme for the StyleMiiU plugin?");

        ImGui::Checkbox("Apply theme after install", &set_current);

        // Make two buttons of equal size.
        const std::string install_label = ICON_FA_COGS " Install";
        const std::string cancel_label = ICON_FA_TIMES " Cancel";
        const ImVec2 install_size = ImGui::CalcTextSize(install_label);
        const ImVec2 cancel_size = ImGui::CalcTextSize(cancel_label);

        const auto &style = ImGui::GetStyle();
        const ImVec2 button_size =
            ImVec2{ std::fmax(install_size.x, cancel_size.x),
                    std::fmax(install_size.y, cancel_size.y) }
            + 2 * style.FramePadding;

        const ImVec2 available = ImGui::GetContentRegionAvail();
        // Place the buttons on the bottom.
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + available.y - button_size.y);

        const float total_width = 2 * button_size.x + style.ItemSpacing.x;

        const float start_x = (available.x - total_width) / 2;
        if (start_x > 0.0f)
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + start_x);

        if (ImGui::Button(install_label, button_size)) {
            Installer::UThemeMetadata theme_data;
            Installer::GetUThemeMetadata(utheme_filename, theme_data);

            ImGui::CloseCurrentPopup();
            state = State::hidden;

            InstallThemePopup::open(utheme_filename, theme_data, true, set_current);
        }
        ImGui::SetItemDefaultFocus();

        ImGui::SameLine();

        if (ImGui::Button(cancel_label, button_size)) {
            ImGui::CloseCurrentPopup();
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
                // TODO
                ImGui::Text("ERROR!");
                for (const auto& msg : error_messages)
                    ImGui::TextWrapped(msg);

                if (success_count + error_count == transfers.size()) {
                    if (ImGui::Button("Close")) {
                        DownloadManager::clear_finished();
                        state = State::hidden;
                    }
                }
                break;

            case State::success:
                show_success();
                break;

            default:
                ;
        }

    }
}
