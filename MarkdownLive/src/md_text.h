// Pure Markdown text transformations (no Windows / Notepad++ dependencies).
// Everything works on UTF-8 (or any ASCII compatible) byte strings and is unit tested
// in tests/md_text_tests.cpp.
#pragma once

#include <string>
#include <vector>

namespace md {

// ---- inline markers (bold, italic, strikethrough, code) -------------------------------

struct WrapResult {
    std::string text;        // replacement for [selStart - eraseBefore, selEnd + eraseAfter)
    size_t eraseBefore = 0;  // bytes to remove in front of the selection
    size_t eraseAfter = 0;   // bytes to remove behind the selection
    size_t selStart = 0;     // new selection inside `text`
    size_t selEnd = 0;
};

// Adds or removes `marker` around `selection`. `before` / `after` are a few bytes of
// context directly adjacent to the selection (used to detect existing markers).
WrapResult toggleWrap(const std::string& selection, const std::string& before,
                      const std::string& after, const std::string& marker);

// ---- line based commands ----------------------------------------------------------------

enum class LinePrefix { Bullet, Ordered, Quote };

std::vector<std::string> toggleLinePrefix(const std::vector<std::string>& lines, LinePrefix kind);

// plain line -> "- [ ] line", "- item" -> "- [ ] item", "[ ]" <-> "[x]"
std::string toggleTask(const std::string& line);
bool isTaskLine(const std::string& line);

// delta > 0 adds '#', delta < 0 removes '#'
std::vector<std::string> changeHeading(const std::vector<std::string>& lines, int delta);

// ---- smart enter --------------------------------------------------------------------------

struct EnterResult {
    bool handled = false;
    bool clearPrevious = false;  // previous line was an empty list item: end the list
    std::string prefix;          // text to put in front of the new line
};

// `previousLine` is the line the caret was on when Enter was pressed, `newLineBlank`
// tells whether the freshly created line is empty (apart from auto indentation).
EnterResult continueList(const std::string& previousLine, bool newLineBlank);

// ---- tables ---------------------------------------------------------------------------------

bool isTableLine(const std::string& line);
std::vector<std::string> formatTable(const std::vector<std::string>& lines);
std::string newTable(int columns, int rows, const std::string& eol);

enum class TableOp { RowAbove, RowBelow, DeleteRow, ColumnLeft, ColumnRight, DeleteColumn };

struct TableEdit {
    bool ok = false;
    std::string error;                // reason if !ok
    std::vector<std::string> lines;   // the whole table, formatted
    size_t row = 0;                   // line index of the cell the caret should go to
    size_t column = 0;
};

// Column (0-based) of the cell that contains byte `offset` of `line`.
size_t tableColumnAt(const std::string& line, size_t offset);
// Byte offset of the first character of the text of cell `column` in `line`.
size_t tableCellOffset(const std::string& line, size_t column);
// Inserts / deletes the row `row` or the column `column` of the table in `lines`
// (the header and delimiter rows can be neither deleted nor preceded by a new row).
TableEdit editTable(const std::vector<std::string>& lines, size_t row, size_t column, TableOp op);

// ---- table of contents ------------------------------------------------------------------

extern const char* const kTocBegin;  // "<!-- TOC -->"
extern const char* const kTocEnd;    // "<!-- /TOC -->"

std::string slugify(const std::string& text);
std::string headingPlainText(const std::string& headingMarkdown);
// Returns the list lines of the table of contents (without the TOC markers).
std::vector<std::string> buildToc(const std::vector<std::string>& documentLines);

// ---- helpers ----------------------------------------------------------------------------------

std::vector<std::string> splitLines(const std::string& text);  // handles \n, \r\n and \r
std::string joinLines(const std::vector<std::string>& lines, const std::string& eol);
size_t displayWidth(const std::string& utf8);

}  // namespace md
