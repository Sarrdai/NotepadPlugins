// Notepad++ plugin entry points, menu and notification handling.
#include "plugin.h"

#include <algorithm>
#include <vector>

#include "PasteFiles.h"
#include "code_highlight.h"
#include "commands.h"
#include "editor.h"
#include "preview_panel.h"
#include "tool_panel.h"
#include "util.h"

NppData g_npp;
HINSTANCE g_module = nullptr;
Settings g_settings;

namespace {

constexpr UINT WM_APP_SMART_ENTER = WM_APP + 10;
constexpr wchar_t kMessageWindowClass[] = L"MarkdownLiveMessageWindow";

FuncItem g_menu[MenuCount];
bool g_menuInitialized = false;
bool g_nppReady = false;
bool g_autoOpened = false;  // the preview was opened by the "auto-open" option
bool g_autoClosed = false;  // the preview was hidden because another document was activated
HWND g_messageWindow = nullptr;
ToolbarIconsWithDarkMode g_toolbarIcons;

ShortcutKey g_keyPreview = {true, false, true, 'M'};  // Ctrl+Shift+M
// Ctrl+B / Ctrl+I / Ctrl+Shift+B are taken by Notepad++ itself, so "emphasis" keys are used.
ShortcutKey g_keyBold = {true, false, true, 'E'};    // Ctrl+Shift+E
ShortcutKey g_keyItalic = {true, false, true, 'I'};  // Ctrl+Shift+I

PreviewPanel& panel() {
    return PreviewPanel::instance();
}

std::wstring iniPath() {
    return configDirectory() + L"\\MarkdownLive.ini";
}

// Edit > Paste with files copied in Explorer: opens them when Markdown files are among them.
bool openPastedFiles(const std::vector<std::wstring>& files) {
    if (std::none_of(files.begin(), files.end(), isMarkdownPath)) return false;
    for (const std::wstring& file : files) {
        if (!nppshared::isDirectory(file))
            SendMessageW(g_npp.nppHandle, NPPM_DOOPEN, 0, reinterpret_cast<LPARAM>(file.c_str()));
    }
    return true;
}

// ---- menu commands ------------------------------------------------------------------------

void focusEditor() {
    SetFocus(Editor::currentHandle());
}

void cmdTogglePreview() {
    g_autoOpened = false;
    g_autoClosed = false;
    panel().toggle();
    if (g_nppReady) focusEditor();
}

void cmdExportHtml() {
    panel().exportHtml();
}
void cmdExportPdf() {
    panel().exportPdf();
}

void cmdSyncScroll() {
    g_settings.syncScroll = !g_settings.syncScroll;
    setMenuCheck(MenuSyncScroll, g_settings.syncScroll);
    g_settings.save();
    panel().onSyncSettingChanged();
}

void cmdSmartEnter() {
    g_settings.smartEnter = !g_settings.smartEnter;
    setMenuCheck(MenuSmartEnter, g_settings.smartEnter);
    g_settings.save();
}

// Shows / hides the preview after another document was activated: it always closes for other
// files and reopens for the next Markdown file ("open automatically" also opens it the first
// time). A pinned preview is never touched.
void applyAutoOpen() {
    if (!g_nppReady || panel().pinned()) return;
    const std::wstring path = currentFilePath();
    if (isMarkdownPath(path)) {
        if (!panel().visible() && (g_settings.autoOpen || g_autoClosed)) {
            g_autoOpened = !g_autoClosed;  // reopened after an automatic close: it is the user's preview again
            g_autoClosed = false;
            panel().show();
            focusEditor();
        }
        return;
    }
    if (!panel().visible()) return;
    if (g_settings.autoOpen && g_autoOpened) {
        panel().hide();
    } else if (!extensionOf(path).empty()) {  // unsaved documents keep the preview
        panel().hide();
        g_autoClosed = true;
    }
}

void cmdAutoOpen() {
    g_settings.autoOpen = !g_settings.autoOpen;
    setMenuCheck(MenuAutoOpen, g_settings.autoOpen);
    g_settings.save();
    applyAutoOpen();
}

void cmdPinPreview() {
    g_autoClosed = false;
    panel().togglePin();
    applyAutoOpen();  // after unpinning, the current document decides again
}

void cmdHighlightCode() {
    g_settings.highlightCode = !g_settings.highlightCode;
    setMenuCheck(MenuHighlightCode, g_settings.highlightCode);
    g_settings.save();
    CodeHighlighter::instance().setEnabled(g_settings.highlightCode);
}

void cmdPanelTop() {
    setPanelPosition(PanelPosition::Top);
}
void cmdPanelBottom() {
    setPanelPosition(PanelPosition::Bottom);
}
void cmdPanelHidden() {
    setPanelPosition(PanelPosition::Hidden);
}

void updatePanelMenu() {
    setMenuCheck(MenuPanelTop, g_settings.panel == PanelPosition::Top);
    setMenuCheck(MenuPanelBottom, g_settings.panel == PanelPosition::Bottom);
    setMenuCheck(MenuPanelHidden, g_settings.panel == PanelPosition::Hidden);
}

void cmdAbout() {
    showMessage(std::wstring(kPluginName) + L" " + kPluginVersion +
                L"\n\nLive Markdown preview and editing helpers for Notepad++.\n\n"
                L"Preview: GitHub Flavored Markdown, syntax highlighting, math (KaTeX),\n"
                L"diagrams (Mermaid), footnotes, task lists, alerts and more.\n\n"
                L"Tip: shortcuts for all commands can be assigned in\n"
                L"Settings > Shortcut Mapper > Plugin commands.");
}

void addItem(MenuIndex index, const wchar_t* name, PFUNCPLUGINCMD fn, ShortcutKey* key = nullptr,
             bool checked = false) {
    wcsncpy(g_menu[index].itemName, name, kMenuItemSize - 1);
    g_menu[index].pFunc = fn;
    g_menu[index].pShKey = key;
    g_menu[index].init2Check = checked;
}

void initMenu() {
    if (g_menuInitialized) return;
    g_menuInitialized = true;
    addItem(MenuTogglePreview, L"Toggle Live Preview", cmdTogglePreview, &g_keyPreview);
    addItem(MenuBold, L"Bold", commands::bold, &g_keyBold);
    addItem(MenuItalic, L"Italic", commands::italic, &g_keyItalic);
    addItem(MenuStrikethrough, L"Strikethrough", commands::strikethrough);
    addItem(MenuInlineCode, L"Inline Code", commands::inlineCode);
    addItem(MenuCodeBlock, L"Code Block", commands::codeBlock);
    addItem(MenuHeadingMore, L"Heading: Add #", commands::headingMore);
    addItem(MenuHeadingLess, L"Heading: Remove #", commands::headingLess);
    addItem(MenuBulletList, L"Toggle Bullet List", commands::bulletList);
    addItem(MenuNumberedList, L"Toggle Numbered List", commands::numberedList);
    addItem(MenuTask, L"Task: Add / Check / Uncheck", commands::task);
    addItem(MenuQuote, L"Toggle Blockquote", commands::quote);
    addItem(MenuLink, L"Insert Link", commands::link);
    addItem(MenuImage, L"Insert Image...", commands::image);
    addItem(MenuPasteImage, L"Paste Image from Clipboard", commands::pasteImage);
    addItem(MenuInsertTable, L"Insert Table", commands::insertTable);
    addItem(MenuFormatTable, L"Format Table", commands::formatTable);
    addItem(MenuTableRowAbove, L"Table: Insert Row Above", commands::tableRowAbove);
    addItem(MenuTableRowBelow, L"Table: Insert Row Below", commands::tableRowBelow);
    addItem(MenuTableDeleteRow, L"Table: Delete Row", commands::tableDeleteRow);
    addItem(MenuTableColumnLeft, L"Table: Insert Column Left", commands::tableColumnLeft);
    addItem(MenuTableColumnRight, L"Table: Insert Column Right", commands::tableColumnRight);
    addItem(MenuTableDeleteColumn, L"Table: Delete Column", commands::tableDeleteColumn);
    addItem(MenuToc, L"Insert / Update Table of Contents", commands::tableOfContents);
    addItem(MenuExportHtml, L"Export as HTML...", cmdExportHtml);
    addItem(MenuExportPdf, L"Export as PDF...", cmdExportPdf);
    addItem(MenuPinPreview, L"Pin Live Preview to Current File", cmdPinPreview);
    addItem(MenuSyncScroll, L"Synchronize Scrolling", cmdSyncScroll, nullptr, g_settings.syncScroll);
    addItem(MenuSmartEnter, L"Continue Lists on Enter", cmdSmartEnter, nullptr, g_settings.smartEnter);
    addItem(MenuAutoOpen, L"Open Preview Automatically for Markdown Files", cmdAutoOpen, nullptr, g_settings.autoOpen);
    addItem(MenuHighlightCode, L"Highlight Code Blocks in Editor", cmdHighlightCode, nullptr, g_settings.highlightCode);
    addItem(MenuPanelTop, L"Tool Panel: Top", cmdPanelTop, nullptr, g_settings.panel == PanelPosition::Top);
    addItem(MenuPanelBottom, L"Tool Panel: Bottom", cmdPanelBottom, nullptr, g_settings.panel == PanelPosition::Bottom);
    addItem(MenuPanelHidden, L"Tool Panel: Hidden", cmdPanelHidden, nullptr, g_settings.panel == PanelPosition::Hidden);
    addItem(MenuAbout, L"About", cmdAbout);
    // entries without a function (MenuSep*) are rendered as separators
}

// ---- helper window --------------------------------------------------------------------------

LRESULT CALLBACK messageWindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_APP_SMART_ENTER) {
        commands::smartEnter();
        return 0;
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

void createMessageWindow() {
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = messageWindowProc;
    wc.hInstance = g_module;
    wc.lpszClassName = kMessageWindowClass;
    RegisterClassExW(&wc);
    g_messageWindow =
        CreateWindowExW(0, kMessageWindowClass, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, g_module, nullptr);
}

bool smartEnterApplies() {
    if (!g_settings.smartEnter) return false;
    const std::wstring path = currentFilePath();
    if (isMarkdownPath(path)) return true;
    return extensionOf(path).empty() && panel().visible();  // unsaved document with open preview
}

// ---- icons ------------------------------------------------------------------------------------

// 16x16 "Markdown mark": a frame with "M" and a down arrow.
const char* const kIconRows[16] = {
    "................", "................", "................", ".##############.",
    "#..............#", "#.#...#...#....#", "#.##.##...#....#", "#.#.#.#...#....#",
    "#.#...#.#.#.#..#", "#.#...#..###...#", "#.#...#...#....#", "#..............#",
    ".##############.", "................", "................", "................",
};

HBITMAP createGlyphBitmap(int size, COLORREF color, bool transparent, COLORREF background) {
    BITMAPINFO info = {};
    info.bmiHeader.biSize = sizeof(info.bmiHeader);
    info.bmiHeader.biWidth = size;
    info.bmiHeader.biHeight = -size;  // top-down
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    DWORD* pixels = nullptr;
    const HBITMAP bitmap =
        CreateDIBSection(nullptr, &info, DIB_RGB_COLORS, reinterpret_cast<void**>(&pixels), nullptr, 0);
    if (!bitmap || !pixels) return bitmap;
    const auto argb = [](COLORREF c, DWORD alpha) {
        return (alpha << 24) | (static_cast<DWORD>(GetRValue(c)) << 16) | (static_cast<DWORD>(GetGValue(c)) << 8) |
               GetBValue(c);
    };
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            const bool set = kIconRows[y * 16 / size][x * 16 / size] == '#';
            pixels[y * size + x] = set ? argb(color, 255) : transparent ? 0 : argb(background, 255);
        }
    }
    return bitmap;
}

void addToolbarIcon() {
    g_toolbarIcons.hToolbarBmp = createGlyphBitmap(16, RGB(64, 64, 64), false, RGB(192, 192, 192));
    g_toolbarIcons.hToolbarIcon = createPluginIcon(32, false);
    g_toolbarIcons.hToolbarIconDarkMode = createPluginIcon(32, true);
    SendMessageW(g_npp.nppHandle, NPPM_ADDTOOLBARICON_FORDARKMODE, static_cast<WPARAM>(g_menu[MenuTogglePreview].cmdID),
                 reinterpret_cast<LPARAM>(&g_toolbarIcons));
}

}  // namespace

// =================================================================================================
// shared helpers (plugin.h)
// =================================================================================================

void Settings::load() {
    const std::wstring ini = iniPath();
    syncScroll = GetPrivateProfileIntW(L"Settings", L"SyncScroll", 1, ini.c_str()) != 0;
    smartEnter = GetPrivateProfileIntW(L"Settings", L"SmartEnter", 1, ini.c_str()) != 0;
    autoOpen = GetPrivateProfileIntW(L"Settings", L"AutoOpen", 0, ini.c_str()) != 0;
    highlightCode = GetPrivateProfileIntW(L"Settings", L"HighlightCode", 1, ini.c_str()) != 0;
    const UINT position = GetPrivateProfileIntW(L"Settings", L"Panel", 0, ini.c_str());
    panel = position == 1 ? PanelPosition::Top : position == 2 ? PanelPosition::Bottom : PanelPosition::Hidden;
}

void Settings::save() const {
    const std::wstring ini = iniPath();
    WritePrivateProfileStringW(L"Settings", L"SyncScroll", syncScroll ? L"1" : L"0", ini.c_str());
    WritePrivateProfileStringW(L"Settings", L"SmartEnter", smartEnter ? L"1" : L"0", ini.c_str());
    WritePrivateProfileStringW(L"Settings", L"AutoOpen", autoOpen ? L"1" : L"0", ini.c_str());
    WritePrivateProfileStringW(L"Settings", L"HighlightCode", highlightCode ? L"1" : L"0", ini.c_str());
    WritePrivateProfileStringW(L"Settings", L"Panel", std::to_wstring(static_cast<int>(panel)).c_str(), ini.c_str());
}

void setMenuCheck(MenuIndex index, bool checked) {
    SendMessageW(g_npp.nppHandle, NPPM_SETMENUITEMCHECK, static_cast<WPARAM>(g_menu[index].cmdID),
                 checked ? TRUE : FALSE);
    ToolPanel::instance().refresh();  // its toggle buttons mirror the menu
}

const wchar_t* menuItemName(MenuIndex index) {
    return g_menu[index].itemName;
}

int menuCommandId(MenuIndex index) {
    return g_menu[index].cmdID;
}

bool menuItemChecked(MenuIndex index) {
    switch (index) {
        case MenuTogglePreview:
            return panel().visible();
        case MenuSyncScroll:
            return g_settings.syncScroll;
        case MenuSmartEnter:
            return g_settings.smartEnter;
        case MenuAutoOpen:
            return g_settings.autoOpen;
        case MenuHighlightCode:
            return g_settings.highlightCode;
        case MenuPinPreview:
            return panel().pinned();
        case MenuPanelTop:
            return g_settings.panel == PanelPosition::Top;
        case MenuPanelBottom:
            return g_settings.panel == PanelPosition::Bottom;
        case MenuPanelHidden:
            return g_settings.panel == PanelPosition::Hidden;
        default:
            return false;
    }
}

void runMenuCommand(MenuIndex index) {
    if (index >= 0 && index < MenuCount && g_menu[index].pFunc) g_menu[index].pFunc();
}

void setPanelPosition(PanelPosition position) {
    if (g_nppReady && !ToolPanel::instance().apply(position)) {
        showMessage(L"The tool panel could not be embedded into the window of this Notepad++ version.", MB_ICONWARNING);
        position = PanelPosition::Hidden;
    }
    g_settings.panel = position;
    g_settings.save();
    updatePanelMenu();
}

std::wstring pluginDirectory() {
    wchar_t path[MAX_PATH] = {0};
    GetModuleFileNameW(g_module, path, MAX_PATH);
    return directoryOf(path);
}

std::wstring configDirectory() {
    wchar_t path[MAX_PATH] = {0};
    SendMessageW(g_npp.nppHandle, NPPM_GETPLUGINSCONFIGDIR, MAX_PATH, reinterpret_cast<LPARAM>(path));
    return path;
}

intptr_t currentBufferId() {
    return SendMessageW(g_npp.nppHandle, NPPM_GETCURRENTBUFFERID, 0, 0);
}

std::wstring currentFilePath() {
    return bufferFilePath(currentBufferId());
}

std::wstring bufferFilePath(intptr_t bufferId) {
    const LRESULT length =
        SendMessageW(g_npp.nppHandle, NPPM_GETFULLPATHFROMBUFFERID, static_cast<WPARAM>(bufferId), 0);
    if (length <= 0) return {};
    std::vector<wchar_t> buffer(static_cast<size_t>(length) + 1, L'\0');
    SendMessageW(g_npp.nppHandle, NPPM_GETFULLPATHFROMBUFFERID, static_cast<WPARAM>(bufferId),
                 reinterpret_cast<LPARAM>(buffer.data()));
    return buffer.data();
}

bool isMarkdownPath(const std::wstring& path) {
    static const wchar_t* const known[] = {L"md",   L"markdown", L"mdown", L"mkd", L"mkdn",
                                           L"mdwn", L"mdtxt",    L"rmd",   L"qmd"};
    const std::wstring ext = extensionOf(path);
    return std::any_of(std::begin(known), std::end(known), [&](const wchar_t* e) { return ext == e; });
}

bool isDarkMode() {
    return SendMessageW(g_npp.nppHandle, NPPM_ISDARKMODEENABLED, 0, 0) != 0;
}

HICON createPluginIcon(int size, bool forDarkBackground) {
    const HBITMAP color = createGlyphBitmap(size, forDarkBackground ? RGB(224, 224, 224) : RGB(64, 64, 64), true, 0);
    const HBITMAP mask = CreateBitmap(size, size, 1, 1, nullptr);
    ICONINFO info = {};
    info.fIcon = TRUE;
    info.hbmColor = color;
    info.hbmMask = mask;
    const HICON icon = CreateIconIndirect(&info);
    DeleteObject(color);
    DeleteObject(mask);
    return icon;
}

void showMessage(const std::wstring& text, UINT icon) {
    MessageBoxW(g_npp.nppHandle, text.c_str(), kPluginName, MB_OK | icon);
}

// =================================================================================================
// exported plugin interface
// =================================================================================================

extern "C" {

__declspec(dllexport) void setInfo(NppData data) {
    g_npp = data;
    g_settings.load();
    initMenu();
    createMessageWindow();
}

__declspec(dllexport) const wchar_t* getName() {
    return kPluginName;
}

__declspec(dllexport) FuncItem* getFuncsArray(int* count) {
    initMenu();
    *count = MenuCount;
    return g_menu;
}

__declspec(dllexport) void beNotified(SCNotification* notification) {
    if (!notification) return;
    const HWND from = static_cast<HWND>(notification->nmhdr.hwndFrom);

    switch (notification->nmhdr.code) {
        case NPPN_READY:
            g_nppReady = true;
            nppshared::PasteFilesHook::install(g_npp.nppHandle, &openPastedFiles);
            ToolPanel::instance().apply(g_settings.panel);
            applyAutoOpen();
            panel().onDocumentChanged(true);
            CodeHighlighter::instance().setEnabled(g_settings.highlightCode);
            CodeHighlighter::instance().init();
            break;

        case NPPN_TBMODIFICATION:
            addToolbarIcon();
            break;

        case NPPN_BUFFERACTIVATED:
            if (!g_nppReady) break;
            applyAutoOpen();
            panel().onDocumentChanged(true);
            CodeHighlighter::instance().onBufferActivated();
            break;

        case NPPN_FILESAVED:
        case NPPN_FILERENAMED:
            if (!g_nppReady) break;
            applyAutoOpen();  // "Save as" may have turned the document into a Markdown file
            panel().onDocumentChanged(false);
            CodeHighlighter::instance().onBufferActivated();
            break;

        case NPPN_FILECLOSED: {
            if (!g_nppReady) break;
            const auto bufferId = static_cast<intptr_t>(notification->nmhdr.idFrom);
            CodeHighlighter::instance().onBufferClosed(bufferId);
            if (panel().pinned()) {
                panel().onBufferClosed(bufferId);
                applyAutoOpen();
            }
            break;
        }

        case NPPN_DARKMODECHANGED:
            panel().onThemeChanged();
            ToolPanel::instance().onThemeChanged();
            CodeHighlighter::instance().onStylesChanged();  // Notepad++ switches to the dark / light theme
            break;

        case NPPN_WORDSTYLESUPDATED:
            if (g_nppReady) CodeHighlighter::instance().onStylesChanged();
            break;

        case NPPN_SHUTDOWN:
            nppshared::PasteFilesHook::uninstall();
            CodeHighlighter::instance().shutdown();
            panel().shutdown();
            ToolPanel::instance().shutdown();
            if (g_messageWindow) DestroyWindow(g_messageWindow);
            g_messageWindow = nullptr;
            break;

        case SCN_MODIFIED:
            if (notification->modificationType & (SC_MOD_INSERTTEXT | SC_MOD_DELETETEXT)) {
                if (panel().visible()) panel().onTextChanged(from);
                if (from == Editor::currentHandle()) CodeHighlighter::instance().onTextChanged();
            }
            break;

        case SCN_UPDATEUI:
            if (panel().visible()) panel().onEditorUpdateUI(from);
            break;

        case SCN_CHARADDED:
            if ((notification->ch == '\n' || notification->ch == '\r') && g_messageWindow && smartEnterApplies()) {
                const bool crOnly = Editor(from).call(SCI_GETEOLMODE) == SC_EOL_CR;
                // Deferred: Notepad++ applies its own auto-indentation after notifying the plugins.
                if ((notification->ch == '\r') == crOnly) PostMessageW(g_messageWindow, WM_APP_SMART_ENTER, 0, 0);
            }
            break;
    }
}

__declspec(dllexport) LRESULT messageProc(UINT, WPARAM, LPARAM) {
    return TRUE;
}

__declspec(dllexport) BOOL isUnicode() {
    return TRUE;
}

}  // extern "C"

BOOL APIENTRY DllMain(HINSTANCE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        g_module = module;
        DisableThreadLibraryCalls(module);
    }
    return TRUE;
}
