// Platform independent helpers for highlighting fenced code blocks in the editor:
// finding the blocks, mapping their info string to a Notepad++ language / Lexilla lexer
// and reading the Notepad++ configuration files (langs.xml, stylers.xml / themes, config.xml).
// Unit tested in tests/md_text_tests.cpp.
#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace syntax {

// ---- fenced code blocks ----------------------------------------------------------------

struct CodeBlock {
    size_t bodyStart = 0;  // first byte of the line after the opening fence
    size_t bodyEnd = 0;    // first byte of the closing fence line (or end of text)
    std::string language;  // normalized first word of the info string ("" if none)
};

std::vector<CodeBlock> findCodeBlocks(const std::string& text);

// "{.Python title=x}" -> "python", "language-js" -> "js"
std::string normalizeInfo(const std::string& info);

// ---- minimal XML reader ----------------------------------------------------------------

struct XmlElement {
    std::string name;
    std::vector<std::pair<std::string, std::string>> attributes;
    std::string text;  // concatenated character data directly inside the element
    std::vector<XmlElement> children;

    const std::string* attribute(const std::string& key) const;
    std::string attributeOr(const std::string& key, const std::string& fallback = {}) const;
};

// Tolerant parser for the configuration files of Notepad++ (no DTDs, no namespaces).
bool parseXml(const std::string& xml, XmlElement& document);

// ---- Notepad++ configuration -------------------------------------------------------------

struct Style {
    bool hasForeground = false;
    uint32_t foreground = 0;  // 0xRRGGBB
    int fontStyle = 0;        // 1 bold, 2 italic, 4 underline
};

struct Theme {
    Style defaultStyle;                                  // "Default Style" of the global styles
    std::map<std::string, std::map<int, Style>> styles;  // LexerType name -> style id -> style
    // user defined keywords (Style Configurator): LexerType name -> keyword class -> words
    std::map<std::string, std::map<std::string, std::string>> keywords;
    // keyword class -> style id it is shown with, per LexerType
    std::map<std::string, std::map<std::string, int>> keywordStyles;
};

Theme parseTheme(const std::string& xml);

struct Language {
    std::vector<std::string> extensions;          // lower case
    std::map<std::string, std::string> keywords;  // keyword class -> words
};

std::map<std::string, Language> parseLanguages(const std::string& langsXml);

struct NppConfig {
    std::string stylerTheme;  // path of the active theme ("" = stylers.xml)
    bool hasDarkModeEntry = false;
    bool darkModeEnabled = false;
    std::string darkThemeName;
    std::string lightThemeName;
};

NppConfig parseConfig(const std::string& configXml);

// ---- languages ---------------------------------------------------------------------------

// Notepad++ language for the info string of a code block ("" if it cannot be highlighted).
std::string resolveLanguage(const std::string& info, const std::map<std::string, Language>& languages);

// Lexilla lexer used by Notepad++ for `language` (as passed to NPPM_CREATELEXER).
std::string lexerFor(const std::string& language);

// LexerType entries of the theme whose styles apply to `language`, most specific first.
std::vector<std::string> styleSourcesFor(const std::string& language);

// Keyword list index (SCI_SETKEYWORDS) of a keyword class, -1 if it is not used.
int keywordSetFor(const std::string& lexer, const std::string& keywordClass, int styleId);

}  // namespace syntax
