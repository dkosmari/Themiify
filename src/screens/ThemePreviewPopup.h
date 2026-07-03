/*
 * Themiify - A theme manager for the Nintendo Wii U
 * Copyright (C) 2026 Fangal-Airbag
 * Copyright (C) 2026 AlphaCraft9658
 * Copyright (C) 2026  Daniel K. O. <dkosmari>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace ThemePreviewPopup {
    void open(const std::string& id, const std::vector<std::string>& images);
    void open(const std::string& id, const std::vector<std::filesystem::path>& images);

    void process_ui();
}
