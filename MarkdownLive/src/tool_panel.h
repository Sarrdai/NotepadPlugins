// Slim tool panel with buttons for the editing commands and options of the plugin menu.
//
// Notepad++ offers no plugin API for such a bar (docked dialogs are far too tall), so the
// panel is embedded as an additional band into one of the two rebar controls of the main
// window: the top one (below the Notepad++ toolbar) or the bottom one (above the status
// bar, where the incremental search bar lives). Notepad++ derives its layout from the
// height of these controls, so the editor area adapts automatically.
#pragma once

#include <windows.h>

#include <string>
#include <vector>

#include "plugin.h"

class ToolPanel {
public:
    static ToolPanel& instance();

    // Shows the panel at `position` or removes it. Returns false if it could not be embedded.
    bool apply(PanelPosition position);
    void refresh();  // the state of a checkable command changed
    void onThemeChanged();
    void shutdown();

private:
    ToolPanel() = default;

    bool create(PanelPosition position);
    void destroy(bool relayout);
    void layout();
    void paint();
    int hitTest(POINT point) const;
    void setHot(int index);
    void showOverflowMenu();
    void showContextMenu(POINT screen);
    std::wstring tooltipText(int index) const;

    static LRESULT CALLBACK windowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);

    HWND hwnd_ = nullptr;
    HWND rebar_ = nullptr;
    HWND tooltip_ = nullptr;
    PanelPosition position_ = PanelPosition::Hidden;
    ULONG_PTR gdiplusToken_ = 0;
    int dpi_ = 96;
    int hot_ = -1;
    int pressed_ = -1;
    bool trackingMouse_ = false;
    std::vector<RECT> rects_;  // button rectangles, empty for items that do not fit
    RECT chevron_ = {};        // "more" button, empty if everything fits
    std::wstring tooltipBuffer_;
};
