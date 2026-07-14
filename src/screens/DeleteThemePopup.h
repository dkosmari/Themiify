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

#include "../ThemeManager.h"

namespace DeleteThemePopup {
    void open(const ThemeManager::InstalledThemeMetadata &installed_theme_data);

    void process_ui();
}
