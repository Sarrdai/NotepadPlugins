// JSON++ - Notepad++ plugin
// Copyright (C) 2026 JSON++ contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Plugin entry points, menu and activation logic.

#include <windows.h>

#include <memory>
#include <string>
#include <unordered_map>

#include "JsonCore.h"
#include "PluginInterface.h"
#include "PrettyPanel.h"
#include "Settings.h"

namespace {

constexpr wchar_t kPluginName[] = L"JSON++";
constexpr wchar_t kVersion[] = L"1.0.0";

enum Command : int {
    kCmdTogglePanel,
    kCmdPin,
    kCmdAutoActivate,
    kCmdSyncCaret,
    kCmdSeparator1,
    kCmdFormatNow,
    kCmdSettings,
    kCmdSeparator2,
    kCmdAbout,
    kCmdCount
};

enum class Mode { Auto, On, Off };

struct BufferState {
    Mode mode = Mode::Auto;
    bool detectedCompact = false;  // sticky: once detected, stays active for the session
};

HINSTANCE g_module = nullptr;
NppData g_npp{};
FuncItem g_funcs[kCmdCount];
jpp::Settings g_settings;
std::wstring g_iniPath;
std::unique_ptr<jpp::PrettyPanel> g_panel;
std::unordered_map<UINT_PTR, BufferState> g_buffers;
bool g_ready = false;
bool g_pinned = false;          // the pretty view keeps its document when other tabs are activated
UINT_PTR g_pinnedBuffer = 0;

LRESULT npp(UINT msg, WPARAM wParam = 0, LPARAM lParam = 0) {
    return ::SendMessageW(g_npp._nppHandle, msg, wParam, lParam);
}

HWND currentView() {
    int which = -1;
    npp(NPPM_GETCURRENTSCINTILLA, 0, reinterpret_cast<LPARAM>(&which));
    return which == 1 ? g_npp._scintillaSecondHandle : g_npp._scintillaMainHandle;
}

std::wstring bufferPath(UINT_PTR bufferId) {
    const auto length = npp(NPPM_GETFULLPATHFROMBUFFERID, bufferId, 0);
    if (length <= 0) return {};
    std::wstring path(static_cast<std::size_t>(length) + 1, L'\0');
    npp(NPPM_GETFULLPATHFROMBUFFERID, bufferId, reinterpret_cast<LPARAM>(path.data()));
    path.resize(static_cast<std::size_t>(length));
    return path;
}

std::wstring fileName(const std::wstring& path) {
    const auto slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? path : path.substr(slash + 1);
}

bool isJsonBuffer(UINT_PTR bufferId) {
    const auto lang = static_cast<int>(npp(NPPM_GETBUFFERLANGTYPE, bufferId, 0));
    return lang == L_JSON || lang == L_JSON5 || g_settings.matchesExtension(bufferPath(bufferId));
}

bool documentLooksCompact(HWND view) {
    auto call = [view](UINT msg, WPARAM w = 0, LPARAM l = 0) { return ::SendMessageW(view, msg, w, l); };
    const auto length = static_cast<std::size_t>(call(SCI_GETLENGTH));
    if (length > static_cast<std::size_t>(g_settings.maxFileSizeMB) * 1024 * 1024) return false;

    auto lines = static_cast<std::size_t>(call(SCI_GETLINECOUNT));
    // A trailing line break does not count as an extra line.
    if (lines > 1 && static_cast<std::size_t>(call(SCI_POSITIONFROMLINE, lines - 1)) == length) --lines;

    const std::size_t headLength = std::min<std::size_t>(length, 4096);
    const auto* head = reinterpret_cast<const char*>(call(SCI_GETRANGEPOINTER, 0, static_cast<LPARAM>(headLength)));
    if (!head) return false;

    jpp::DetectOptions options;
    options.minLength = static_cast<std::size_t>(g_settings.minLength);
    options.minAverageLineLength = static_cast<std::size_t>(g_settings.minAverageLineLength);
    return jpp::looksCompact(std::string_view(head, headLength), length, lines, options);
}

void updateMenuChecks() {
    const bool panelShown = g_panel && g_panel->isVisible() && g_panel->isAttached();
    npp(NPPM_SETMENUITEMCHECK, g_funcs[kCmdTogglePanel]._cmdID, panelShown);
    npp(NPPM_SETMENUITEMCHECK, g_funcs[kCmdPin]._cmdID, g_pinned);
    npp(NPPM_SETMENUITEMCHECK, g_funcs[kCmdAutoActivate]._cmdID, g_settings.autoActivate);
    npp(NPPM_SETMENUITEMCHECK, g_funcs[kCmdSyncCaret]._cmdID, g_settings.syncCaret);
}

void setPinned(bool pinned) {
    g_pinned = pinned;
    g_pinnedBuffer = pinned && g_panel ? g_panel->buffer() : 0;
    if (g_panel) g_panel->setPinned(pinned);
    updateMenuChecks();
}

void evaluateActiveBuffer();

// Pins the pretty view to its document (opening it for the active one first) or releases the pin.
void togglePin();

void onPanelClosedByUser() {
    // Remember the decision for the shown buffer so switching tabs does not reopen it.
    const auto bufferId = g_pinned ? g_pinnedBuffer : static_cast<UINT_PTR>(npp(NPPM_GETCURRENTBUFFERID));
    g_buffers[bufferId].mode = Mode::Off;
    setPinned(false);
}

jpp::PrettyPanel* ensurePanel() {
    if (!g_panel) {
        auto panel = std::make_unique<jpp::PrettyPanel>(g_module, g_npp, g_settings);
        if (!panel->create(kCmdTogglePanel, &onPanelClosedByUser)) return nullptr;
        panel->setPinHandler(&togglePin);
        g_panel = std::move(panel);
    }
    return g_panel.get();
}

// Decides whether the pretty view is shown for the active buffer: it closes for other documents
// and reopens for documents that have it switched on. A pinned view keeps its document.
void evaluateActiveBuffer() {
    if (!g_ready) return;
    if (g_pinned && g_panel && g_panel->isAttached()) {
        updateMenuChecks();
        return;
    }
    const auto bufferId = static_cast<UINT_PTR>(npp(NPPM_GETCURRENTBUFFERID));
    HWND view = currentView();
    BufferState& state = g_buffers[bufferId];

    bool show = false;
    switch (state.mode) {
        case Mode::On: show = true; break;
        case Mode::Off: show = false; break;
        case Mode::Auto:
            if (g_settings.autoActivate && isJsonBuffer(bufferId)) {
                if (!state.detectedCompact) state.detectedCompact = documentLooksCompact(view);
                show = state.detectedCompact;
            }
            break;
    }

    if (show) {
        if (jpp::PrettyPanel* panel = ensurePanel()) {
            panel->attach(bufferId, view, fileName(bufferPath(bufferId)));
            panel->show();
        }
    } else if (g_panel) {
        g_panel->detach();
        g_panel->hide();
    }
    updateMenuChecks();
}

void loadSettings() {
    wchar_t dir[MAX_PATH] = {};
    npp(NPPM_GETPLUGINSCONFIGDIR, MAX_PATH, reinterpret_cast<LPARAM>(dir));
    g_iniPath = std::wstring(dir) + L"\\JsonPlusPlus.ini";
    g_settings.load(g_iniPath);
}

// ---------------------------------------------------------------------------
// Menu commands
// ---------------------------------------------------------------------------

void cmdTogglePanel() {
    // Notepad++ also invokes this command while restoring docked panels at startup;
    // visibility is driven by the active buffer instead.
    if (!g_ready) return;
    if (g_pinned) {  // hides the pinned view
        g_buffers[g_pinnedBuffer].mode = Mode::Off;
        setPinned(false);
        evaluateActiveBuffer();
        return;
    }
    const auto bufferId = static_cast<UINT_PTR>(npp(NPPM_GETCURRENTBUFFERID));
    const bool shownForBuffer = g_panel && g_panel->isVisible() && g_panel->buffer() == bufferId;
    g_buffers[bufferId].mode = shownForBuffer ? Mode::Off : Mode::On;
    evaluateActiveBuffer();
}

void togglePin() {
    if (!g_ready) return;
    if (g_pinned) {
        setPinned(false);
        evaluateActiveBuffer();  // the active document decides again
        return;
    }
    if (!g_panel || !g_panel->isVisible() || !g_panel->isAttached()) {
        g_buffers[static_cast<UINT_PTR>(npp(NPPM_GETCURRENTBUFFERID))].mode = Mode::On;
        evaluateActiveBuffer();
    }
    if (g_panel && g_panel->isVisible() && g_panel->isAttached()) setPinned(true);
}

void cmdPin() { togglePin(); }

void cmdAutoActivate() {
    g_settings.autoActivate = !g_settings.autoActivate;
    g_settings.save(g_iniPath);
    evaluateActiveBuffer();
}

void cmdSyncCaret() {
    g_settings.syncCaret = !g_settings.syncCaret;
    g_settings.save(g_iniPath);
    updateMenuChecks();
}

void cmdFormatNow() {
    if (g_panel && g_panel->isAttached()) g_panel->reformatNow();
}

void cmdSettings() {
    if (::GetFileAttributesW(g_iniPath.c_str()) == INVALID_FILE_ATTRIBUTES) g_settings.save(g_iniPath);
    npp(NPPM_DOOPEN, 0, reinterpret_cast<LPARAM>(g_iniPath.c_str()));
}

void cmdAbout() {
    const std::wstring text = std::wstring(kPluginName) + L" " + kVersion +
                              L"\n\nShows compact JSON documents pretty printed in a synchronised, editable panel.\n"
                              L"The document itself stays compact and is saved compact.\n\n"
                              L"Settings: " + g_iniPath +
                              L"\n(changes are applied when the file is saved in Notepad++)";
    ::MessageBoxW(g_npp._nppHandle, text.c_str(), kPluginName, MB_OK | MB_ICONINFORMATION);
}

void setCommand(int index, const wchar_t* name, PFUNCPLUGINCMD fn) {
    wcsncpy_s(g_funcs[index]._itemName, name, _TRUNCATE);
    g_funcs[index]._pFunc = fn;
    g_funcs[index]._init2Check = false;
    g_funcs[index]._pShKey = nullptr;
}

}  // namespace

// ---------------------------------------------------------------------------
// Notepad++ plugin interface
// ---------------------------------------------------------------------------

BOOL APIENTRY DllMain(HINSTANCE module, DWORD reason, LPVOID /*reserved*/) {
    if (reason == DLL_PROCESS_ATTACH) {
        g_module = module;
        ::DisableThreadLibraryCalls(module);
    }
    return TRUE;
}

extern "C" __declspec(dllexport) void setInfo(NppData data) {
    g_npp = data;
    setCommand(kCmdTogglePanel, L"Show pretty view for current file", cmdTogglePanel);
    setCommand(kCmdPin, L"Pin pretty view to current file", cmdPin);
    setCommand(kCmdAutoActivate, L"Open automatically for compact JSON", cmdAutoActivate);
    setCommand(kCmdSyncCaret, L"Synchronise caret", cmdSyncCaret);
    setCommand(kCmdSeparator1, L"-", nullptr);
    setCommand(kCmdFormatNow, L"Re-indent pretty view now", cmdFormatNow);
    setCommand(kCmdSettings, L"Settings...", cmdSettings);
    setCommand(kCmdSeparator2, L"-", nullptr);
    setCommand(kCmdAbout, L"About", cmdAbout);
}

extern "C" __declspec(dllexport) const wchar_t* getName() { return kPluginName; }

extern "C" __declspec(dllexport) FuncItem* getFuncsArray(int* count) {
    *count = kCmdCount;
    return g_funcs;
}

extern "C" __declspec(dllexport) void beNotified(SCNotification* n) {
    const UINT code = n->nmhdr.code;
    const auto hwndFrom = static_cast<HWND>(n->nmhdr.hwndFrom);

    if (hwndFrom == g_npp._scintillaMainHandle || hwndFrom == g_npp._scintillaSecondHandle) {
        if (code == SCN_UPDATEUI && g_panel) g_panel->onViewUpdateUI(hwndFrom, n->updated);
        return;
    }
    if (hwndFrom != g_npp._nppHandle) return;

    const auto bufferId = static_cast<UINT_PTR>(n->nmhdr.idFrom);
    switch (code) {
        case NPPN_READY:
            loadSettings();
            g_ready = true;
            evaluateActiveBuffer();
            break;
        case NPPN_BUFFERACTIVATED:
        case NPPN_LANGCHANGED:
            evaluateActiveBuffer();
            break;
        case NPPN_FILEBEFORESAVE:
            // Pending edits of the pretty view must be part of the saved (compact) file.
            if (g_panel && g_panel->buffer() == bufferId) g_panel->flush(true);
            break;
        case NPPN_FILESAVED:
            if (!g_iniPath.empty() && _wcsicmp(bufferPath(bufferId).c_str(), g_iniPath.c_str()) == 0) {
                g_settings.load(g_iniPath);
                if (g_panel) g_panel->onSettingsChanged();
            }
            evaluateActiveBuffer();  // "Save as" may have changed the extension
            break;
        case NPPN_FILEBEFORECLOSE:
            if (g_panel && g_panel->buffer() == bufferId) {
                if (g_pinned) setPinned(false);
                g_panel->detach();
            }
            break;
        case NPPN_FILECLOSED:
            g_buffers.erase(bufferId);
            evaluateActiveBuffer();  // a closed pinned document may have been in the background
            break;
        case NPPN_WORDSTYLESUPDATED:
        case NPPN_DARKMODECHANGED:
            if (g_panel) g_panel->applyTheme();
            break;
        case NPPN_BEFORESHUTDOWN:
            if (g_panel) g_panel->flush(true);
            break;
        case NPPN_SHUTDOWN:
            g_ready = false;
            if (g_panel) g_panel->detach();
            break;
        default:
            break;
    }
}

extern "C" __declspec(dllexport) LRESULT messageProc(UINT /*msg*/, WPARAM /*wParam*/, LPARAM /*lParam*/) {
    return TRUE;
}

extern "C" __declspec(dllexport) BOOL isUnicode() { return TRUE; }
