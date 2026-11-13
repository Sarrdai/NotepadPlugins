// Thin wrapper around the active Scintilla view.
#pragma once

#include <windows.h>

#include <string>

class Editor {
public:
    explicit Editor(HWND scintilla) : hwnd_(scintilla) {}

    // The Scintilla view that currently has the focus in Notepad++.
    static Editor current();
    static HWND currentHandle();

    HWND handle() const { return hwnd_; }
    intptr_t call(UINT message, uintptr_t wParam = 0, intptr_t lParam = 0) const;

    UINT codePage() const;     // Windows code page of the document bytes
    std::string eol() const;   // "\r\n", "\n" or "\r"
    bool readOnly() const;

    intptr_t length() const;
    intptr_t lineCount() const;
    intptr_t caret() const;
    intptr_t selectionStart() const;
    intptr_t selectionEnd() const;
    intptr_t lineFromPosition(intptr_t pos) const;
    intptr_t lineStart(intptr_t line) const;
    intptr_t lineEnd(intptr_t line) const;  // position before the EOL characters

    std::string text() const;
    std::string range(intptr_t start, intptr_t end) const;
    std::string line(intptr_t line) const;  // without EOL

    void replaceRange(intptr_t start, intptr_t end, const std::string& text) const;
    void select(intptr_t anchor, intptr_t caret) const;

    // Top document line and whether the view is scrolled to the very end.
    intptr_t firstVisibleDocLine(bool* atEnd = nullptr) const;

private:
    HWND hwnd_;
};

// RAII undo group: all changes made while it is alive are undone in one step.
class UndoGroup {
public:
    explicit UndoGroup(const Editor& editor);
    ~UndoGroup();
    UndoGroup(const UndoGroup&) = delete;
    UndoGroup& operator=(const UndoGroup&) = delete;

private:
    const Editor& editor_;
};
