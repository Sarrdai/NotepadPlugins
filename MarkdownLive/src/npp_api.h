// Minimal subset of the Notepad++ plugin interface and Scintilla messages used by this
// plugin. Values match the official headers (Notepad_plus_msgs.h, PluginInterface.h,
// Docking.h, Scintilla.h) of Notepad++ 8.x.
#pragma once

#include <windows.h>

// ---- plugin interface ---------------------------------------------------------------------

struct NppData {
    HWND nppHandle = nullptr;
    HWND scintillaMainHandle = nullptr;
    HWND scintillaSecondHandle = nullptr;
};

struct ShortcutKey {
    bool isCtrl = false;
    bool isAlt = false;
    bool isShift = false;
    UCHAR key = 0;
};

typedef void(__cdecl* PFUNCPLUGINCMD)();

constexpr int kMenuItemSize = 64;

struct FuncItem {
    wchar_t itemName[kMenuItemSize] = {0};
    PFUNCPLUGINCMD pFunc = nullptr;
    int cmdID = 0;
    bool init2Check = false;
    ShortcutKey* pShKey = nullptr;
};

struct ToolbarIconsWithDarkMode {
    HBITMAP hToolbarBmp = nullptr;
    HICON hToolbarIcon = nullptr;
    HICON hToolbarIconDarkMode = nullptr;
};

// ---- Notepad++ messages -----------------------------------------------------------------

constexpr UINT NPPMSG = WM_USER + 1000;
constexpr UINT NPPM_GETCURRENTSCINTILLA = NPPMSG + 4;
constexpr UINT NPPM_CREATESCINTILLAHANDLE = NPPMSG + 20;
constexpr UINT NPPM_GETCURRENTDOCINDEX = NPPMSG + 23;
constexpr UINT NPPM_ACTIVATEDOC = NPPMSG + 28;
constexpr UINT NPPM_DMMSHOW = NPPMSG + 30;
constexpr UINT NPPM_DMMHIDE = NPPMSG + 31;
constexpr UINT NPPM_DMMUPDATEDISPINFO = NPPMSG + 32;
constexpr UINT NPPM_DMMREGASDCKDLG = NPPMSG + 33;
constexpr UINT NPPM_SETMENUITEMCHECK = NPPMSG + 40;
constexpr UINT NPPM_GETPLUGINSCONFIGDIR = NPPMSG + 46;
constexpr UINT NPPM_GETPOSFROMBUFFERID = NPPMSG + 57;
constexpr UINT NPPM_GETFULLPATHFROMBUFFERID = NPPMSG + 58;
constexpr UINT NPPM_GETBUFFERIDFROMPOS = NPPMSG + 59;
constexpr UINT NPPM_GETCURRENTBUFFERID = NPPMSG + 60;
constexpr UINT NPPM_GETSHORTCUTBYCMDID = NPPMSG + 76;
constexpr UINT NPPM_DOOPEN = NPPMSG + 77;
constexpr UINT NPPM_GETCURRENTVIEW = NPPMSG + 88;
constexpr UINT NPPM_ADDTOOLBARICON_FORDARKMODE = NPPMSG + 101;
constexpr UINT NPPM_ISDARKMODEENABLED = NPPMSG + 107;
constexpr UINT NPPM_GETDARKMODECOLORS = NPPMSG + 108;
constexpr UINT NPPM_CREATELEXER = NPPMSG + 110;        // Notepad++ 8.4+
constexpr UINT NPPM_ALLOCATEINDICATOR = NPPMSG + 113;  // Notepad++ 8.5.6+
constexpr UINT NPPM_GETNPPDIRECTORY = WM_USER + 3000 + 7;

constexpr int MAIN_VIEW = 0;
constexpr int SUB_VIEW = 1;

// Colors of the active dark mode tone (NppDarkMode::Colors).
struct NppDarkModeColors {
    COLORREF background = 0;
    COLORREF softerBackground = 0;
    COLORREF hotBackground = 0;
    COLORREF pureBackground = 0;
    COLORREF errorBackground = 0;
    COLORREF text = 0;
    COLORREF darkerText = 0;
    COLORREF disabledText = 0;
    COLORREF linkText = 0;
    COLORREF edge = 0;
    COLORREF hotEdge = 0;
    COLORREF disabledEdge = 0;
};

// ---- Notepad++ notifications -----------------------------------------------------------

constexpr UINT NPPN_FIRST = 1000;
constexpr UINT NPPN_READY = NPPN_FIRST + 1;
constexpr UINT NPPN_TBMODIFICATION = NPPN_FIRST + 2;
constexpr UINT NPPN_FILECLOSED = NPPN_FIRST + 5;
constexpr UINT NPPN_FILESAVED = NPPN_FIRST + 8;
constexpr UINT NPPN_SHUTDOWN = NPPN_FIRST + 9;
constexpr UINT NPPN_BUFFERACTIVATED = NPPN_FIRST + 10;
constexpr UINT NPPN_WORDSTYLESUPDATED = NPPN_FIRST + 12;
constexpr UINT NPPN_FILERENAMED = NPPN_FIRST + 23;
constexpr UINT NPPN_DARKMODECHANGED = NPPN_FIRST + 27;

// ---- docking ------------------------------------------------------------------------------

constexpr UINT DWS_ICONTAB = 0x00000001;
constexpr UINT DWS_ADDINFO = 0x00000004;
constexpr UINT DWS_USEOWNDARKMODE = 0x00000008;
constexpr UINT DWS_DF_CONT_RIGHT = 1u << 28;

constexpr UINT DMN_FIRST = 1050;
constexpr UINT DMN_CLOSE = DMN_FIRST + 1;

struct DockedWidgetData {
    HWND hClient = nullptr;
    const wchar_t* pszName = nullptr;
    int dlgID = 0;
    UINT uMask = 0;
    HICON hIconTab = nullptr;
    const wchar_t* pszAddInfo = nullptr;
    RECT rcFloat = {};
    int iPrevCont = 0;
    const wchar_t* pszModuleName = nullptr;
};

// ---- Scintilla ----------------------------------------------------------------------------

typedef ptrdiff_t Sci_Position;

struct Sci_NotifyHeader {
    void* hwndFrom;
    uintptr_t idFrom;
    unsigned int code;
};

struct SCNotification {
    Sci_NotifyHeader nmhdr;
    Sci_Position position;
    int ch;
    int modifiers;
    int modificationType;
    const char* text;
    Sci_Position length;
    Sci_Position linesAdded;
    int message;
    uintptr_t wParam;
    intptr_t lParam;
    Sci_Position line;
    int foldLevelNow;
    int foldLevelPrev;
    int margin;
    int listType;
    int x;
    int y;
    int token;
    Sci_Position annotationLinesAdded;
    int updated;
    int listCompletionMethod;
    int characterSource;
};

constexpr UINT SCI_CLEARALL = 2004;
constexpr UINT SCI_GETLENGTH = 2006;
constexpr UINT SCI_GETCURRENTPOS = 2008;
constexpr UINT SCI_GETSTYLEAT = 2010;
constexpr UINT SCI_SETUNDOCOLLECTION = 2012;
constexpr UINT SCI_GOTOLINE = 2024;
constexpr UINT SCI_GETEOLMODE = 2030;
constexpr UINT SCI_SETCODEPAGE = 2037;
constexpr UINT SCI_BEGINUNDOACTION = 2078;
constexpr UINT SCI_ENDUNDOACTION = 2079;
constexpr UINT SCI_INDICSETSTYLE = 2080;
constexpr UINT SCI_GETLINEENDPOSITION = 2136;
constexpr UINT SCI_GETCODEPAGE = 2137;
constexpr UINT SCI_GETREADONLY = 2140;
constexpr UINT SCI_GETSELECTIONSTART = 2143;
constexpr UINT SCI_GETSELECTIONEND = 2145;
constexpr UINT SCI_GETFIRSTVISIBLELINE = 2152;
constexpr UINT SCI_GETLINECOUNT = 2154;
constexpr UINT SCI_SETSEL = 2160;
constexpr UINT SCI_LINEFROMPOSITION = 2166;
constexpr UINT SCI_POSITIONFROMLINE = 2167;
constexpr UINT SCI_SCROLLCARET = 2169;
constexpr UINT SCI_GETTEXT = 2182;
constexpr UINT SCI_GETDIRECTFUNCTION = 2184;
constexpr UINT SCI_GETDIRECTPOINTER = 2185;
constexpr UINT SCI_REPLACETARGET = 2194;
constexpr UINT SCI_VISIBLEFROMDOCLINE = 2220;
constexpr UINT SCI_DOCLINEFROMVISIBLE = 2221;
constexpr UINT SCI_ENSUREVISIBLE = 2232;
constexpr UINT SCI_WORDSTARTPOSITION = 2266;
constexpr UINT SCI_WORDENDPOSITION = 2267;
constexpr UINT SCI_APPENDTEXT = 2282;
constexpr UINT SCI_LINESONSCREEN = 2370;
constexpr UINT SCI_GRABFOCUS = 2400;
constexpr UINT SCI_SETINDICATORCURRENT = 2500;
constexpr UINT SCI_SETINDICATORVALUE = 2502;
constexpr UINT SCI_INDICATORFILLRANGE = 2504;
constexpr UINT SCI_INDICATORCLEARRANGE = 2505;
constexpr UINT SCI_INDICATORVALUEAT = 2507;
constexpr UINT SCI_GETSELECTIONS = 2570;
constexpr UINT SCI_SETFIRSTVISIBLELINE = 2613;
constexpr UINT SCI_SETTARGETRANGE = 2686;
constexpr UINT SCI_GETTARGETTEXT = 2687;
constexpr UINT SCI_INDICSETFLAGS = 2684;
constexpr UINT SCI_COLOURISE = 4003;
constexpr UINT SCI_SETKEYWORDS = 4005;
constexpr UINT SCI_SETILEXER = 4033;

constexpr int INDIC_TEXTFORE = 17;
constexpr int SC_INDICFLAG_VALUEFORE = 1;
constexpr int SC_INDICVALUEBIT = 0x1000000;

constexpr int SC_EOL_CRLF = 0;
constexpr int SC_EOL_CR = 1;
constexpr int SC_EOL_LF = 2;
constexpr int SC_CP_UTF8 = 65001;

constexpr int SC_MOD_INSERTTEXT = 0x1;
constexpr int SC_MOD_DELETETEXT = 0x2;

constexpr UINT SCN_CHARADDED = 2001;
constexpr UINT SCN_UPDATEUI = 2007;
constexpr UINT SCN_MODIFIED = 2008;
