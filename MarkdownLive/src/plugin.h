// Shared plugin state and helpers.
#pragma once

#include <windows.h>

#include <string>

#include "npp_api.h"

constexpr wchar_t kPluginName[] = L"Markdown Live";
constexpr wchar_t kPluginVersion[] = L"1.0.0";

extern NppData g_npp;
extern HINSTANCE g_module;

// Where the tool panel (buttons for the commands below) is embedded in the Notepad++ window.
enum class PanelPosition { Hidden = 0, Top = 1, Bottom = 2 };

struct Settings {
    bool syncScroll = true;
    bool smartEnter = true;
    bool autoOpen = false;
    bool autoClose = false;      // hide the preview for files that are not Markdown
    bool highlightCode = true;   // color fenced code blocks in the editor
    PanelPosition panel = PanelPosition::Hidden;

    void load();
    void save() const;
};
extern Settings g_settings;

// Index of each entry in the plugin menu (see plugin.cpp). Notepad++ stores custom shortcuts by
// this index, so new entries go to the end of their group to keep most indices stable.
enum MenuIndex {
    MenuTogglePreview = 0,
    MenuSep1,
    MenuBold,
    MenuItalic,
    MenuStrikethrough,
    MenuInlineCode,
    MenuCodeBlock,
    MenuSep2,
    MenuHeadingMore,
    MenuHeadingLess,
    MenuBulletList,
    MenuNumberedList,
    MenuTask,
    MenuQuote,
    MenuSep3,
    MenuLink,
    MenuImage,
    MenuPasteImage,
    MenuInsertTable,
    MenuFormatTable,
    MenuTableRowAbove,
    MenuTableRowBelow,
    MenuTableDeleteRow,
    MenuTableColumnLeft,
    MenuTableColumnRight,
    MenuTableDeleteColumn,
    MenuToc,
    MenuSep4,
    MenuExportHtml,
    MenuExportPdf,
    MenuPinPreview,
    MenuSep5,
    MenuSyncScroll,
    MenuSmartEnter,
    MenuAutoOpen,
    MenuAutoClose,
    MenuHighlightCode,
    MenuSep6,
    MenuPanelTop,
    MenuPanelBottom,
    MenuPanelHidden,
    MenuSep7,
    MenuAbout,
    MenuCount
};

void setMenuCheck(MenuIndex index, bool checked);

// Access to the menu entries for the tool panel.
const wchar_t* menuItemName(MenuIndex index);
int menuCommandId(MenuIndex index);
bool menuItemChecked(MenuIndex index);  // current state of the checkable entries
void runMenuCommand(MenuIndex index);
void setPanelPosition(PanelPosition position);

std::wstring pluginDirectory();   // folder that contains the plugin DLL
std::wstring configDirectory();   // Notepad++ plugin config folder
intptr_t currentBufferId();
std::wstring currentFilePath();   // full path or e.g. "new 1" for unsaved documents
std::wstring bufferFilePath(intptr_t bufferId);
bool isMarkdownPath(const std::wstring& path);
bool isDarkMode();
// Small monochrome "Markdown" icon drawn at runtime (caller owns the handle).
HICON createPluginIcon(int size, bool forDarkBackground);
void showMessage(const std::wstring& text, UINT icon = MB_ICONINFORMATION);
