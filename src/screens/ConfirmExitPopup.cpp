/*
 * Themiify - A theme manager for the Nintendo Wii U
 * Copyright (C) 2026 Fangal-Airbag
 * Copyright (C) 2026 AlphaCraft9658
 * Copyright (C) 2026 Daniel K. O. <dkosmari>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <string>

#include <imgui.h>
#include <imgui_raii.h>
#include <imgui_stdlib.h>

#include "ConfirmExitPopup.h"

#include "../App.h"
#include "../IconsFontAwesome4.h"
#include "../utils.h"

namespace ConfirmExitPopup {

    namespace {

        enum class State {
            hidden,
            queued,
            visible,
        };

        State state;

        std::string popup_id;

    } // namespace

    void
    open() {
        state = State::queued;
    }

    void
    process_ui() {
        using namespace ImGui::RAII;

        if (state == State::hidden)
            return;

        if (state == State::queued) {
            state = State::visible;
            ImGui::OpenPopup(popup_id);
        }

        const auto center = ImGui::GetMainViewport()->GetCenter();
        ImGui::SetNextWindowPos(center, ImGuiCond_Always, {0.5f, 0.5f});
        PopupModal popup{popup_id, nullptr,
                         ImGuiWindowFlags_NoSavedSettings |
                         ImGuiWindowFlags_AlwaysAutoResize |
                         ImGuiWindowFlags_NoDecoration |
                         ImGuiWindowFlags_NoMove};
        if (!popup) {
            state = State::hidden;
            return;
        }

        {
            Font title_font{nullptr, 40};
            ImGui::Text("Are you sure you want to exit?");
        }

        ImGui::Separator();

        // Show exit/cancel buttons, centered.

        const std::string exit_label = ICON_FA_SIGN_OUT " Exit";
        const auto exit_size = ImGui::CalcTextSize(exit_label);

        const std::string cancel_label = ICON_FA_TIMES " Cancel";
        const auto cancel_size = ImGui::CalcTextSize(cancel_label);

        // Use same size for both buttons.
        const auto& style = ImGui::GetStyle();
        const auto button_size = max(exit_size, cancel_size) + 2 * style.FramePadding;

        const float buttons_width = 2 * button_size.x + style.ItemSpacing.x;

        float offset = (ImGui::GetContentRegionAvail().x - buttons_width) / 2;
        if (offset > 0)
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + offset);

        if (ImGui::Button(exit_label, button_size)) {
            ImGui::CloseCurrentPopup();
            state = State::hidden;
            App::quit();
        }

        ImGui::SameLine();

        if (ImGui::Button(cancel_label, button_size)) {
            ImGui::CloseCurrentPopup();
            state = State::hidden;
        }

    }

} // namespace ConfirmExitPopup
