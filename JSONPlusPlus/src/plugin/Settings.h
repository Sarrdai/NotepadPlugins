// JSON++ - Notepad++ plugin
// Copyright (C) 2026 JSON++ contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <string>
#include <vector>

namespace jpp {

struct Settings {
    // Activation
    bool autoActivate = true;
    int minLength = 64;                // bytes
    int minAverageLineLength = 300;    // bytes per line
    int maxFileSizeMB = 32;            // larger documents are never activated automatically
    std::wstring extensions = L"json;jsonc;json5;geojson;topojson;har;webmanifest;jsonld;babelrc;eslintrc";

    // Pretty view
    int indentSize = 4;
    bool useTabs = false;
    bool spaceAfterColon = true;
    bool allowComments = true;         // JSONC: do not report comments as errors
    bool syncCaret = true;             // keep the caret of both views on the same JSON position

    // Synchronisation timing
    int documentToPanelDelayMs = 150;  // debounce for changes made in the editor
    int panelToDocumentDelayMs = 120;  // debounce for changes made in the pretty view
    int reformatDelayMs = 1500;        // idle time before the pretty view is re-indented (0 = never)

    void load(const std::wstring& iniPath);
    void save(const std::wstring& iniPath) const;

    bool matchesExtension(const std::wstring& filePath) const;
};

}  // namespace jpp
