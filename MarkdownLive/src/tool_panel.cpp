#include "tool_panel.h"

#include <commctrl.h>
#include <uxtheme.h>
#include <windowsx.h>

#include <cmath>
#include <algorithm>
#include <initializer_list>
#include <memory>

// GDI+ headers rely on the min/max macros that NOMINMAX removes.
namespace Gdiplus {
using std::max;
using std::min;
}  // namespace Gdiplus
#include <gdiplus.h>

#include "editor.h"
#include "npp_api.h"

namespace {

constexpr wchar_t kWindowClass[] = L"MarkdownLiveToolPanel";
constexpr UINT kBandId = 0x4D4C;  // "ML"; Notepad++ itself numbers its bands from 0
constexpr UINT WM_APP_RUN = WM_APP + 1;
constexpr UINT WM_APP_REAPPLY = WM_APP + 2;
constexpr UINT kMenuTop = 1, kMenuBottom = 2, kMenuHide = 3;

// Sizes in pixels at 96 dpi.
constexpr int kBarHeight = 28;
constexpr int kButtonSize = 24;
constexpr int kGlyphSize = 16;
constexpr int kSeparatorWidth = 9;
constexpr int kChevronWidth = 16;
constexpr int kPadding = 3;

constexpr int kSeparator = -1;

struct Item {
    int command;           // MenuIndex or kSeparator
    const wchar_t* label;  // text button instead of an icon
};

const Item kItems[] = {
    {MenuTogglePreview, nullptr},
    {MenuPinPreview, nullptr},
    {kSeparator, nullptr},
    {MenuBold, nullptr},
    {MenuItalic, nullptr},
    {MenuStrikethrough, nullptr},
    {MenuInlineCode, nullptr},
    {MenuCodeBlock, nullptr},
    {kSeparator, nullptr},
    {MenuHeadingMore, nullptr},
    {MenuHeadingLess, nullptr},
    {MenuBulletList, nullptr},
    {MenuNumberedList, nullptr},
    {MenuTask, nullptr},
    {MenuQuote, nullptr},
    {kSeparator, nullptr},
    {MenuLink, nullptr},
    {MenuImage, nullptr},
    {MenuPasteImage, nullptr},
    {MenuInsertTable, nullptr},
    {MenuFormatTable, nullptr},
    {kSeparator, nullptr},
    {MenuTableRowAbove, nullptr},
    {MenuTableRowBelow, nullptr},
    {MenuTableDeleteRow, nullptr},
    {MenuTableColumnLeft, nullptr},
    {MenuTableColumnRight, nullptr},
    {MenuTableDeleteColumn, nullptr},
    {kSeparator, nullptr},
    {MenuToc, nullptr},
    {kSeparator, nullptr},
    {MenuExportHtml, L"HTML"},
    {MenuExportPdf, L"PDF"},
    {kSeparator, nullptr},
    {MenuSyncScroll, nullptr},
    {MenuSmartEnter, nullptr},
    {MenuAutoOpen, nullptr},
    {MenuHighlightCode, nullptr},
};
constexpr int kItemCount = static_cast<int>(sizeof(kItems) / sizeof(kItems[0]));
constexpr int kChevronIndex = kItemCount;  // hit test result for the "more" button

int dpiOf(HWND hwnd) {
    // GetDpiForWindow exists since Windows 10 1607
    using Fn = UINT(WINAPI*)(HWND);
    static const auto fn = reinterpret_cast<Fn>(
        reinterpret_cast<void*>(GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow")));
    int dpi = fn ? static_cast<int>(fn(hwnd)) : 0;
    if (dpi <= 0) {
        const HDC dc = GetDC(hwnd);
        dpi = GetDeviceCaps(dc, LOGPIXELSX);
        ReleaseDC(hwnd, dc);
    }
    return dpi > 0 ? dpi : 96;
}

HWND findRebarWithout(HWND excluded) {
    for (HWND child = FindWindowExW(g_npp.nppHandle, nullptr, REBARCLASSNAMEW, nullptr); child;
         child = FindWindowExW(g_npp.nppHandle, child, REBARCLASSNAMEW, nullptr)) {
        if (child != excluded) return child;
    }
    return nullptr;
}

// Notepad++ has two rebars: the one that hosts its toolbar is the top one, the other one sits
// above the status bar.
HWND findRebar(bool top) {
    HWND upper = nullptr, lower = nullptr;
    LONG upperY = 0, lowerY = 0;
    for (HWND child = FindWindowExW(g_npp.nppHandle, nullptr, REBARCLASSNAMEW, nullptr); child;
         child = FindWindowExW(g_npp.nppHandle, child, REBARCLASSNAMEW, nullptr)) {
        if (FindWindowExW(child, nullptr, TOOLBARCLASSNAMEW, nullptr)) return top ? child : findRebarWithout(child);
        RECT rect;
        GetWindowRect(child, &rect);
        if (!upper || rect.top < upperY) upper = child, upperY = rect.top;
        if (!lower || rect.top >= lowerY) lower = child, lowerY = rect.top;
    }
    // no toolbar found: decide by position
    return upper == lower ? nullptr : top ? upper : lower;
}

struct Palette {
    COLORREF background, text, separator, hot, hotEdge, checked, checkedEdge;
};

Palette currentPalette() {
    if (isDarkMode()) {
        NppDarkModeColors c;
        if (!SendMessageW(g_npp.nppHandle, NPPM_GETDARKMODECOLORS, sizeof(c), reinterpret_cast<LPARAM>(&c))) {
            c.pureBackground = RGB(0x20, 0x20, 0x20);
            c.softerBackground = RGB(0x38, 0x38, 0x38);
            c.hotBackground = RGB(0x45, 0x45, 0x45);
            c.text = RGB(0xE0, 0xE0, 0xE0);
            c.edge = RGB(0x64, 0x64, 0x64);
            c.hotEdge = RGB(0x9B, 0x9B, 0x9B);
        }
        return {c.pureBackground, c.text, c.edge, c.hotBackground, c.hotEdge, c.softerBackground, c.edge};
    }
    return {GetSysColor(COLOR_BTNFACE), GetSysColor(COLOR_BTNTEXT), RGB(0xC4, 0xC4, 0xC4), RGB(0xE5, 0xF1, 0xFB),
            RGB(0x00, 0x78, 0xD7),      RGB(0xCC, 0xE4, 0xF7),      RGB(0x66, 0xA7, 0xE8)};
}

Gdiplus::Color toColor(COLORREF c, BYTE alpha = 255) {
    return Gdiplus::Color(alpha, GetRValue(c), GetGValue(c), GetBValue(c));
}

// Falls back to the generic sans serif family (and finally to its regular style) if the
// requested font is not installed.
std::unique_ptr<Gdiplus::Font> makeFont(const wchar_t* family, float pixels, int style) {
    const Gdiplus::FontFamily preferred(family);
    if (preferred.IsAvailable()) {
        auto font = std::make_unique<Gdiplus::Font>(&preferred, pixels, style, Gdiplus::UnitPixel);
        if (font->GetLastStatus() == Gdiplus::Ok) return font;
    }
    const Gdiplus::FontFamily* generic = Gdiplus::FontFamily::GenericSansSerif();
    auto font = std::make_unique<Gdiplus::Font>(generic, pixels, style, Gdiplus::UnitPixel);
    if (font->GetLastStatus() == Gdiplus::Ok) return font;
    return std::make_unique<Gdiplus::Font>(generic, pixels, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
}

void drawCentered(Gdiplus::Graphics& g, const wchar_t* text, const Gdiplus::Font& font, const Gdiplus::RectF& box,
                  const Gdiplus::Brush& brush) {
    const Gdiplus::StringFormat* format = Gdiplus::StringFormat::GenericTypographic();
    Gdiplus::RectF bounds;
    g.MeasureString(text, -1, &font, Gdiplus::PointF(0, 0), format, &bounds);
    const Gdiplus::PointF origin(box.X + (box.Width - bounds.Width) / 2, box.Y + (box.Height - bounds.Height) / 2);
    g.DrawString(text, -1, &font, origin, format, &brush);
}

float labelWidth(Gdiplus::Graphics& g, const wchar_t* text, const Gdiplus::Font& font) {
    Gdiplus::RectF bounds;
    g.MeasureString(text, -1, &font, Gdiplus::PointF(0, 0), Gdiplus::StringFormat::GenericTypographic(), &bounds);
    return bounds.Width;
}

// Draws the icon of a command into `box`. All coordinates are given on a 16 x 16 grid.
void drawGlyph(Gdiplus::Graphics& g, int command, const Gdiplus::RectF& box, COLORREF colorRef) {
    using Gdiplus::PointF;
    using Gdiplus::RectF;
    const float u = box.Width / 16.0f;
    const Gdiplus::Color color = toColor(colorRef);
    Gdiplus::Pen pen(color, std::max(1.0f, 1.2f * u));
    pen.SetStartCap(Gdiplus::LineCapRound);
    pen.SetEndCap(Gdiplus::LineCapRound);
    pen.SetLineJoin(Gdiplus::LineJoinRound);
    const Gdiplus::SolidBrush brush(color);
    const Gdiplus::SolidBrush soft(toColor(colorRef, 70));

    const auto at = [&](float x, float y) { return PointF(box.X + x * u, box.Y + y * u); };
    const auto line = [&](float x1, float y1, float x2, float y2) { g.DrawLine(&pen, at(x1, y1), at(x2, y2)); };
    const auto path = [&](std::initializer_list<PointF> grid) {
        std::vector<PointF> points;
        for (const PointF& p : grid) points.push_back(at(p.X, p.Y));
        g.DrawLines(&pen, points.data(), static_cast<INT>(points.size()));
    };
    const auto frame = [&](float x, float y, float w, float h) {
        g.DrawRectangle(&pen, box.X + x * u, box.Y + y * u, w * u, h * u);
    };
    const auto fill = [&](const Gdiplus::Brush& b, float x, float y, float w, float h) {
        g.FillRectangle(&b, box.X + x * u, box.Y + y * u, w * u, h * u);
    };
    const auto dot = [&](float x, float y, float r) {
        g.FillEllipse(&brush, box.X + (x - r) * u, box.Y + (y - r) * u, 2 * r * u, 2 * r * u);
    };
    const auto text = [&](const wchar_t* s, const wchar_t* family, float size, int style, float x, float y, float w,
                          float h) {
        drawCentered(g, s, *makeFont(family, size * u, style), RectF(box.X + x * u, box.Y + y * u, w * u, h * u),
                     brush);
    };
    // window with a divider: editor on the left, preview on the right
    const auto splitWindow = [&] {
        frame(1.5f, 2.5f, 13, 11);
        line(8, 2.5f, 8, 13.5f);
        line(3.5f, 5.5f, 6, 5.5f);
        line(3.5f, 8, 6, 8);
        line(3.5f, 10.5f, 5, 10.5f);
    };

    switch (command) {
        case MenuTogglePreview:
            splitWindow();
            fill(soft, 8, 2.5f, 6.5f, 11);
            break;
        case MenuBold:
            text(L"B", L"Segoe UI", 14, Gdiplus::FontStyleBold, 0, 0, 16, 16);
            break;
        case MenuItalic:
            text(L"I", L"Times New Roman", 15, Gdiplus::FontStyleBoldItalic, 0, 0, 16, 16);
            break;
        case MenuStrikethrough:
            text(L"S", L"Segoe UI", 14, Gdiplus::FontStyleRegular, 0, 0, 16, 16);
            line(2, 8.5f, 14, 8.5f);
            break;
        case MenuInlineCode:
            path({{5.5f, 4.5f}, {2, 8}, {5.5f, 11.5f}});
            path({{10.5f, 4.5f}, {14, 8}, {10.5f, 11.5f}});
            line(9.2f, 3.5f, 6.8f, 12.5f);
            break;
        case MenuCodeBlock:
            frame(1.5f, 2.5f, 13, 11);
            path({{6.5f, 5.5f}, {4, 8}, {6.5f, 10.5f}});
            path({{9.5f, 5.5f}, {12, 8}, {9.5f, 10.5f}});
            break;
        case MenuHeadingMore:
        case MenuHeadingLess:
            text(L"H", L"Segoe UI", 13, Gdiplus::FontStyleBold, 0, 1, 11, 15);
            line(10.5f, 4.5f, 15, 4.5f);
            if (command == MenuHeadingMore) line(12.75f, 2.25f, 12.75f, 6.75f);
            break;
        case MenuBulletList:
            for (const float y : {3.5f, 8.0f, 12.5f}) {
                dot(2.75f, y, 1.25f);
                line(6.5f, y, 14, y);
            }
            break;
        case MenuNumberedList:
            text(L"1", L"Segoe UI", 8, Gdiplus::FontStyleBold, 0, 0, 5.5f, 8);
            text(L"2", L"Segoe UI", 8, Gdiplus::FontStyleBold, 0, 8, 5.5f, 8);
            line(7.5f, 4.5f, 14, 4.5f);
            line(7.5f, 12, 14, 12);
            break;
        case MenuTask:
            frame(2.5f, 2.5f, 11, 11);
            path({{5, 8.2f}, {7.2f, 10.5f}, {11.2f, 5.5f}});
            break;
        case MenuQuote:
            fill(brush, 2, 2.5f, 2, 11);
            line(7, 4.5f, 14, 4.5f);
            line(7, 8, 14, 8);
            line(7, 11.5f, 11.5f, 11.5f);
            break;
        case MenuLink: {
            // two chain links
            const Gdiplus::GraphicsState state = g.Save();
            g.TranslateTransform(box.X + 8 * u, box.Y + 8 * u);
            g.RotateTransform(-45);
            for (const float x : {-6.5f, 0.5f}) {
                Gdiplus::GraphicsPath link;
                link.AddArc(x * u, -2.5f * u, 5 * u, 5 * u, 90, 180);
                link.AddArc((x + 1) * u, -2.5f * u, 5 * u, 5 * u, 270, 180);
                link.CloseFigure();
                g.DrawPath(&pen, &link);
            }
            g.DrawLine(&pen, -2.5f * u, 0.0f, 2.5f * u, 0.0f);
            g.Restore(state);
            break;
        }
        case MenuImage:
            frame(1.5f, 2.5f, 13, 11);
            path({{3.5f, 11.5f}, {6.5f, 8}, {8.5f, 10}, {10.5f, 7.5f}, {12.5f, 11.5f}});
            dot(5.5f, 5.5f, 1.1f);
            break;
        case MenuPasteImage:
            // clipboard with a picture
            frame(2.5f, 3.5f, 11, 11);
            fill(brush, 5.5f, 1.5f, 5, 3.5f);
            path({{4.5f, 12.5f}, {7, 9.5f}, {8.5f, 11}, {10, 9.5f}, {11.5f, 12.5f}});
            break;
        case MenuInsertTable:
            frame(1.5f, 2.5f, 13, 11);
            fill(soft, 1.5f, 2.5f, 13, 3.5f);
            line(1.5f, 6, 14.5f, 6);
            line(1.5f, 9.75f, 14.5f, 9.75f);
            line(6, 2.5f, 6, 13.5f);
            line(10.25f, 2.5f, 10.25f, 13.5f);
            break;
        case MenuFormatTable:
            // table with a "column width" arrow below
            frame(1.5f, 1.5f, 13, 7.5f);
            line(1.5f, 5.25f, 14.5f, 5.25f);
            line(8, 1.5f, 8, 9);
            line(2, 12.5f, 14, 12.5f);
            path({{4.25f, 10.25f}, {2, 12.5f}, {4.25f, 14.75f}});
            path({{11.75f, 10.25f}, {14, 12.5f}, {11.75f, 14.75f}});
            break;
        case MenuTableRowAbove:
        case MenuTableRowBelow:
        case MenuTableDeleteRow:
        case MenuTableColumnLeft:
        case MenuTableColumnRight:
        case MenuTableDeleteColumn: {
            // 3 x 3 table, the affected row / column is shaded, a "+" / "-" badge sits at the bottom right
            const bool rowCommand =
                command == MenuTableRowAbove || command == MenuTableRowBelow || command == MenuTableDeleteRow;
            const bool remove = command == MenuTableDeleteRow || command == MenuTableDeleteColumn;
            const bool first = command == MenuTableRowAbove || command == MenuTableColumnLeft;
            const bool last = command == MenuTableRowBelow || command == MenuTableColumnRight;
            const float band = first ? 1.5f : last ? 7.5f : 4.5f;
            if (rowCommand)
                fill(soft, 1.5f, band, 9, 3);
            else
                fill(soft, band, 1.5f, 3, 9);
            frame(1.5f, 1.5f, 9, 9);
            for (const float d : {4.5f, 7.5f}) {
                line(1.5f, d, 10.5f, d);
                line(d, 1.5f, d, 10.5f);
            }
            line(10.5f, 12.75f, 15, 12.75f);
            if (!remove) line(12.75f, 10.5f, 12.75f, 15);
            break;
        }
        case MenuToc:
            // outline: headings with indented sub entries
            line(2, 3, 11, 3);
            line(5.5f, 6.5f, 14, 6.5f);
            line(5.5f, 10, 14, 10);
            line(2, 13.5f, 11, 13.5f);
            dot(2.75f, 6.5f, 0.9f);
            dot(2.75f, 10, 0.9f);
            break;
        case MenuSyncScroll:
            for (const float x : {4.5f, 11.5f}) {
                line(x, 2.5f, x, 13.5f);
                path({{x - 2.5f, 5}, {x, 2.5f}, {x + 2.5f, 5}});
                path({{x - 2.5f, 11}, {x, 13.5f}, {x + 2.5f, 11}});
            }
            break;
        case MenuSmartEnter:
            // list bullet followed by a "return" arrow
            dot(2.75f, 4, 1.25f);
            line(6, 4, 9.5f, 4);
            path({{13.5f, 3}, {13.5f, 10.5f}, {3.5f, 10.5f}});
            path({{6.5f, 7.5f}, {3.5f, 10.5f}, {6.5f, 13.5f}});
            break;
        case MenuAutoOpen: {
            // preview window with a "play" triangle
            splitWindow();
            const PointF triangle[] = {at(9.75f, 5.5f), at(13, 8), at(9.75f, 10.5f)};
            g.FillPolygon(&brush, triangle, 3);
            break;
        }
        case MenuPinPreview: {
            // push pin, tilted
            const Gdiplus::GraphicsState state = g.Save();
            g.TranslateTransform(box.X + 8 * u, box.Y + 8 * u);
            g.RotateTransform(35);
            g.TranslateTransform(-8 * u, -8 * u);
            const auto local = [&](float x, float y) { return PointF(x * u, y * u); };
            const PointF head[] = {local(5.5f, 1.5f), local(10.5f, 1.5f), local(9.5f, 2.75f), local(9.5f, 6.5f),
                                   local(11.5f, 9),   local(4.5f, 9),     local(6.5f, 6.5f),  local(6.5f, 2.75f)};
            g.FillPolygon(&soft, head, 8);
            g.DrawPolygon(&pen, head, 8);
            g.DrawLine(&pen, local(8, 9), local(8, 14.5f));
            g.Restore(state);
            break;
        }
        case MenuHighlightCode:
            // code lines in different "colors" (solid / shaded)
            frame(1.5f, 2.5f, 13, 11);
            fill(soft, 7.25f, 4.25f, 5.5f, 2.5f);
            line(3.5f, 5.5f, 5.5f, 5.5f);
            line(8, 5.5f, 12, 5.5f);
            line(5, 8, 10.5f, 8);
            fill(soft, 3, 9.25f, 5, 2.5f);
            line(3.5f, 10.5f, 7.5f, 10.5f);
            line(9.5f, 10.5f, 11, 10.5f);
            break;
    }
}

std::wstring shortcutText(int commandId) {
    ShortcutKey key;
    if (!SendMessageW(g_npp.nppHandle, NPPM_GETSHORTCUTBYCMDID, static_cast<WPARAM>(commandId),
                      reinterpret_cast<LPARAM>(&key)) ||
        key.key == 0) {
        return {};
    }
    std::wstring text;
    if (key.isCtrl) text += L"Ctrl+";
    if (key.isAlt) text += L"Alt+";
    if (key.isShift) text += L"Shift+";
    if ((key.key >= '0' && key.key <= '9') || (key.key >= 'A' && key.key <= 'Z')) {
        text += static_cast<wchar_t>(key.key);
    } else if (key.key >= VK_F1 && key.key <= VK_F24) {
        text += L"F" + std::to_wstring(key.key - VK_F1 + 1);
    } else {
        wchar_t name[32] = {0};
        if (!GetKeyNameTextW(static_cast<LONG>(MapVirtualKeyW(key.key, MAPVK_VK_TO_VSC) << 16), name, 32)) return {};
        text += name;
    }
    return text;
}

}  // namespace

ToolPanel& ToolPanel::instance() {
    static ToolPanel panel;
    return panel;
}

// =================================================================================================
// embedding
// =================================================================================================

bool ToolPanel::apply(PanelPosition position) {
    if (position == position_ && (hwnd_ != nullptr) == (position != PanelPosition::Hidden)) return true;
    destroy(position == PanelPosition::Hidden);
    return position == PanelPosition::Hidden || create(position);
}

bool ToolPanel::create(PanelPosition position) {
    const HWND rebar = findRebar(position == PanelPosition::Top);
    if (!rebar) return false;

    static bool classRegistered = false;
    if (!classRegistered) {
        WNDCLASSEXW wc = {};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = windowProc;
        wc.hInstance = g_module;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.lpszClassName = kWindowClass;
        classRegistered = RegisterClassExW(&wc) != 0;
    }
    if (gdiplusToken_ == 0) {
        Gdiplus::GdiplusStartupInput input;
        if (Gdiplus::GdiplusStartup(&gdiplusToken_, &input, nullptr) != Gdiplus::Ok) gdiplusToken_ = 0;
    }
    if (!classRegistered || gdiplusToken_ == 0) return false;

    dpi_ = dpiOf(g_npp.nppHandle);
    const int height = MulDiv(kBarHeight, dpi_, 96);
    hwnd_ = CreateWindowExW(0, kWindowClass, L"", WS_CHILD | WS_CLIPSIBLINGS, 0, 0, 0, height, rebar, nullptr, g_module,
                            nullptr);
    if (!hwnd_) return false;

    tooltip_ =
        CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr, WS_POPUP | TTS_NOPREFIX | TTS_ALWAYSTIP, CW_USEDEFAULT,
                        CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, hwnd_, nullptr, g_module, nullptr);

    REBARBANDINFOW band = {};
    band.cbSize = REBARBANDINFOW_V6_SIZE;
    band.fMask = RBBIM_STYLE | RBBIM_CHILD | RBBIM_CHILDSIZE | RBBIM_SIZE | RBBIM_ID;
    band.fStyle = RBBS_BREAK | RBBS_NOGRIPPER;  // a row of its own
    band.hwndChild = hwnd_;
    band.cxMinChild = 0;
    band.cyMinChild = static_cast<UINT>(height);
    band.cyChild = static_cast<UINT>(height);
    band.cyMaxChild = static_cast<UINT>(height);
    band.cx = 0;
    band.wID = kBandId;
    if (!SendMessageW(rebar, RB_INSERTBANDW, static_cast<WPARAM>(-1), reinterpret_cast<LPARAM>(&band))) {
        destroy(false);
        return false;
    }
    rebar_ = rebar;
    position_ = position;
    onThemeChanged();
    ShowWindow(hwnd_, SW_SHOWNA);
    SendMessageW(g_npp.nppHandle, WM_SIZE, 0, 0);  // Notepad++ lays out its window around the rebars
    return true;
}

void ToolPanel::destroy(bool relayout) {
    position_ = PanelPosition::Hidden;
    if (!hwnd_) return;
    if (rebar_ && IsWindow(rebar_)) {
        const LRESULT index = SendMessageW(rebar_, RB_IDTOINDEX, kBandId, 0);
        if (index >= 0) SendMessageW(rebar_, RB_DELETEBAND, static_cast<WPARAM>(index), 0);
    }
    const HWND window = hwnd_;
    hwnd_ = nullptr;
    rebar_ = nullptr;
    tooltip_ = nullptr;  // owned by the panel window
    hot_ = pressed_ = -1;
    trackingMouse_ = false;
    DestroyWindow(window);
    if (relayout) SendMessageW(g_npp.nppHandle, WM_SIZE, 0, 0);
}

void ToolPanel::shutdown() {
    destroy(false);
    if (gdiplusToken_ != 0) Gdiplus::GdiplusShutdown(gdiplusToken_);
    gdiplusToken_ = 0;
}

void ToolPanel::refresh() {
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
}

void ToolPanel::onThemeChanged() {
    if (tooltip_) SetWindowTheme(tooltip_, isDarkMode() ? L"DarkMode_Explorer" : nullptr, nullptr);
    refresh();
}

// =================================================================================================
// layout and painting
// =================================================================================================

void ToolPanel::layout() {
    if (!hwnd_) return;
    RECT client;
    GetClientRect(hwnd_, &client);
    const auto scale = [this](int value) { return MulDiv(value, dpi_, 96); };
    const int button = scale(kButtonSize);
    const int top = (client.bottom - button) / 2;

    // natural widths
    std::vector<int> widths(kItemCount, button);
    {
        Gdiplus::Graphics g(hwnd_);
        const auto font = makeFont(L"Segoe UI", static_cast<float>(scale(12)), Gdiplus::FontStyleRegular);
        for (int i = 0; i < kItemCount; ++i) {
            if (kItems[i].command == kSeparator)
                widths[i] = scale(kSeparatorWidth);
            else if (kItems[i].label)
                widths[i] = static_cast<int>(labelWidth(g, kItems[i].label, *font) + 0.5f) + scale(12);
        }
    }
    int total = scale(kPadding) * 2;
    for (const int w : widths) total += w;

    // items that do not fit are reachable through the "more" button at the right edge
    const bool overflow = total > client.right;
    const int limit = overflow ? client.right - scale(kChevronWidth) - scale(kPadding) : client.right;
    rects_.assign(kItemCount, RECT{});
    int x = scale(kPadding);
    int shown = 0;
    for (; shown < kItemCount && x + widths[shown] <= limit; ++shown) {
        rects_[shown] = {x, top, x + widths[shown], top + button};
        x += widths[shown];
    }
    while (overflow && shown > 0 && kItems[shown - 1].command == kSeparator)
        rects_[--shown] = {};  // no dangling separator
    chevron_ = overflow ? RECT{client.right - scale(kChevronWidth) - scale(kPadding), top,
                               client.right - scale(kPadding), top + button}
                        : RECT{};

    if (tooltip_) {
        TOOLINFOW tool = {};
        tool.cbSize = TTTOOLINFOW_V1_SIZE;
        tool.hwnd = hwnd_;
        for (int i = 0; i <= kItemCount; ++i) {
            tool.uId = static_cast<UINT_PTR>(i);
            SendMessageW(tooltip_, TTM_DELTOOLW, 0, reinterpret_cast<LPARAM>(&tool));
            const RECT& rect = i == kChevronIndex ? chevron_ : rects_[i];
            if (IsRectEmpty(&rect) || (i < kItemCount && kItems[i].command == kSeparator)) continue;
            tool.uFlags = TTF_SUBCLASS;
            tool.rect = rect;
            tool.lpszText = LPSTR_TEXTCALLBACKW;
            SendMessageW(tooltip_, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&tool));
        }
    }
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void ToolPanel::paint() {
    PAINTSTRUCT ps;
    const HDC dc = BeginPaint(hwnd_, &ps);
    RECT client;
    GetClientRect(hwnd_, &client);
    if (client.right > 0 && client.bottom > 0) {
        const HDC memory = CreateCompatibleDC(dc);
        const HBITMAP bitmap = CreateCompatibleBitmap(dc, client.right, client.bottom);
        const HGDIOBJ oldBitmap = SelectObject(memory, bitmap);
        {
            const Palette colors = currentPalette();
            const float u = static_cast<float>(dpi_) / 96.0f;
            Gdiplus::Graphics g(memory);
            g.Clear(toColor(colors.background));
            g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
            g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
            g.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAliasGridFit);

            const auto labelFont = makeFont(L"Segoe UI", 12 * u, Gdiplus::FontStyleRegular);
            const Gdiplus::SolidBrush textBrush(toColor(colors.text));
            const Gdiplus::Pen separatorPen(toColor(colors.separator), std::max(1.0f, std::floor(u)));
            const auto drawState = [&](const RECT& r, int index, bool checked) {
                const bool hot = index == hot_ && (pressed_ < 0 || pressed_ == index);
                const bool down = index == pressed_ && index == hot_;
                if (!hot && !checked) return;
                const Gdiplus::RectF box(static_cast<float>(r.left) + 0.5f, static_cast<float>(r.top) + 0.5f,
                                         static_cast<float>(r.right - r.left) - 1,
                                         static_cast<float>(r.bottom - r.top) - 1);
                const Gdiplus::SolidBrush fill(toColor(down || (checked && !hot) ? colors.checked : colors.hot));
                const Gdiplus::Pen edge(toColor(hot ? colors.hotEdge : colors.checkedEdge), 1.0f);
                g.FillRectangle(&fill, box);
                g.DrawRectangle(&edge, box);
            };

            for (int i = 0; i < kItemCount; ++i) {
                const RECT& r = rects_.size() == static_cast<size_t>(kItemCount) ? rects_[i] : RECT{};
                if (IsRectEmpty(&r)) continue;
                const Item& item = kItems[i];
                if (item.command == kSeparator) {
                    const float x = std::floor((static_cast<float>(r.left) + static_cast<float>(r.right)) / 2) + 0.5f;
                    g.DrawLine(&separatorPen, x, static_cast<float>(r.top) + 3 * u, x,
                               static_cast<float>(r.bottom) - 3 * u);
                    continue;
                }
                drawState(r, i, menuItemChecked(static_cast<MenuIndex>(item.command)));
                if (item.label) {
                    const Gdiplus::RectF box(static_cast<float>(r.left), static_cast<float>(r.top),
                                             static_cast<float>(r.right - r.left),
                                             static_cast<float>(r.bottom - r.top));
                    drawCentered(g, item.label, *labelFont, box, textBrush);
                } else {
                    const float size = static_cast<float>(MulDiv(kGlyphSize, dpi_, 96));
                    const Gdiplus::RectF box(std::floor(static_cast<float>(r.left + r.right) / 2 - size / 2),
                                             std::floor(static_cast<float>(r.top + r.bottom) / 2 - size / 2), size,
                                             size);
                    drawGlyph(g, item.command, box, colors.text);
                }
            }
            if (!IsRectEmpty(&chevron_)) {
                drawState(chevron_, kChevronIndex, false);
                Gdiplus::Pen pen(toColor(colors.text), std::max(1.0f, 1.2f * u));
                const float cx = static_cast<float>(chevron_.left + chevron_.right) / 2;
                const float cy = static_cast<float>(chevron_.top + chevron_.bottom) / 2;
                for (const float dx : {-3.5f, 0.5f}) {
                    const Gdiplus::PointF points[] = {
                        {cx + dx * u, cy - 3 * u}, {cx + (dx + 3) * u, cy}, {cx + dx * u, cy + 3 * u}};
                    g.DrawLines(&pen, points, 3);
                }
            }
        }
        BitBlt(dc, 0, 0, client.right, client.bottom, memory, 0, 0, SRCCOPY);
        SelectObject(memory, oldBitmap);
        DeleteObject(bitmap);
        DeleteDC(memory);
    }
    EndPaint(hwnd_, &ps);
}

// =================================================================================================
// interaction
// =================================================================================================

int ToolPanel::hitTest(POINT point) const {
    if (PtInRect(&chevron_, point)) return kChevronIndex;
    for (size_t i = 0; i < rects_.size(); ++i) {
        if (kItems[i].command != kSeparator && PtInRect(&rects_[i], point)) return static_cast<int>(i);
    }
    return -1;
}

void ToolPanel::setHot(int index) {
    if (index == hot_) return;
    hot_ = index;
    refresh();
}

std::wstring ToolPanel::tooltipText(int index) const {
    if (index == kChevronIndex) return L"More commands";
    if (index < 0 || index >= kItemCount || kItems[index].command == kSeparator) return {};
    const auto command = static_cast<MenuIndex>(kItems[index].command);
    std::wstring text = menuItemName(command);
    while (!text.empty() && text.back() == L'.') text.pop_back();  // "Insert Image..."
    const std::wstring shortcut = shortcutText(menuCommandId(command));
    if (!shortcut.empty()) text += L" (" + shortcut + L")";
    return text;
}

// Commands that do not fit into the window.
void ToolPanel::showOverflowMenu() {
    const HMENU menu = CreatePopupMenu();
    bool separatorPending = false;
    int count = 0;
    for (int i = 0; i < kItemCount; ++i) {
        if (!IsRectEmpty(&rects_[i])) continue;
        const int command = kItems[i].command;
        if (command == kSeparator) {
            separatorPending = count > 0;
            continue;
        }
        if (separatorPending) AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        separatorPending = false;
        const bool checked = menuItemChecked(static_cast<MenuIndex>(command));
        AppendMenuW(menu, MF_STRING | (checked ? MF_CHECKED : 0), static_cast<UINT_PTR>(command) + 1,
                    menuItemName(static_cast<MenuIndex>(command)));
        ++count;
    }
    POINT anchor = {chevron_.right, position_ == PanelPosition::Bottom ? chevron_.top : chevron_.bottom};
    ClientToScreen(hwnd_, &anchor);
    const UINT align = TPM_RIGHTALIGN | (position_ == PanelPosition::Bottom ? TPM_BOTTOMALIGN : TPM_TOPALIGN);
    const int chosen =
        TrackPopupMenu(menu, align | TPM_RETURNCMD | TPM_NONOTIFY, anchor.x, anchor.y, 0, hwnd_, nullptr);
    DestroyMenu(menu);
    if (chosen > 0 && hwnd_) PostMessageW(hwnd_, WM_APP_RUN, static_cast<WPARAM>(chosen - 1), 0);
}

// Right click: where the panel is shown.
void ToolPanel::showContextMenu(POINT screen) {
    const HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, kMenuTop, L"Panel at Top");
    AppendMenuW(menu, MF_STRING, kMenuBottom, L"Panel at Bottom");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kMenuHide, L"Hide Panel");
    CheckMenuRadioItem(menu, kMenuTop, kMenuBottom, position_ == PanelPosition::Top ? kMenuTop : kMenuBottom,
                       MF_BYCOMMAND);
    const int chosen = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY, screen.x, screen.y, 0, hwnd_, nullptr);
    DestroyMenu(menu);
    // the panel window is recreated: run the command after this message has been handled
    static const MenuIndex commands[] = {MenuPanelTop, MenuPanelBottom, MenuPanelHidden};
    if (chosen >= static_cast<int>(kMenuTop) && chosen <= static_cast<int>(kMenuHide) && hwnd_) {
        PostMessageW(hwnd_, WM_APP_RUN, static_cast<WPARAM>(commands[chosen - 1]), 0);
    }
}

LRESULT CALLBACK ToolPanel::windowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    ToolPanel& self = instance();
    if (hwnd != self.hwnd_) return DefWindowProcW(hwnd, message, wParam, lParam);  // being created or destroyed

    switch (message) {
        case WM_SIZE:
            self.layout();
            return 0;

        case WM_ERASEBKGND:
            return 1;

        case WM_PAINT:
            self.paint();
            return 0;

        case WM_MOUSEMOVE: {
            if (!self.trackingMouse_) {
                TRACKMOUSEEVENT track = {sizeof(track), TME_LEAVE, hwnd, 0};
                self.trackingMouse_ = TrackMouseEvent(&track) != 0;
            }
            self.setHot(self.hitTest({GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)}));
            return 0;
        }

        case WM_MOUSELEAVE:
            self.trackingMouse_ = false;
            self.setHot(-1);
            return 0;

        case WM_LBUTTONDOWN:
        case WM_LBUTTONDBLCLK: {
            const int index = self.hitTest({GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)});
            self.setHot(index);
            if (index == kChevronIndex) {
                self.showOverflowMenu();
            } else if (index >= 0) {
                self.pressed_ = index;
                SetCapture(hwnd);
                self.refresh();
            }
            return 0;
        }

        case WM_LBUTTONUP: {
            const int pressed = self.pressed_;
            if (pressed < 0) return 0;
            ReleaseCapture();  // resets pressed_
            if (pressed == self.hitTest({GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)})) {
                PostMessageW(hwnd, WM_APP_RUN, static_cast<WPARAM>(kItems[pressed].command), 0);
            }
            return 0;
        }

        case WM_CAPTURECHANGED:
            self.pressed_ = -1;
            self.refresh();
            return 0;

        case WM_CONTEXTMENU: {
            POINT screen = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            if (screen.x == -1 && screen.y == -1) GetCursorPos(&screen);
            self.showContextMenu(screen);
            return 0;
        }

        case WM_MOUSEACTIVATE:
            return MA_NOACTIVATE;  // the keyboard focus stays in the editor

        case WM_NOTIFY: {
            auto* header = reinterpret_cast<NMHDR*>(lParam);
            if (header && header->hwndFrom == self.tooltip_ && header->code == TTN_GETDISPINFOW) {
                auto* info = reinterpret_cast<NMTTDISPINFOW*>(lParam);
                self.tooltipBuffer_ = self.tooltipText(static_cast<int>(header->idFrom));
                info->lpszText = self.tooltipBuffer_.data();
                return 0;
            }
            break;
        }

        case WM_APP_RUN:
            // may move or remove the panel (and with it this window)
            runMenuCommand(static_cast<MenuIndex>(wParam));
            SetFocus(Editor::currentHandle());
            return 0;

        case WM_DPICHANGED_AFTERPARENT:
            PostMessageW(hwnd, WM_APP_REAPPLY, 0, 0);
            return 0;

        case WM_APP_REAPPLY: {
            const PanelPosition position = self.position_;
            self.destroy(false);
            self.apply(position);
            return 0;
        }
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}
