// JSON++ - Notepad++ plugin
// Copyright (C) 2026 JSON++ contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "PrettyPanel.h"

#include <algorithm>
#include <cstring>
#include <string>

#include "JsonCore.h"
#include "dockingResource.h"

namespace jpp {

namespace {

constexpr wchar_t kWindowClass[] = L"JsonPlusPlusPrettyPanel";

constexpr UINT_PTR kTimerDocumentToPanel = 1;
constexpr UINT_PTR kTimerPanelToDocument = 2;
constexpr UINT_PTR kTimerReformat = 3;
constexpr UINT_PTR kTimerValidate = 4;
constexpr UINT_PTR kTimerCaretFromView = 5;
constexpr UINT_PTR kTimerCaretFromPanel = 6;

constexpr int kValidateDelayMs = 250;
constexpr int kCaretDelayMs = 40;

constexpr int kMarginNumbers = 0;
constexpr int kMarginSymbols = 1;
constexpr int kMarginFold = 2;
constexpr int kErrorIndicator = 8;  // own Scintilla instance: no allocation needed

// Lexilla JSON lexer style numbers (SciLexer.h, SCE_JSON_*).
constexpr int kJsonNumber = 1;
constexpr int kJsonString = 2;
constexpr int kJsonStringEol = 3;
constexpr int kJsonPropertyName = 4;
constexpr int kJsonEscape = 5;
constexpr int kJsonLineComment = 6;
constexpr int kJsonBlockComment = 7;
constexpr int kJsonOperator = 8;
constexpr int kJsonKeyword = 11;
constexpr int kJsonError = 13;

// RAII guard for programmatic modifications.
class SyncGuard {
public:
    explicit SyncGuard(bool& flag) noexcept : _flag(flag), _previous(flag) { _flag = true; }
    ~SyncGuard() { _flag = _previous; }
    SyncGuard(const SyncGuard&) = delete;
    SyncGuard& operator=(const SyncGuard&) = delete;

private:
    bool& _flag;
    bool _previous;
};

UINT dpiOf(HWND hwnd) {
    using GetDpiForWindowFn = UINT(WINAPI*)(HWND);
    static const auto fn = reinterpret_cast<GetDpiForWindowFn>(
        reinterpret_cast<void*>(::GetProcAddress(::GetModuleHandleW(L"user32.dll"), "GetDpiForWindow")));
    if (fn && hwnd) return fn(hwnd);
    HDC dc = ::GetDC(nullptr);
    const int dpi = ::GetDeviceCaps(dc, LOGPIXELSY);
    ::ReleaseDC(nullptr, dc);
    return static_cast<UINT>(dpi);
}

int scale(HWND hwnd, int value) { return ::MulDiv(value, static_cast<int>(dpiOf(hwnd)), 96); }

std::wstring withThousands(std::size_t value) {
    std::wstring digits = std::to_wstring(value);
    for (int i = static_cast<int>(digits.size()) - 3; i > 0; i -= 3) digits.insert(static_cast<std::size_t>(i), 1, L'.');
    return digits;
}

std::wstring widen(std::string_view text) {
    if (text.empty()) return {};
    const int length = ::MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    std::wstring result(static_cast<std::size_t>(length), L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), result.data(), length);
    return result;
}

bool isDark(COLORREF c) {
    return (GetRValue(c) * 299 + GetGValue(c) * 587 + GetBValue(c) * 114) / 1000 < 128;
}

}  // namespace

PrettyPanel::PrettyPanel(HINSTANCE module, const NppData& npp, const Settings& settings)
    : _module(module), _npp(npp), _settings(settings) {}

PrettyPanel::~PrettyPanel() {
    if (_statusFont) ::DeleteObject(_statusFont);
    if (_backBrush) ::DeleteObject(_backBrush);
}

// ---------------------------------------------------------------------------
// Creation / docking
// ---------------------------------------------------------------------------

bool PrettyPanel::create(int dockCommandIndex, std::function<void()> onClosedByUser) {
    _onClosedByUser = std::move(onClosedByUser);

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = &PrettyPanel::windowProc;
    wc.hInstance = _module;
    wc.hCursor = ::LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = kWindowClass;
    if (!::RegisterClassExW(&wc) && ::GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return false;

    // Docking clients are created as children of Notepad++; the docking manager re-parents them.
    _hwnd = ::CreateWindowExW(0, kWindowClass, L"JSON++", WS_CHILD | WS_CLIPCHILDREN, 0, 0, 300, 400,
                              _npp._nppHandle, nullptr, _module, this);
    if (!_hwnd) return false;

    auto editor = reinterpret_cast<HWND>(::SendMessageW(_npp._nppHandle, NPPM_CREATESCINTILLAHANDLE, 0,
                                                        reinterpret_cast<LPARAM>(_hwnd)));
    auto mirror = reinterpret_cast<HWND>(::SendMessageW(_npp._nppHandle, NPPM_CREATESCINTILLAHANDLE, 0,
                                                        reinterpret_cast<LPARAM>(_hwnd)));
    if (!editor || !mirror) return false;
    _editor.bind(editor);
    _mirror.bind(mirror);
    ::ShowWindow(mirror, SW_HIDE);
    ::ShowWindow(editor, SW_SHOW);
    // The mirror only reports document changes; everything else is view state.
    _mirror.call(SCI_SETMODEVENTMASK, SC_MOD_INSERTTEXT | SC_MOD_DELETETEXT);

    _status = ::CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_LEFT | SS_NOPREFIX | SS_ENDELLIPSIS |
                                                       SS_CENTERIMAGE,
                                0, 0, 100, 20, _hwnd, nullptr, _module, nullptr);
    _pin = ::CreateWindowExW(0, L"STATIC", L"Pin", WS_CHILD | WS_VISIBLE | SS_CENTER | SS_NOPREFIX | SS_CENTERIMAGE |
                                                    SS_NOTIFY,
                             0, 0, 60, 20, _hwnd, nullptr, _module, nullptr);
    NONCLIENTMETRICSW metrics{};
    metrics.cbSize = sizeof(metrics);
    if (::SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0)) {
        _statusFont = ::CreateFontIndirectW(&metrics.lfStatusFont);
        ::SendMessageW(_status, WM_SETFONT, reinterpret_cast<WPARAM>(_statusFont), FALSE);
        ::SendMessageW(_pin, WM_SETFONT, reinterpret_cast<WPARAM>(_statusFont), FALSE);
    }
    setPinned(false);

    setupEditor();
    applyTheme();

    wchar_t path[MAX_PATH] = {};
    ::GetModuleFileNameW(_module, path, MAX_PATH);
    _moduleName = path;
    if (const auto slash = _moduleName.find_last_of(L"\\/"); slash != std::wstring::npos) {
        _moduleName.erase(0, slash + 1);
    }

    _dock.hClient = _hwnd;
    _dock.pszName = _dockTitle.c_str();
    _dock.dlgID = dockCommandIndex;
    _dock.uMask = DWS_DF_CONT_RIGHT | DWS_ADDINFO;
    _dock.pszAddInfo = _dockInfo.c_str();
    _dock.pszModuleName = _moduleName.c_str();
    return true;
}

void PrettyPanel::show() {
    if (!_hwnd) return;
    if (!_registered) {
        ::SendMessageW(_npp._nppHandle, NPPM_DMMREGASDCKDLG, 0, reinterpret_cast<LPARAM>(&_dock));
        _registered = true;
    }
    if (!_visible) ::SendMessageW(_npp._nppHandle, NPPM_DMMSHOW, 0, reinterpret_cast<LPARAM>(_hwnd));
    _visible = true;
}

void PrettyPanel::hide() {
    if (!_hwnd || !_registered || !_visible) return;
    ::SendMessageW(_npp._nppHandle, NPPM_DMMHIDE, 0, reinterpret_cast<LPARAM>(_hwnd));
    _visible = false;
}

void PrettyPanel::setupEditor() {
    const SciWnd& e = _editor;
    if (void* lexer = reinterpret_cast<void*>(
            ::SendMessageW(_npp._nppHandle, NPPM_CREATELEXER, 0, reinterpret_cast<LPARAM>(L"json")))) {
        e.call(SCI_SETILEXER, 0, reinterpret_cast<sptr_t>(lexer));
    }
    e.call(SCI_SETPROPERTY, reinterpret_cast<uptr_t>("fold"), reinterpret_cast<sptr_t>("1"));
    e.call(SCI_SETPROPERTY, reinterpret_cast<uptr_t>("fold.compact"), reinterpret_cast<sptr_t>("0"));
    e.call(SCI_SETPROPERTY, reinterpret_cast<uptr_t>("lexer.json.allow.comments"), reinterpret_cast<sptr_t>("1"));
    e.call(SCI_SETPROPERTY, reinterpret_cast<uptr_t>("lexer.json.escape.sequence"), reinterpret_cast<sptr_t>("1"));
    e.call(SCI_SETKEYWORDS, 0, reinterpret_cast<sptr_t>("false null true"));

    e.call(SCI_SETCODEPAGE, SC_CP_UTF8);
    e.call(SCI_SETEOLMODE, SC_EOL_LF);
    e.call(SCI_SETMODEVENTMASK, SC_MOD_INSERTTEXT | SC_MOD_DELETETEXT);
    e.call(SCI_USEPOPUP, SC_POPUP_ALL);
    e.call(SCI_SETINDENTATIONGUIDES, SC_IV_LOOKBOTH);
    e.call(SCI_SETSCROLLWIDTH, 1);
    e.call(SCI_SETSCROLLWIDTHTRACKING, 1);
    e.call(SCI_SETLAYOUTCACHE, SC_CACHE_PAGE);
    e.call(SCI_SETMARGINLEFT, 0, scale(_hwnd, 2));

    e.call(SCI_SETMARGINTYPEN, kMarginNumbers, SC_MARGIN_NUMBER);
    e.call(SCI_SETMARGINWIDTHN, kMarginSymbols, 0);
    e.call(SCI_SETMARGINTYPEN, kMarginFold, SC_MARGIN_SYMBOL);
    e.call(SCI_SETMARGINMASKN, kMarginFold, static_cast<sptr_t>(SC_MASK_FOLDERS));
    e.call(SCI_SETMARGINSENSITIVEN, kMarginFold, 1);
    e.call(SCI_SETMARGINWIDTHN, kMarginFold, scale(_hwnd, 14));
    e.call(SCI_SETAUTOMATICFOLD, SC_AUTOMATICFOLD_SHOW | SC_AUTOMATICFOLD_CLICK | SC_AUTOMATICFOLD_CHANGE);
    e.call(SCI_SETFOLDFLAGS, SC_FOLDFLAG_LINEAFTER_CONTRACTED);
    e.call(SCI_MARKERDEFINE, SC_MARKNUM_FOLDEROPEN, SC_MARK_BOXMINUS);
    e.call(SCI_MARKERDEFINE, SC_MARKNUM_FOLDER, SC_MARK_BOXPLUS);
    e.call(SCI_MARKERDEFINE, SC_MARKNUM_FOLDERSUB, SC_MARK_VLINE);
    e.call(SCI_MARKERDEFINE, SC_MARKNUM_FOLDERTAIL, SC_MARK_LCORNER);
    e.call(SCI_MARKERDEFINE, SC_MARKNUM_FOLDEREND, SC_MARK_BOXPLUSCONNECTED);
    e.call(SCI_MARKERDEFINE, SC_MARKNUM_FOLDEROPENMID, SC_MARK_BOXMINUSCONNECTED);
    e.call(SCI_MARKERDEFINE, SC_MARKNUM_FOLDERMIDTAIL, SC_MARK_TCORNER);

    e.call(SCI_INDICSETSTYLE, kErrorIndicator, INDIC_SQUIGGLEPIXMAP);
    e.call(SCI_INDICSETFORE, kErrorIndicator, RGB(0xE0, 0x30, 0x30));
    e.call(SCI_INDICSETUNDER, kErrorIndicator, 1);

    onSettingsChanged();
}

void PrettyPanel::onSettingsChanged() {
    if (!_editor) return;
    const int indent = std::max(1, _settings.indentSize);
    _editor.call(SCI_SETTABWIDTH, indent);
    _editor.call(SCI_SETINDENT, indent);
    _editor.call(SCI_SETUSETABS, _settings.useTabs ? 1 : 0);
    if (_doc) refreshFromDocument();
}

// ---------------------------------------------------------------------------
// Theme: reuse the colours of the Notepad++ editor (and its JSON styler)
// ---------------------------------------------------------------------------

void PrettyPanel::applyTheme() {
    if (!_editor) return;
    HWND view = _view ? _view : _npp._scintillaMainHandle;
    auto viewCall = [view](unsigned int msg, uptr_t w = 0, sptr_t l = 0) {
        return ::SendMessageW(view, msg, w, l);
    };

    char lexerName[64] = {};
    if (viewCall(SCI_GETLEXERLANGUAGE, 0, 0) < static_cast<sptr_t>(sizeof(lexerName))) {
        viewCall(SCI_GETLEXERLANGUAGE, 0, reinterpret_cast<sptr_t>(lexerName));
    }
    const bool viewUsesJsonLexer = std::strcmp(lexerName, "json") == 0;

    auto copyStyle = [&](int style) {
        char font[128] = {};
        if (viewCall(SCI_STYLEGETFONT, style, 0) < static_cast<sptr_t>(sizeof(font))) {
            viewCall(SCI_STYLEGETFONT, style, reinterpret_cast<sptr_t>(font));
            if (font[0]) _editor.call(SCI_STYLESETFONT, style, reinterpret_cast<sptr_t>(font));
        }
        _editor.call(SCI_STYLESETSIZEFRACTIONAL, style, viewCall(SCI_STYLEGETSIZEFRACTIONAL, style));
        _editor.call(SCI_STYLESETFORE, style, viewCall(SCI_STYLEGETFORE, style));
        _editor.call(SCI_STYLESETBACK, style, viewCall(SCI_STYLEGETBACK, style));
        _editor.call(SCI_STYLESETWEIGHT, style, viewCall(SCI_STYLEGETWEIGHT, style));
        _editor.call(SCI_STYLESETITALIC, style, viewCall(SCI_STYLEGETITALIC, style));
        _editor.call(SCI_STYLESETUNDERLINE, style, viewCall(SCI_STYLEGETUNDERLINE, style));
        _editor.call(SCI_STYLESETEOLFILLED, style, viewCall(SCI_STYLEGETEOLFILLED, style));
    };

    copyStyle(STYLE_DEFAULT);
    _editor.call(SCI_STYLECLEARALL);
    for (int style = STYLE_LINENUMBER; style <= STYLE_FOLDDISPLAYTEXT; ++style) copyStyle(style);

    const auto defaultFore = static_cast<COLORREF>(_editor.call(SCI_STYLEGETFORE, STYLE_DEFAULT));
    const auto defaultBack = static_cast<COLORREF>(_editor.call(SCI_STYLEGETBACK, STYLE_DEFAULT));
    if (viewUsesJsonLexer) {
        for (int style = 0; style <= kJsonError; ++style) copyStyle(style);
    } else {
        // Fallback palette when the editor uses another lexer (e.g. JSON opened as "Normal text").
        const bool dark = isDark(defaultBack);
        auto set = [&](int style, COLORREF fore, bool bold = false) {
            _editor.call(SCI_STYLESETFORE, style, fore);
            _editor.call(SCI_STYLESETBOLD, style, bold);
        };
        set(kJsonNumber, dark ? RGB(0xB5, 0xCE, 0xA8) : RGB(0xFF, 0x80, 0x00));
        set(kJsonString, dark ? RGB(0xCE, 0x91, 0x78) : RGB(0x80, 0x80, 0x80));
        set(kJsonStringEol, dark ? RGB(0xF4, 0x47, 0x47) : RGB(0xC0, 0x00, 0x00));
        set(kJsonPropertyName, dark ? RGB(0x9C, 0xDC, 0xFE) : RGB(0x88, 0x00, 0x88));
        set(kJsonEscape, dark ? RGB(0xD7, 0xBA, 0x7D) : RGB(0x0B, 0x98, 0xD8));
        set(kJsonLineComment, dark ? RGB(0x6A, 0x99, 0x55) : RGB(0x00, 0x80, 0x00));
        set(kJsonBlockComment, dark ? RGB(0x6A, 0x99, 0x55) : RGB(0x00, 0x80, 0x00));
        set(kJsonOperator, defaultFore, true);
        set(kJsonKeyword, dark ? RGB(0x56, 0x9C, 0xD6) : RGB(0x00, 0x00, 0xFF), true);
        set(kJsonError, dark ? RGB(0xF4, 0x47, 0x47) : RGB(0xFF, 0x00, 0x00));
    }

    // Element colours (selection, caret, caret line, white space ...).
    static constexpr int kElements[] = {
        SC_ELEMENT_SELECTION_TEXT,          SC_ELEMENT_SELECTION_BACK,
        SC_ELEMENT_SELECTION_ADDITIONAL_TEXT, SC_ELEMENT_SELECTION_ADDITIONAL_BACK,
        SC_ELEMENT_SELECTION_SECONDARY_TEXT, SC_ELEMENT_SELECTION_SECONDARY_BACK,
        SC_ELEMENT_SELECTION_INACTIVE_TEXT,  SC_ELEMENT_SELECTION_INACTIVE_BACK,
        SC_ELEMENT_CARET,                    SC_ELEMENT_CARET_LINE_BACK,
        SC_ELEMENT_WHITE_SPACE,              SC_ELEMENT_FOLD_LINE,
    };
    for (const int element : kElements) {
        if (viewCall(SCI_GETELEMENTISSET, element)) {
            _editor.call(SCI_SETELEMENTCOLOUR, element, viewCall(SCI_GETELEMENTCOLOUR, element));
        } else {
            _editor.call(SCI_RESETELEMENTCOLOUR, element);
        }
    }
    _editor.call(SCI_SETSELECTIONLAYER, viewCall(SCI_GETSELECTIONLAYER));
    _editor.call(SCI_SETCARETLINEFRAME, viewCall(SCI_GETCARETLINEFRAME));
    _editor.call(SCI_SETCARETLINEVISIBLEALWAYS, viewCall(SCI_GETCARETLINEVISIBLEALWAYS));
    _editor.call(SCI_SETCARETWIDTH, viewCall(SCI_GETCARETWIDTH));
    _editor.call(SCI_SETTECHNOLOGY, viewCall(SCI_GETTECHNOLOGY));
    _editor.call(SCI_SETFONTQUALITY, viewCall(SCI_GETFONTQUALITY));
    _editor.call(SCI_SETZOOM, viewCall(SCI_GETZOOM));

    const auto marginFore = static_cast<COLORREF>(_editor.call(SCI_STYLEGETFORE, STYLE_LINENUMBER));
    const auto marginBack = static_cast<COLORREF>(_editor.call(SCI_STYLEGETBACK, STYLE_LINENUMBER));
    _editor.call(SCI_SETFOLDMARGINCOLOUR, 1, marginBack);
    _editor.call(SCI_SETFOLDMARGINHICOLOUR, 1, marginBack);
    for (int marker = SC_MARKNUM_FOLDEREND; marker <= SC_MARKNUM_FOLDEROPEN; ++marker) {
        _editor.call(SCI_MARKERSETFORE, marker, defaultBack);
        _editor.call(SCI_MARKERSETBACK, marker, marginFore);
    }

    _statusFore = marginFore;
    _statusBack = marginBack;
    if (_backBrush) ::DeleteObject(_backBrush);
    _backBrush = ::CreateSolidBrush(_statusBack);
    if (_hwnd) ::InvalidateRect(_hwnd, nullptr, TRUE);
    if (_status) ::InvalidateRect(_status, nullptr, TRUE);
    if (_pin) ::InvalidateRect(_pin, nullptr, TRUE);
    _lineNumberDigits = 0;
    updateLineNumberWidth();
}

// ---------------------------------------------------------------------------
// Attaching to a document
// ---------------------------------------------------------------------------

void PrettyPanel::attach(UINT_PTR bufferId, HWND view, const std::wstring& title) {
    if (!_hwnd) return;
    const auto doc = static_cast<sptr_t>(::SendMessageW(view, SCI_GETDOCPOINTER, 0, 0));
    if (_doc != 0 && _buffer == bufferId && _doc == doc) {
        _view = view;
        return;
    }
    detach();

    _buffer = bufferId;
    _view = view;
    _doc = doc;
    // The mirror holds its own reference, so the document stays valid while attached.
    _mirror.call(SCI_SETDOCPOINTER, 0, _doc);

    _dockInfo = title;
    _dock.pszAddInfo = _dockInfo.c_str();
    if (_registered) ::SendMessageW(_npp._nppHandle, NPPM_DMMUPDATEDISPINFO, 0, reinterpret_cast<LPARAM>(_hwnd));

    applyTheme();
    loadFromDocument();
}

void PrettyPanel::detach() {
    if (_doc == 0) return;
    flush(true);
    stopTimers();
    _mirror.call(SCI_SETDOCPOINTER, 0, 0);  // releases our reference
    _doc = 0;
    _buffer = 0;
    _view = nullptr;
    _panelDirty = false;
    _pushDeferred = false;

    SyncGuard guard(_syncing);
    _editor.call(SCI_SETREADONLY, 0);
    _editor.call(SCI_CLEARALL);
    _editor.call(SCI_EMPTYUNDOBUFFER);
    setStatus(L"", false);
}

void PrettyPanel::loadFromDocument() {
    const std::string pretty = prettyPrint(_mirror.text(), formatOptions());
    {
        SyncGuard guard(_syncing);
        _editor.call(SCI_SETREADONLY, 0);
        _editor.call(SCI_SETCODEPAGE, _mirror.call(SCI_GETCODEPAGE));
        _editor.call(SCI_CLEARALL);
        _editor.call(SCI_APPENDTEXT, pretty.size(), reinterpret_cast<sptr_t>(pretty.data()));
        _editor.call(SCI_EMPTYUNDOBUFFER);
        _editor.call(SCI_SETSAVEPOINT);
        _editor.call(SCI_GOTOPOS, 0);
        _editor.call(SCI_SETREADONLY, _mirror.call(SCI_GETREADONLY));
    }
    _panelDirty = false;
    _pushDeferred = false;
    updateLineNumberWidth();
    validateNow();
    syncCaretFromView();
}

// ---------------------------------------------------------------------------
// Synchronisation
// ---------------------------------------------------------------------------

void PrettyPanel::flush(bool force) {
    if (_doc == 0) return;
    pushToDocument(force);
}

void PrettyPanel::refreshFromDocument() {
    ::KillTimer(_hwnd, kTimerDocumentToPanel);
    if (_doc == 0) return;
    // The document is the source of truth: a concurrent, not yet written panel edit is dropped.
    ::KillTimer(_hwnd, kTimerPanelToDocument);
    _panelDirty = false;
    _pushDeferred = false;
    setPanelText(prettyPrint(_mirror.text(), formatOptions()));
    _editor.call(SCI_SETREADONLY, _mirror.call(SCI_GETREADONLY));
    restartTimer(kTimerValidate, kValidateDelayMs);
}

void PrettyPanel::pushToDocument(bool force) {
    ::KillTimer(_hwnd, kTimerPanelToDocument);
    if (_doc == 0 || !_panelDirty) return;
    if (_mirror.call(SCI_GETREADONLY)) return;

    const std::string_view panelText = _editor.text();
    const std::string_view documentText = _mirror.text();
    TextEdit edit;
    if (hasUnterminatedTokens(panelText)) {
        // Whitespace is significant inside the open string: positions cannot be mapped.
        if (!force) {
            _pushDeferred = true;
            restartTimer(kTimerValidate, kValidateDelayMs);
            return;
        }
        edit = TextEdit{0, documentText.size(), compact(panelText)};
    } else {
        edit = diffContent(documentText, panelText);
    }
    _panelDirty = false;
    _pushDeferred = false;
    if (edit.empty()) return;

    SyncGuard guard(_syncing);
    _mirror.call(SCI_BEGINUNDOACTION);
    _mirror.replaceRange(edit.start, edit.end, edit.replacement);
    _mirror.call(SCI_ENDUNDOACTION);
}

void PrettyPanel::reformatPanel(bool force) {
    ::KillTimer(_hwnd, kTimerReformat);
    if (_doc == 0) return;
    pushToDocument(false);
    if (_panelDirty) return;  // deferred: unterminated string

    const std::string_view text = _editor.text();
    if (!validate(text, true).valid) return;  // never re-indent half written JSON

    const auto caret = static_cast<std::size_t>(_editor.call(SCI_GETCURRENTPOS));
    const auto anchor = static_cast<std::size_t>(_editor.call(SCI_GETANCHOR));
    if (!force && ::GetFocus() == _editor.hwnd()) {
        // Do not swallow the empty line the user just opened.
        const auto line = _editor.call(SCI_LINEFROMPOSITION, caret);
        const auto start = static_cast<std::size_t>(_editor.call(SCI_POSITIONFROMLINE, line));
        const auto end = static_cast<std::size_t>(_editor.call(SCI_GETLINEENDPOSITION, line));
        if (text.substr(start, end - start).find_first_not_of(" \t") == std::string_view::npos) return;
    }

    std::string pretty = prettyPrint(text, formatOptions());
    if (pretty == text) return;
    const std::size_t caretIndex = significantIndexAt(text, caret);
    const std::size_t anchorIndex = significantIndexAt(text, anchor);

    setPanelText(pretty);
    SyncGuard guard(_syncing);
    _editor.call(SCI_SETSEL, positionAfterSignificantIndex(pretty, anchorIndex),
                 static_cast<sptr_t>(positionAfterSignificantIndex(pretty, caretIndex)));
}

void PrettyPanel::reformatNow() {
    if (_doc == 0) return;
    pushToDocument(true);
    reformatPanel(true);
}

void PrettyPanel::setPanelText(std::string_view text) {
    const TextEdit edit = diffText(_editor.text(), text);
    if (edit.empty()) return;
    SyncGuard guard(_syncing);
    const auto readOnly = _editor.call(SCI_GETREADONLY);
    _editor.call(SCI_SETREADONLY, 0);
    // Programmatic changes are not undoable in the panel; undo history lives in the document.
    _editor.call(SCI_SETUNDOCOLLECTION, 0);
    _editor.replaceRange(edit.start, edit.end, edit.replacement);
    _editor.call(SCI_SETUNDOCOLLECTION, 1);
    _editor.call(SCI_EMPTYUNDOBUFFER);
    _editor.call(SCI_SETREADONLY, readOnly);
    updateLineNumberWidth();
}

// ---------------------------------------------------------------------------
// Validation / status
// ---------------------------------------------------------------------------

void PrettyPanel::validateNow() {
    ::KillTimer(_hwnd, kTimerValidate);
    if (_doc == 0) return;
    const std::string_view text = _editor.text();
    const ValidationResult result = validate(text, _settings.allowComments);

    _editor.call(SCI_SETINDICATORCURRENT, kErrorIndicator);
    _editor.call(SCI_INDICATORCLEARRANGE, 0, static_cast<sptr_t>(text.size()));

    if (!result.valid) {
        std::size_t begin = std::min(result.errorBegin, text.size());
        std::size_t end = std::min(std::max(result.errorEnd, begin + 1), text.size());
        if (begin == end && begin > 0) --begin;  // error at end of input: mark the last character
        _editor.call(SCI_INDICATORFILLRANGE, begin, static_cast<sptr_t>(end - begin));

        const auto line = _editor.call(SCI_LINEFROMPOSITION, result.errorBegin);
        const auto column = _editor.call(SCI_GETCOLUMN, result.errorBegin);
        std::wstring status = L"⚠ Ln " + std::to_wstring(line + 1) + L", Col " + std::to_wstring(column + 1) +
                              L": " + widen(result.message);
        if (_pushDeferred) status += L"  •  editor update paused";
        setStatus(status, true);
        return;
    }

    std::wstring status = result.empty ? L"Empty document" : L"✔ Valid JSON";
    if (result.valueCount > 1) status += L" (" + std::to_wstring(result.valueCount) + L" values)";
    status += L"  •  " + withThousands(static_cast<std::size_t>(_mirror.call(SCI_GETLENGTH))) + L" bytes compact";
    if (_mirror.call(SCI_GETREADONLY)) status += L"  •  read-only";
    setStatus(status, false);
}

void PrettyPanel::setPinned(bool pinned) {
    _pinned = pinned;
    if (!_pin) return;
    ::SetWindowTextW(_pin, pinned ? L"\u25CF Pinned" : L"Pin");
    ::InvalidateRect(_pin, nullptr, TRUE);
}

void PrettyPanel::setStatus(const std::wstring& text, bool error) {
    _statusError = error;
    if (_status) {
        const std::wstring padded = text.empty() ? text : L"  " + text;
        ::SetWindowTextW(_status, padded.c_str());
        ::InvalidateRect(_status, nullptr, TRUE);
    }
}

// ---------------------------------------------------------------------------
// Caret synchronisation
// ---------------------------------------------------------------------------

HWND PrettyPanel::viewShowingDocument() const {
    for (HWND view : {_view, _npp._scintillaMainHandle, _npp._scintillaSecondHandle}) {
        if (view && static_cast<sptr_t>(::SendMessageW(view, SCI_GETDOCPOINTER, 0, 0)) == _doc) return view;
    }
    return nullptr;
}

void PrettyPanel::onViewUpdateUI(HWND view, int updated) {
    if (_doc == 0 || _syncing || !_settings.syncCaret || !_visible) return;
    if (!(updated & SC_UPDATE_SELECTION) || ::GetFocus() != view) return;
    if (static_cast<sptr_t>(::SendMessageW(view, SCI_GETDOCPOINTER, 0, 0)) != _doc) return;
    _view = view;
    restartTimer(kTimerCaretFromView, kCaretDelayMs);
}

void PrettyPanel::syncCaretFromView() {
    ::KillTimer(_hwnd, kTimerCaretFromView);
    if (_doc == 0 || _panelDirty || !_settings.syncCaret) return;
    HWND view = viewShowingDocument();
    if (!view) return;
    const auto caret = static_cast<std::size_t>(::SendMessageW(view, SCI_GETCURRENTPOS, 0, 0));
    const auto anchor = static_cast<std::size_t>(::SendMessageW(view, SCI_GETANCHOR, 0, 0));
    const std::string_view documentText = _mirror.text();
    const std::string_view panelText = _editor.text();
    const std::size_t panelCaret = positionAfterSignificantIndex(panelText, significantIndexAt(documentText, caret));
    const std::size_t panelAnchor =
        anchor == caret ? panelCaret : positionAfterSignificantIndex(panelText, significantIndexAt(documentText, anchor));

    SyncGuard guard(_syncing);
    _editor.call(SCI_SETSEL, panelAnchor, static_cast<sptr_t>(panelCaret));
    _editor.call(SCI_SCROLLCARET);
}

void PrettyPanel::syncCaretFromPanel() {
    ::KillTimer(_hwnd, kTimerCaretFromPanel);
    if (_doc == 0 || !_settings.syncCaret) return;
    pushToDocument(false);
    if (_panelDirty) return;
    HWND view = viewShowingDocument();
    if (!view) return;

    const std::string_view panelText = _editor.text();
    const std::string_view documentText = _mirror.text();
    const auto caret = static_cast<std::size_t>(_editor.call(SCI_GETCURRENTPOS));
    const auto anchor = static_cast<std::size_t>(_editor.call(SCI_GETANCHOR));
    const std::size_t docCaret = positionAfterSignificantIndex(documentText, significantIndexAt(panelText, caret));
    const std::size_t docAnchor =
        anchor == caret ? docCaret : positionAfterSignificantIndex(documentText, significantIndexAt(panelText, anchor));

    SyncGuard guard(_syncing);
    ::SendMessageW(view, SCI_SETSEL, docAnchor, static_cast<LPARAM>(docCaret));
    ::SendMessageW(view, SCI_SCROLLCARET, 0, 0);
}

// ---------------------------------------------------------------------------
// Editor conveniences
// ---------------------------------------------------------------------------

void PrettyPanel::autoIndent() {
    const auto pos = _editor.call(SCI_GETCURRENTPOS);
    const auto line = _editor.call(SCI_LINEFROMPOSITION, pos);
    if (line == 0) return;
    const auto previous = line - 1;
    sptr_t indent = _editor.call(SCI_GETLINEINDENTATION, previous);

    // Last non blank character of the previous line.
    char last = 0;
    const auto lineStart = _editor.call(SCI_POSITIONFROMLINE, previous);
    for (auto p = _editor.call(SCI_GETLINEENDPOSITION, previous); p > lineStart; --p) {
        const char c = static_cast<char>(_editor.call(SCI_GETCHARAT, p - 1));
        if (c != ' ' && c != '\t') {
            last = c;
            break;
        }
    }
    const bool opens = last == '{' || last == '[';
    const sptr_t unit = _editor.call(SCI_GETINDENT);
    _editor.call(SCI_SETLINEINDENTATION, line, opens ? indent + unit : indent);
    const auto caret = _editor.call(SCI_GETLINEINDENTPOSITION, line);
    _editor.call(SCI_GOTOPOS, caret);

    // "{|}" + Enter: move the closing bracket to its own line.
    const char next = static_cast<char>(_editor.call(SCI_GETCHARAT, caret));
    if (opens && (next == '}' || next == ']')) {
        _editor.call(SCI_INSERTTEXT, caret, reinterpret_cast<sptr_t>("\n"));
        _editor.call(SCI_SETLINEINDENTATION, line + 1, indent);
        _editor.call(SCI_GOTOPOS, _editor.call(SCI_GETLINEENDPOSITION, line));
    }
}

void PrettyPanel::highlightBraces() {
    auto isBrace = [](int c) { return c == '{' || c == '}' || c == '[' || c == ']'; };
    sptr_t pos = _editor.call(SCI_GETCURRENTPOS);
    sptr_t brace = -1;
    if (pos > 0 && isBrace(static_cast<int>(_editor.call(SCI_GETCHARAT, pos - 1)))) {
        brace = pos - 1;
    } else if (isBrace(static_cast<int>(_editor.call(SCI_GETCHARAT, pos)))) {
        brace = pos;
    }
    if (brace < 0) {
        _editor.call(SCI_BRACEHIGHLIGHT, static_cast<uptr_t>(-1), -1);
        return;
    }
    const sptr_t match = _editor.call(SCI_BRACEMATCH, brace, 0);
    if (match < 0) {
        _editor.call(SCI_BRACEBADLIGHT, brace);
    } else {
        _editor.call(SCI_BRACEHIGHLIGHT, brace, match);
    }
}

void PrettyPanel::updateLineNumberWidth() {
    int digits = 1;
    for (auto lines = _editor.call(SCI_GETLINECOUNT); lines >= 10; lines /= 10) ++digits;
    digits = std::max(digits, 3);
    if (digits == _lineNumberDigits) return;
    _lineNumberDigits = digits;
    const std::string sample = "_" + std::string(static_cast<std::size_t>(digits), '9');
    const auto width = _editor.call(SCI_TEXTWIDTH, STYLE_LINENUMBER, reinterpret_cast<sptr_t>(sample.c_str()));
    _editor.call(SCI_SETMARGINWIDTHN, kMarginNumbers, width);
}

FormatOptions PrettyPanel::formatOptions() const {
    FormatOptions options;
    options.indentSize = _settings.indentSize;
    options.useTabs = _settings.useTabs;
    options.spaceAfterColon = _settings.spaceAfterColon;
    return options;
}

// ---------------------------------------------------------------------------
// Notifications
// ---------------------------------------------------------------------------

void PrettyPanel::onEditorNotify(const SCNotification& n) {
    switch (n.nmhdr.code) {
        case SCN_MODIFIED:
            if (_syncing || !(n.modificationType & (SC_MOD_INSERTTEXT | SC_MOD_DELETETEXT))) break;
            _panelDirty = true;
            restartTimer(kTimerPanelToDocument, _settings.panelToDocumentDelayMs);
            restartTimer(kTimerValidate, kValidateDelayMs);
            if (_settings.reformatDelayMs > 0) restartTimer(kTimerReformat, _settings.reformatDelayMs);
            if (n.linesAdded != 0) updateLineNumberWidth();
            break;
        case SCN_CHARADDED:
            if (n.ch == '\n') autoIndent();
            break;
        case SCN_UPDATEUI:
            highlightBraces();
            if (!_syncing && _settings.syncCaret && (n.updated & SC_UPDATE_SELECTION) &&
                ::GetFocus() == _editor.hwnd()) {
                restartTimer(kTimerCaretFromPanel, kCaretDelayMs);
            }
            break;
        case SCN_FOCUSOUT:
            pushToDocument(false);
            break;
        default:
            break;
    }
}

void PrettyPanel::onMirrorNotify(const SCNotification& n) {
    if (n.nmhdr.code != SCN_MODIFIED || _syncing) return;
    if (!(n.modificationType & (SC_MOD_INSERTTEXT | SC_MOD_DELETETEXT))) return;
    restartTimer(kTimerDocumentToPanel, _settings.documentToPanelDelayMs);
}

void PrettyPanel::onTimer(UINT_PTR id) {
    switch (id) {
        case kTimerDocumentToPanel: refreshFromDocument(); break;
        case kTimerPanelToDocument: pushToDocument(false); break;
        case kTimerReformat: reformatPanel(false); break;
        case kTimerValidate: validateNow(); break;
        case kTimerCaretFromView: syncCaretFromView(); break;
        case kTimerCaretFromPanel: syncCaretFromPanel(); break;
        default: ::KillTimer(_hwnd, id); break;
    }
}

void PrettyPanel::restartTimer(UINT_PTR id, int delayMs) {
    if (_hwnd) ::SetTimer(_hwnd, id, static_cast<UINT>(std::max(1, delayMs)), nullptr);
}

void PrettyPanel::stopTimers() {
    for (UINT_PTR id = kTimerDocumentToPanel; id <= kTimerCaretFromPanel; ++id) ::KillTimer(_hwnd, id);
}

// ---------------------------------------------------------------------------
// Window procedure
// ---------------------------------------------------------------------------

void PrettyPanel::layout() {
    RECT rc{};
    ::GetClientRect(_hwnd, &rc);
    const int statusHeight = scale(_hwnd, 22);
    const int width = rc.right - rc.left;
    const int height = std::max(0, static_cast<int>(rc.bottom - rc.top) - statusHeight);
    const int pinWidth = std::min(width, scale(_hwnd, 64));
    ::MoveWindow(_editor.hwnd(), 0, 0, width, height, TRUE);
    ::MoveWindow(_status, 0, height, width - pinWidth, statusHeight, TRUE);
    ::MoveWindow(_pin, width - pinWidth, height, pinWidth, statusHeight, TRUE);
}

LRESULT CALLBACK PrettyPanel::windowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    PrettyPanel* self = nullptr;
    if (msg == WM_NCCREATE) {
        self = static_cast<PrettyPanel*>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams);
        ::SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        self->_hwnd = hwnd;
    } else {
        self = reinterpret_cast<PrettyPanel*>(::GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }
    return self ? self->handleMessage(msg, wParam, lParam) : ::DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT PrettyPanel::handleMessage(UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_SIZE:
            if (_editor) layout();
            return 0;
        case WM_SETFOCUS:
            if (_editor) ::SetFocus(_editor.hwnd());
            return 0;
        case WM_TIMER:
            onTimer(static_cast<UINT_PTR>(wParam));
            return 0;
        case WM_ERASEBKGND: {
            RECT rc{};
            ::GetClientRect(_hwnd, &rc);
            ::FillRect(reinterpret_cast<HDC>(wParam), &rc, _backBrush ? _backBrush : ::GetSysColorBrush(COLOR_BTNFACE));
            return 1;
        }
        case WM_COMMAND:
            if (reinterpret_cast<HWND>(lParam) == _pin && HIWORD(wParam) == STN_CLICKED) {
                if (_onPinClicked) _onPinClicked();
                return 0;
            }
            break;
        case WM_SETCURSOR:
            if (reinterpret_cast<HWND>(wParam) == _pin) {
                ::SetCursor(::LoadCursorW(nullptr, IDC_HAND));
                return TRUE;
            }
            break;
        case WM_CTLCOLORSTATIC:
            if (reinterpret_cast<HWND>(lParam) == _pin) {
                auto dc = reinterpret_cast<HDC>(wParam);
                ::SetTextColor(dc, _pinned ? RGB(0x30, 0x8C, 0xE8) : _statusFore);
                ::SetBkColor(dc, _statusBack);
                return reinterpret_cast<LRESULT>(_backBrush ? _backBrush : ::GetSysColorBrush(COLOR_BTNFACE));
            }
            if (reinterpret_cast<HWND>(lParam) == _status) {
                auto dc = reinterpret_cast<HDC>(wParam);
                ::SetTextColor(dc, _statusError ? RGB(0xE0, 0x30, 0x30) : _statusFore);
                ::SetBkColor(dc, _statusBack);
                return reinterpret_cast<LRESULT>(_backBrush ? _backBrush : ::GetSysColorBrush(COLOR_BTNFACE));
            }
            break;
        case WM_NOTIFY: {
            const auto* header = reinterpret_cast<const NMHDR*>(lParam);
            if (header->hwndFrom == _editor.hwnd()) {
                onEditorNotify(*reinterpret_cast<const SCNotification*>(lParam));
                return 0;
            }
            if (header->hwndFrom == _mirror.hwnd()) {
                onMirrorNotify(*reinterpret_cast<const SCNotification*>(lParam));
                return 0;
            }
            if (LOWORD(header->code) == DMN_CLOSE) {
                // Closed via the panel's [x]: Notepad++ hides the panel itself.
                _visible = false;
                detach();
                if (_onClosedByUser) _onClosedByUser();
                return 0;
            }
            break;
        }
        default:
            break;
    }
    return ::DefWindowProcW(_hwnd, msg, wParam, lParam);
}

}  // namespace jpp
