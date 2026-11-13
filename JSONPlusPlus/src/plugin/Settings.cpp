// JSON++ - Notepad++ plugin
// Copyright (C) 2026 JSON++ contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "Settings.h"

#include <windows.h>

#include <algorithm>
#include <cwctype>

namespace jpp {

namespace {

constexpr const wchar_t* kSection = L"JSON++";

int readInt(const std::wstring& ini, const wchar_t* key, int fallback, int minValue, int maxValue) {
    const int value = static_cast<int>(::GetPrivateProfileIntW(kSection, key, fallback, ini.c_str()));
    return std::clamp(value, minValue, maxValue);
}

bool readBool(const std::wstring& ini, const wchar_t* key, bool fallback) {
    return ::GetPrivateProfileIntW(kSection, key, fallback ? 1 : 0, ini.c_str()) != 0;
}

std::wstring readString(const std::wstring& ini, const wchar_t* key, const std::wstring& fallback) {
    wchar_t buffer[1024] = {};
    ::GetPrivateProfileStringW(kSection, key, fallback.c_str(), buffer, static_cast<DWORD>(std::size(buffer)),
                               ini.c_str());
    return buffer;
}

void writeInt(const std::wstring& ini, const wchar_t* key, int value) {
    ::WritePrivateProfileStringW(kSection, key, std::to_wstring(value).c_str(), ini.c_str());
}

std::wstring lower(std::wstring s) {
    std::transform(s.begin(), s.end(), s.begin(), [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
    return s;
}

}  // namespace

void Settings::load(const std::wstring& ini) {
    const Settings d;
    autoActivate = readBool(ini, L"AutoActivate", d.autoActivate);
    minLength = readInt(ini, L"MinLength", d.minLength, 0, 1 << 30);
    minAverageLineLength = readInt(ini, L"MinAverageLineLength", d.minAverageLineLength, 1, 1 << 30);
    maxFileSizeMB = readInt(ini, L"MaxFileSizeMB", d.maxFileSizeMB, 1, 2047);
    extensions = readString(ini, L"Extensions", d.extensions);
    indentSize = readInt(ini, L"IndentSize", d.indentSize, 0, 16);
    useTabs = readBool(ini, L"UseTabs", d.useTabs);
    spaceAfterColon = readBool(ini, L"SpaceAfterColon", d.spaceAfterColon);
    allowComments = readBool(ini, L"AllowComments", d.allowComments);
    syncCaret = readBool(ini, L"SyncCaret", d.syncCaret);
    documentToPanelDelayMs = readInt(ini, L"DocumentToPanelDelayMs", d.documentToPanelDelayMs, 10, 10000);
    panelToDocumentDelayMs = readInt(ini, L"PanelToDocumentDelayMs", d.panelToDocumentDelayMs, 10, 10000);
    reformatDelayMs = readInt(ini, L"ReformatDelayMs", d.reformatDelayMs, 0, 600000);
}

void Settings::save(const std::wstring& ini) const {
    writeInt(ini, L"AutoActivate", autoActivate);
    writeInt(ini, L"MinLength", minLength);
    writeInt(ini, L"MinAverageLineLength", minAverageLineLength);
    writeInt(ini, L"MaxFileSizeMB", maxFileSizeMB);
    ::WritePrivateProfileStringW(kSection, L"Extensions", extensions.c_str(), ini.c_str());
    writeInt(ini, L"IndentSize", indentSize);
    writeInt(ini, L"UseTabs", useTabs);
    writeInt(ini, L"SpaceAfterColon", spaceAfterColon);
    writeInt(ini, L"AllowComments", allowComments);
    writeInt(ini, L"SyncCaret", syncCaret);
    writeInt(ini, L"DocumentToPanelDelayMs", documentToPanelDelayMs);
    writeInt(ini, L"PanelToDocumentDelayMs", panelToDocumentDelayMs);
    writeInt(ini, L"ReformatDelayMs", reformatDelayMs);
}

bool Settings::matchesExtension(const std::wstring& filePath) const {
    const auto slash = filePath.find_last_of(L"\\/");
    const std::wstring name = lower(slash == std::wstring::npos ? filePath : filePath.substr(slash + 1));
    const auto dot = name.find_last_of(L'.');
    // Dot files such as ".babelrc" use their full name as extension.
    const std::wstring ext = dot == std::wstring::npos ? name : name.substr(dot + 1);
    if (ext.empty()) return false;

    const std::wstring list = lower(extensions);
    std::size_t start = 0;
    while (start <= list.size()) {
        std::size_t end = list.find_first_of(L";, ", start);
        if (end == std::wstring::npos) end = list.size();
        std::wstring item = list.substr(start, end - start);
        if (!item.empty() && item.front() == L'.') item.erase(0, 1);
        if (!item.empty() && item == ext) return true;
        start = end + 1;
    }
    return false;
}

}  // namespace jpp
