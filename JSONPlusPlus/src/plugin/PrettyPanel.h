// JSON++ - Notepad++ plugin
// Copyright (C) 2026 JSON++ contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <windows.h>

#include <functional>
#include <string>

#include "PluginInterface.h"
#include "Docking.h"
#include "JsonCore.h"
#include "SciWnd.h"
#include "Settings.h"

namespace jpp {

// Dockable panel that shows the document of a Notepad++ buffer pretty printed
// and keeps both sides synchronised:
//
//   document --(SCN_MODIFIED of the mirror view)--> prettyPrint --> minimal diff --> panel
//   panel    --(SCN_MODIFIED of the panel editor)--> diffContent --> minimal compact edit --> document
//
// The document is accessed through a hidden "mirror" Scintilla view that shares
// the Notepad++ document (SCI_SETDOCPOINTER). This makes the synchronisation
// independent of the view (main/sub) and of which buffer is currently shown.
// The document itself always stays compact, so saving writes the compact form.
class PrettyPanel {
public:
    PrettyPanel(HINSTANCE module, const NppData& npp, const Settings& settings);
    ~PrettyPanel();
    PrettyPanel(const PrettyPanel&) = delete;
    PrettyPanel& operator=(const PrettyPanel&) = delete;

    bool create(int dockCommandIndex, std::function<void()> onClosedByUser);

    void show();
    void hide();
    bool isVisible() const noexcept { return _visible; }

    // Binds the panel to the document currently shown in `view`.
    void attach(UINT_PTR bufferId, HWND view, const std::wstring& title);
    // Writes pending edits back and releases the document.
    void detach();
    UINT_PTR buffer() const noexcept { return _buffer; }
    bool isAttached() const noexcept { return _doc != 0; }

    // Writes pending panel edits into the document immediately.
    // `force` also writes intermediate states (e.g. unterminated strings).
    void flush(bool force);

    void reformatNow();
    void applyTheme();
    void onSettingsChanged();
    void onViewUpdateUI(HWND view, int updated);

    // Pin button in the status row: `onClicked` is called on click, `setPinned` shows the state.
    void setPinHandler(std::function<void()> onClicked) { _onPinClicked = std::move(onClicked); }
    void setPinned(bool pinned);

private:
    static LRESULT CALLBACK windowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    LRESULT handleMessage(UINT msg, WPARAM wParam, LPARAM lParam);

    void setupEditor();
    void layout();
    void onEditorNotify(const SCNotification& n);
    void onMirrorNotify(const SCNotification& n);
    void onTimer(UINT_PTR id);

    void loadFromDocument();
    void refreshFromDocument();
    void pushToDocument(bool force);
    void reformatPanel(bool force);
    void setPanelText(std::string_view text);
    void validateNow();
    void syncCaretFromView();
    void syncCaretFromPanel();
    void autoIndent();
    void highlightBraces();
    void updateLineNumberWidth();
    void setStatus(const std::wstring& text, bool error);
    HWND viewShowingDocument() const;
    FormatOptions formatOptions() const;
    void restartTimer(UINT_PTR id, int delayMs);
    void stopTimers();

    HINSTANCE _module;
    NppData _npp;
    const Settings& _settings;

    HWND _hwnd = nullptr;
    HWND _status = nullptr;
    HWND _pin = nullptr;
    bool _pinned = false;
    std::function<void()> _onPinClicked;
    HFONT _statusFont = nullptr;
    HBRUSH _backBrush = nullptr;
    COLORREF _statusFore = RGB(0, 0, 0);
    COLORREF _statusBack = RGB(240, 240, 240);
    bool _statusError = false;

    SciWnd _editor;  // visible pretty printed editor
    SciWnd _mirror;  // hidden view sharing the Notepad++ document

    DockedWidgetData _dock{};
    std::wstring _dockTitle = L"JSON++";
    std::wstring _dockInfo;
    std::wstring _moduleName;
    bool _registered = false;
    bool _visible = false;
    std::function<void()> _onClosedByUser;

    UINT_PTR _buffer = 0;
    sptr_t _doc = 0;
    HWND _view = nullptr;

    bool _syncing = false;       // set while we modify one side programmatically
    bool _panelDirty = false;    // panel has edits not yet written to the document
    bool _pushDeferred = false;  // panel contains an unterminated string/comment
    int _lineNumberDigits = 0;
};

}  // namespace jpp
