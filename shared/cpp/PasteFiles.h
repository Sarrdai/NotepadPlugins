// Shared by the C++ Notepad++ plugins of this repository.
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Edit > Paste with files copied in Explorer (also from network drives): Notepad++ ignores such a
// clipboard (CF_HDROP without text). The hook subclasses the Notepad++ main window and hands the
// copied files to the plugin, which opens them directly from their location.

#pragma once

#include <windows.h>
#include <shellapi.h>

#include <string>
#include <vector>

namespace nppshared {

constexpr UINT kIdmEditPaste = 42000 + 5;  // IDM_EDIT_PASTE (menuCmdID.h)

// Files copied in Explorer; empty if the clipboard holds text (normal paste) or no file list.
inline std::vector<std::wstring> clipboardFileList(HWND owner) {
    std::vector<std::wstring> files;
    if (::IsClipboardFormatAvailable(CF_UNICODETEXT) || !::IsClipboardFormatAvailable(CF_HDROP)) return files;
    if (!::OpenClipboard(owner)) return files;
    // The clipboard owns the data: no DragFinish.
    if (const auto drop = static_cast<HDROP>(::GetClipboardData(CF_HDROP))) {
        const UINT count = ::DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
        for (UINT i = 0; i < count; ++i) {
            const UINT length = ::DragQueryFileW(drop, i, nullptr, 0);
            std::wstring file(length + 1, L'\0');
            ::DragQueryFileW(drop, i, file.data(), length + 1);
            file.resize(length);
            files.push_back(std::move(file));
        }
    }
    ::CloseClipboard();
    return files;
}

inline bool isDirectory(const std::wstring& path) {
    const DWORD attributes = ::GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY);
}

// Subclasses the Notepad++ main window. `handler` receives the copied files on Edit > Paste and
// returns true if it handled them (the paste is then swallowed). Install at NPPN_READY.
class PasteFilesHook {
public:
    using Handler = bool (*)(const std::vector<std::wstring>& files);

    static void install(HWND npp, Handler handler) {
        if (_original) return;
        _npp = npp;
        _handler = handler;
        _original = reinterpret_cast<WNDPROC>(
            ::SetWindowLongPtrW(npp, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&PasteFilesHook::windowProc)));
    }

    // Restores the original procedure if no other plugin subclassed the window after us.
    static void uninstall() {
        if (!_original) return;
        if (reinterpret_cast<WNDPROC>(::GetWindowLongPtrW(_npp, GWLP_WNDPROC)) == &PasteFilesHook::windowProc) {
            ::SetWindowLongPtrW(_npp, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(_original));
            _original = nullptr;
        }
        _handler = nullptr;  // stay in the chain, but pass everything through
    }

private:
    static LRESULT CALLBACK windowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
        if (msg == WM_COMMAND && LOWORD(wParam) == kIdmEditPaste && _handler) {
            const std::vector<std::wstring> files = clipboardFileList(hwnd);
            if (!files.empty() && _handler(files)) return 0;
        }
        return ::CallWindowProcW(_original, hwnd, msg, wParam, lParam);
    }

    static inline HWND _npp = nullptr;
    static inline WNDPROC _original = nullptr;
    static inline Handler _handler = nullptr;
};

}  // namespace nppshared
