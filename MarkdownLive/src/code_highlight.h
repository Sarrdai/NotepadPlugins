// Syntax highlighting of fenced code blocks inside the Markdown editor.
//
// Every code block is colored with the lexer Notepad++ uses for its language (created with
// NPPM_CREATELEXER in a hidden Scintilla view) and the colors of the active Notepad++ theme
// (stylers.xml / themes\*.xml, keywords from langs.xml). The colors are applied as a text
// color indicator, so the Markdown lexer of the document keeps working undisturbed.
// Requires Notepad++ 8.4 or newer; on older versions nothing is highlighted.
#pragma once

#include <windows.h>

#include <cstdint>
#include <map>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "code_syntax.h"

class Editor;

class CodeHighlighter {
public:
    static CodeHighlighter& instance();

    void init();  // Notepad++ is ready
    void setEnabled(bool enabled);

    void onBufferActivated();
    void onTextChanged();      // debounced
    void onStylesChanged();    // theme / dark mode / Style Configurator
    void onBufferClosed(intptr_t bufferId);
    void shutdown();

private:
    struct LanguageInfo {
        bool supported = true;
        std::string lexer;
        std::vector<std::pair<int, std::string>> keywords;  // SCI_SETKEYWORDS index -> words
        std::map<int, int> colors;                           // style id -> Scintilla color (BGR)
        int defaultColor = -1;
    };

    CodeHighlighter() = default;

    void update();
    void loadConfiguration();
    LanguageInfo* language(const std::string& name);
    bool colorize(const Editor& editor, intptr_t start, const std::string& code, const std::string& languageName,
                  LanguageInfo& info);
    void clearRange(const Editor& editor, intptr_t start, intptr_t end) const;

    static void CALLBACK timerProc(HWND, UINT, UINT_PTR, DWORD);

    bool enabled_ = true;
    bool ready_ = false;
    bool configLoaded_ = false;
    int indicator_ = 0;
    UINT_PTR timer_ = 0;

    HWND hostWindow_ = nullptr;
    HWND lexerView_ = nullptr;      // hidden Scintilla used to run the lexers
    std::string lexerLanguage_;     // language currently set up in lexerView_

    std::map<std::string, syntax::Language> languages_;
    syntax::Theme theme_;
    std::unordered_map<std::string, LanguageInfo> languageInfo_;

    // buffer id -> hashes of the code blocks that are highlighted in it
    std::unordered_map<intptr_t, std::unordered_set<size_t>> highlighted_;
};
