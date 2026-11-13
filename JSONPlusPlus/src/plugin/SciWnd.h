// JSON++ - Notepad++ plugin
// Copyright (C) 2026 JSON++ contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <windows.h>

#include <string_view>

#include "Scintilla.h"

namespace jpp {

// Thin wrapper around a Scintilla window using the direct function interface
// (no SendMessage round trip for every call).
class SciWnd {
public:
    void bind(HWND hwnd) noexcept {
        _hwnd = hwnd;
        _fn = reinterpret_cast<SciFnDirect>(::SendMessageW(hwnd, SCI_GETDIRECTFUNCTION, 0, 0));
        _ptr = static_cast<sptr_t>(::SendMessageW(hwnd, SCI_GETDIRECTPOINTER, 0, 0));
    }

    HWND hwnd() const noexcept { return _hwnd; }
    explicit operator bool() const noexcept { return _hwnd != nullptr && _fn != nullptr; }

    sptr_t call(unsigned int msg, uptr_t wParam = 0, sptr_t lParam = 0) const {
        return _fn(_ptr, msg, wParam, lParam);
    }

    // View on the whole document. Invalidated by the next modification.
    std::string_view text() const {
        const auto length = static_cast<std::size_t>(call(SCI_GETLENGTH));
        const auto* data = reinterpret_cast<const char*>(call(SCI_GETCHARACTERPOINTER));
        return data ? std::string_view(data, length) : std::string_view();
    }

    void replaceRange(std::size_t start, std::size_t end, std::string_view replacement) const {
        call(SCI_SETTARGETRANGE, start, static_cast<sptr_t>(end));
        call(SCI_REPLACETARGET, replacement.size(), reinterpret_cast<sptr_t>(replacement.data()));
    }

private:
    HWND _hwnd = nullptr;
    SciFnDirect _fn = nullptr;
    sptr_t _ptr = 0;
};

}  // namespace jpp
