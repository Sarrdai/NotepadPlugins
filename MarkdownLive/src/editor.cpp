#include "editor.h"

#include "npp_api.h"
#include "plugin.h"

Editor Editor::current() { return Editor(currentHandle()); }

HWND Editor::currentHandle() {
    int which = 0;
    SendMessageW(g_npp.nppHandle, NPPM_GETCURRENTSCINTILLA, 0, reinterpret_cast<LPARAM>(&which));
    return which == 1 ? g_npp.scintillaSecondHandle : g_npp.scintillaMainHandle;
}

intptr_t Editor::call(UINT message, uintptr_t wParam, intptr_t lParam) const {
    return SendMessageW(hwnd_, message, wParam, lParam);
}

UINT Editor::codePage() const {
    const auto cp = static_cast<UINT>(call(SCI_GETCODEPAGE));
    return cp == 0 ? CP_ACP : cp;
}

std::string Editor::eol() const {
    switch (call(SCI_GETEOLMODE)) {
        case SC_EOL_CR: return "\r";
        case SC_EOL_LF: return "\n";
        default: return "\r\n";
    }
}

bool Editor::readOnly() const { return call(SCI_GETREADONLY) != 0; }
intptr_t Editor::length() const { return call(SCI_GETLENGTH); }
intptr_t Editor::lineCount() const { return call(SCI_GETLINECOUNT); }
intptr_t Editor::caret() const { return call(SCI_GETCURRENTPOS); }
intptr_t Editor::selectionStart() const { return call(SCI_GETSELECTIONSTART); }
intptr_t Editor::selectionEnd() const { return call(SCI_GETSELECTIONEND); }
intptr_t Editor::lineFromPosition(intptr_t pos) const { return call(SCI_LINEFROMPOSITION, static_cast<uintptr_t>(pos)); }
intptr_t Editor::lineStart(intptr_t line) const { return call(SCI_POSITIONFROMLINE, static_cast<uintptr_t>(line)); }
intptr_t Editor::lineEnd(intptr_t line) const { return call(SCI_GETLINEENDPOSITION, static_cast<uintptr_t>(line)); }

std::string Editor::text() const {
    const intptr_t len = length();
    if (len <= 0) return {};
    std::string buffer(static_cast<size_t>(len) + 1, '\0');
    call(SCI_GETTEXT, static_cast<uintptr_t>(len) + 1, reinterpret_cast<intptr_t>(&buffer[0]));
    buffer.resize(static_cast<size_t>(len));
    return buffer;
}

std::string Editor::range(intptr_t start, intptr_t end) const {
    if (end <= start) return {};
    call(SCI_SETTARGETRANGE, static_cast<uintptr_t>(start), end);
    std::string buffer(static_cast<size_t>(end - start) + 1, '\0');
    call(SCI_GETTARGETTEXT, 0, reinterpret_cast<intptr_t>(&buffer[0]));
    buffer.resize(static_cast<size_t>(end - start));
    return buffer;
}

std::string Editor::line(intptr_t line) const { return range(lineStart(line), lineEnd(line)); }

void Editor::replaceRange(intptr_t start, intptr_t end, const std::string& text) const {
    call(SCI_SETTARGETRANGE, static_cast<uintptr_t>(start), end);
    call(SCI_REPLACETARGET, text.size(), reinterpret_cast<intptr_t>(text.data()));
}

void Editor::select(intptr_t anchor, intptr_t caret) const {
    call(SCI_SETSEL, static_cast<uintptr_t>(anchor), caret);
}

intptr_t Editor::firstVisibleDocLine(bool* atEnd) const {
    const intptr_t firstVisible = call(SCI_GETFIRSTVISIBLELINE);
    if (atEnd) {
        const intptr_t lastDisplayLine = call(SCI_VISIBLEFROMDOCLINE, static_cast<uintptr_t>(lineCount() - 1));
        *atEnd = firstVisible > 0 && firstVisible + call(SCI_LINESONSCREEN) > lastDisplayLine;
    }
    return call(SCI_DOCLINEFROMVISIBLE, static_cast<uintptr_t>(firstVisible));
}

UndoGroup::UndoGroup(const Editor& editor) : editor_(editor) { editor_.call(SCI_BEGINUNDOACTION); }
UndoGroup::~UndoGroup() { editor_.call(SCI_ENDUNDOACTION); }
