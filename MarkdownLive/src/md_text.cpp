#include "md_text.h"

#include <algorithm>
#include <map>

namespace md {

const char* const kTocBegin = "<!-- TOC -->";
const char* const kTocEnd = "<!-- /TOC -->";

namespace {

bool isSpace(char c) { return c == ' ' || c == '\t'; }
bool isDigit(char c) { return c >= '0' && c <= '9'; }
bool isAlnum(char c) {
    return isDigit(c) || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (static_cast<unsigned char>(c) >= 0x80);
}

bool isBlank(const std::string& s) {
    return std::all_of(s.begin(), s.end(), [](char c) { return isSpace(c) || c == '\r' || c == '\n'; });
}

std::string trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && isSpace(s[a])) ++a;
    while (b > a && isSpace(s[b - 1])) --b;
    return s.substr(a, b - a);
}

bool startsWith(const std::string& s, const std::string& p, size_t at = 0) {
    return s.size() >= at + p.size() && s.compare(at, p.size(), p) == 0;
}

bool endsWith(const std::string& s, const std::string& p) {
    return s.size() >= p.size() && s.compare(s.size() - p.size(), p.size(), p) == 0;
}

// ---- UTF-8 ------------------------------------------------------------------------------

// Decodes one code point starting at `i` and advances `i`. Invalid bytes are returned as-is.
char32_t decodeUtf8(const std::string& s, size_t& i) {
    const auto b0 = static_cast<unsigned char>(s[i]);
    int extra = 0;
    char32_t cp = b0;
    if (b0 >= 0xF0 && b0 <= 0xF7) { extra = 3; cp = b0 & 0x07; }
    else if (b0 >= 0xE0 && b0 <= 0xEF) { extra = 2; cp = b0 & 0x0F; }
    else if (b0 >= 0xC0 && b0 <= 0xDF) { extra = 1; cp = b0 & 0x1F; }
    if (extra > 0 && i + extra >= s.size()) { ++i; return b0; }
    for (int k = 1; k <= extra; ++k) {
        const auto b = static_cast<unsigned char>(s[i + k]);
        if ((b & 0xC0) != 0x80) { ++i; return b0; }
        cp = (cp << 6) | (b & 0x3F);
    }
    i += static_cast<size_t>(extra) + 1;
    return cp;
}

void appendUtf8(std::string& out, char32_t cp) {
    if (cp < 0x80) out += static_cast<char>(cp);
    else if (cp < 0x800) {
        out += static_cast<char>(0xC0 | (cp >> 6));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        out += static_cast<char>(0xE0 | (cp >> 12));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (cp >> 18));
        out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    }
}

// ---- line structure -------------------------------------------------------------------

struct LineParts {
    std::string indent;  // leading whitespace
    std::string quote;   // "> > " ...
    std::string marker;  // "-", "*", "+", "12." or "3)"
    std::string task;    // "[ ]" / "[x]" or empty
    std::string rest;    // remaining text
    bool ordered = false;
    long number = 0;
    char delimiter = '.';
};

LineParts parseLine(const std::string& line) {
    LineParts p;
    size_t i = 0;
    const size_t n = line.size();
    while (i < n && isSpace(line[i])) ++i;
    p.indent = line.substr(0, i);

    size_t q = i;
    while (q < n && line[q] == '>') {
        ++q;
        if (q < n && line[q] == ' ') ++q;
    }
    p.quote = line.substr(i, q - i);
    i = q;

    auto followedBySpaceOrEnd = [&](size_t at) { return at >= n || isSpace(line[at]); };

    size_t m = i;
    if (m < n && (line[m] == '-' || line[m] == '*' || line[m] == '+') && followedBySpaceOrEnd(m + 1)) {
        p.marker = line.substr(m, 1);
        m += 1;
    } else {
        size_t d = m;
        while (d < n && isDigit(line[d]) && d - m < 9) ++d;
        if (d > m && d < n && (line[d] == '.' || line[d] == ')') && followedBySpaceOrEnd(d + 1)) {
            p.ordered = true;
            p.number = std::stol(line.substr(m, d - m));
            p.delimiter = line[d];
            p.marker = line.substr(m, d - m + 1);
            m = d + 1;
        }
    }

    if (!p.marker.empty()) {
        while (m < n && isSpace(line[m])) ++m;
        if (m + 2 < n && line[m] == '[' && line[m + 2] == ']' &&
            (line[m + 1] == ' ' || line[m + 1] == 'x' || line[m + 1] == 'X') && followedBySpaceOrEnd(m + 3)) {
            p.task = line.substr(m, 3);
            m += 3;
            while (m < n && isSpace(line[m])) ++m;
        }
        i = m;
    }
    p.rest = line.substr(i);
    return p;
}

bool isThematicBreak(const std::string& line) {
    char kind = 0;
    int count = 0;
    for (char c : line) {
        if (isSpace(c)) continue;
        if (c != '-' && c != '*' && c != '_') return false;
        if (kind && c != kind) return false;
        kind = c;
        ++count;
    }
    return count >= 3;
}

// Length of the run of `ch` at the end (fromEnd) or start of `s`.
size_t runLength(const std::string& s, char ch, bool fromEnd) {
    size_t n = 0;
    if (fromEnd) {
        while (n < s.size() && s[s.size() - 1 - n] == ch) ++n;
    } else {
        while (n < s.size() && s[n] == ch) ++n;
    }
    return n;
}

// Is a run of `run` marker characters on both sides an instance of `marker`?
bool runMatchesMarker(size_t run, const std::string& marker) {
    if (run < marker.size()) return false;
    // "*" (italic) must not be confused with "**" (bold): only odd runs contain an italic marker
    if (marker == "*" || marker == "_") return run % 2 == 1;
    return true;
}

}  // namespace

// =================================================================================================
// helpers
// =================================================================================================

std::vector<std::string> splitLines(const std::string& text) {
    std::vector<std::string> lines;
    size_t start = 0;
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '\n' || text[i] == '\r') {
            lines.push_back(text.substr(start, i - start));
            if (text[i] == '\r' && i + 1 < text.size() && text[i + 1] == '\n') ++i;
            start = i + 1;
        }
    }
    lines.push_back(text.substr(start));
    return lines;
}

std::string joinLines(const std::vector<std::string>& lines, const std::string& eol) {
    std::string out;
    for (size_t i = 0; i < lines.size(); ++i) {
        if (i) out += eol;
        out += lines[i];
    }
    return out;
}

size_t displayWidth(const std::string& utf8) {
    size_t width = 0;
    for (size_t i = 0; i < utf8.size();) {
        const char32_t c = decodeUtf8(utf8, i);
        if ((c >= 0x300 && c <= 0x36F) || (c >= 0x200B && c <= 0x200D) || (c >= 0xFE00 && c <= 0xFE0F)) continue;
        const bool wide = (c >= 0x1100 && c <= 0x115F) || (c >= 0x2E80 && c <= 0xA4CF) ||
                          (c >= 0xAC00 && c <= 0xD7A3) || (c >= 0xF900 && c <= 0xFAFF) ||
                          (c >= 0xFE30 && c <= 0xFE4F) || (c >= 0xFF00 && c <= 0xFF60) ||
                          (c >= 0xFFE0 && c <= 0xFFE6) || (c >= 0x1F300 && c <= 0x1FAFF);
        width += wide ? 2 : 1;
    }
    return width;
}

// =================================================================================================
// inline markers
// =================================================================================================

WrapResult toggleWrap(const std::string& selection, const std::string& before,
                      const std::string& after, const std::string& marker) {
    WrapResult r;
    const size_t m = marker.size();
    const char ch = marker[0];
    const bool uniform = std::all_of(marker.begin(), marker.end(), [&](char c) { return c == ch; });

    auto markersAround = [&](const std::string& left, const std::string& right) {
        if (!endsWith(left, marker) || !startsWith(right, marker)) return false;
        if (!uniform) return true;
        return runMatchesMarker(std::min(runLength(left, ch, true), runLength(right, ch, false)), marker);
    };

    // 1) markers directly outside the selection (or around the caret): remove them
    if (markersAround(before, after)) {
        r.eraseBefore = m;
        r.eraseAfter = m;
        r.text = selection;
        r.selStart = 0;
        r.selEnd = selection.size();
        return r;
    }

    // 2) the selection itself includes the markers: remove them
    if (selection.size() >= 2 * m && startsWith(selection, marker) && endsWith(selection, marker)) {
        bool wrapped = true;
        if (uniform) {
            const size_t lead = runLength(selection, ch, false);
            const size_t tail = runLength(selection, ch, true);
            wrapped = lead < selection.size() && runMatchesMarker(std::min(lead, tail), marker);
        }
        if (wrapped) {
            r.text = selection.substr(m, selection.size() - 2 * m);
            r.selStart = 0;
            r.selEnd = r.text.size();
            return r;
        }
    }

    // 3) add markers; keep surrounding whitespace outside ("**word** " instead of "**word **")
    size_t a = 0, b = selection.size();
    while (a < b && (isSpace(selection[a]) || selection[a] == '\n' || selection[a] == '\r')) ++a;
    while (b > a && (isSpace(selection[b - 1]) || selection[b - 1] == '\n' || selection[b - 1] == '\r')) --b;
    if (a == b) { a = 0; b = selection.size(); }
    r.text = selection.substr(0, a) + marker + selection.substr(a, b - a) + marker + selection.substr(b);
    r.selStart = a + m;
    r.selEnd = b + m;
    return r;
}

// =================================================================================================
// line based commands
// =================================================================================================

std::vector<std::string> toggleLinePrefix(const std::vector<std::string>& lines, LinePrefix kind) {
    std::vector<LineParts> parts;
    parts.reserve(lines.size());
    for (const auto& l : lines) parts.push_back(parseLine(l));

    bool all = true;
    bool any = false;
    for (size_t i = 0; i < lines.size(); ++i) {
        if (isBlank(lines[i])) continue;
        any = true;
        const auto& p = parts[i];
        const bool has = kind == LinePrefix::Quote ? !p.quote.empty()
                         : kind == LinePrefix::Ordered ? p.ordered
                         : (!p.marker.empty() && !p.ordered);
        if (!has) all = false;
    }
    const bool remove = any && all;

    std::vector<std::string> out;
    out.reserve(lines.size());
    long number = 1;
    for (size_t i = 0; i < lines.size(); ++i) {
        const auto& p = parts[i];
        const std::string task = p.task.empty() ? "" : p.task + " ";
        const std::string list = p.marker.empty() ? "" : p.marker + " " + task;

        if (kind == LinePrefix::Quote) {
            if (remove) {
                if (p.quote.empty()) { out.push_back(lines[i]); continue; }
                size_t cut = 1;
                if (p.quote.size() > 1 && p.quote[1] == ' ') cut = 2;
                out.push_back(p.indent + p.quote.substr(cut) + list + p.rest);
            } else if (any) {
                out.push_back(isBlank(lines[i]) ? p.indent + ">" : p.indent + "> " + p.quote + list + p.rest);
            } else {
                out.push_back(p.indent + "> " + p.rest);
            }
            continue;
        }

        if (isBlank(lines[i]) && any) { out.push_back(lines[i]); continue; }

        if (remove) {
            out.push_back(p.indent + p.quote + p.rest);
        } else if (kind == LinePrefix::Bullet) {
            out.push_back(p.indent + p.quote + "- " + task + p.rest);
        } else {
            out.push_back(p.indent + p.quote + std::to_string(number++) + ". " + task + p.rest);
        }
    }
    return out;
}

bool isTaskLine(const std::string& line) { return !parseLine(line).task.empty(); }

std::string toggleTask(const std::string& line) {
    const LineParts p = parseLine(line);
    if (p.marker.empty()) return p.indent + p.quote + "- [ ] " + p.rest;
    const std::string task = p.task.empty() ? "[ ]" : (p.task[1] == ' ' ? "[x]" : "[ ]");
    return p.indent + p.quote + p.marker + " " + task + (p.rest.empty() ? "" : " " + p.rest);
}

std::vector<std::string> changeHeading(const std::vector<std::string>& lines, int delta) {
    std::vector<std::string> out;
    out.reserve(lines.size());
    for (const auto& line : lines) {
        if (isBlank(line)) { out.push_back(line); continue; }
        size_t i = 0;
        while (i < line.size() && i < 3 && line[i] == ' ') ++i;
        size_t h = i;
        while (h < line.size() && line[h] == '#') ++h;
        int level = static_cast<int>(h - i);
        std::string text;
        if (level >= 1 && level <= 6 && (h == line.size() || isSpace(line[h]))) {
            text = trim(line.substr(h));
        } else {
            level = 0;
            text = trim(line);
        }
        const int next = std::max(0, std::min(6, level + delta));
        if (next == level) { out.push_back(line); continue; }
        if (next == 0) out.push_back(text);
        else out.push_back(std::string(static_cast<size_t>(next), '#') + (text.empty() ? " " : " " + text));
    }
    return out;
}

// =================================================================================================
// smart enter
// =================================================================================================

EnterResult continueList(const std::string& previousLine, bool newLineBlank) {
    EnterResult r;
    if (isBlank(previousLine) || isThematicBreak(previousLine)) return r;
    const LineParts p = parseLine(previousLine);
    if (p.marker.empty() && p.quote.empty()) return r;

    if (isBlank(p.rest)) {
        // empty item: pressing Enter again ends the list / quote
        if (!newLineBlank) return r;
        r.handled = true;
        r.clearPrevious = true;
        return r;
    }

    r.handled = true;
    r.prefix = p.indent + p.quote;
    if (!p.quote.empty() && p.quote.back() != ' ') r.prefix += ' ';
    if (!p.marker.empty()) {
        r.prefix += p.ordered ? std::to_string(p.number + 1) + p.delimiter : p.marker;
        r.prefix += ' ';
        if (!p.task.empty()) r.prefix += "[ ] ";
    }
    return r;
}

// =================================================================================================
// tables
// =================================================================================================

namespace {

enum class Align { None, Left, Center, Right };

std::vector<std::string> splitRow(const std::string& line) {
    std::string s = trim(line);
    if (!s.empty() && s.front() == '|') s.erase(0, 1);
    bool trailingPipe = !s.empty() && s.back() == '|' && !(s.size() >= 2 && s[s.size() - 2] == '\\');
    if (trailingPipe) s.pop_back();

    std::vector<std::string> cells;
    std::string cell;
    bool inCode = false;
    for (size_t i = 0; i < s.size(); ++i) {
        const char c = s[i];
        if (c == '\\' && i + 1 < s.size()) {
            cell += c;
            cell += s[++i];
        } else if (c == '`') {
            inCode = !inCode;
            cell += c;
        } else if (c == '|' && !inCode) {
            cells.push_back(trim(cell));
            cell.clear();
        } else {
            cell += c;
        }
    }
    cells.push_back(trim(cell));
    return cells;
}

bool isDelimiterCell(const std::string& cell) {
    if (cell.empty()) return false;
    size_t dashes = 0;
    for (size_t i = 0; i < cell.size(); ++i) {
        if (cell[i] == '-') ++dashes;
        else if (cell[i] == ':' && (i == 0 || i == cell.size() - 1)) continue;
        else return false;
    }
    return dashes >= 1;
}

// Index of the delimiter row ("| --- | :-: |") or rows.size() if the table has none.
size_t findDelimiterRow(const std::vector<std::vector<std::string>>& rows) {
    if (rows.size() >= 2 && std::all_of(rows[1].begin(), rows[1].end(), isDelimiterCell)) return 1;
    return rows.size();
}

std::string pad(const std::string& text, size_t width, Align align) {
    const size_t w = displayWidth(text);
    const size_t space = width > w ? width - w : 0;
    if (align == Align::Right) return std::string(space, ' ') + text;
    if (align == Align::Center) return std::string(space / 2, ' ') + text + std::string(space - space / 2, ' ');
    return text + std::string(space, ' ');
}

}  // namespace

bool isTableLine(const std::string& line) {
    return !isBlank(line) && line.find('|') != std::string::npos;
}

std::vector<std::string> formatTable(const std::vector<std::string>& lines) {
    if (lines.empty()) return lines;
    size_t indentLen = 0;
    while (indentLen < lines[0].size() && isSpace(lines[0][indentLen])) ++indentLen;
    const std::string indent = lines[0].substr(0, indentLen);

    std::vector<std::vector<std::string>> rows;
    size_t columns = 0;
    for (const auto& l : lines) {
        rows.push_back(splitRow(l));
        columns = std::max(columns, rows.back().size());
    }

    const size_t delimiterRow = findDelimiterRow(rows);

    std::vector<Align> aligns(columns, Align::None);
    if (delimiterRow < rows.size()) {
        for (size_t c = 0; c < rows[delimiterRow].size(); ++c) {
            const std::string& cell = rows[delimiterRow][c];
            const bool left = cell.front() == ':';
            const bool right = cell.back() == ':' && cell.size() > 1;
            aligns[c] = left && right ? Align::Center : right ? Align::Right : left ? Align::Left : Align::None;
        }
    }

    std::vector<size_t> widths(columns, 3);
    for (size_t r = 0; r < rows.size(); ++r) {
        if (r == delimiterRow) continue;
        for (size_t c = 0; c < rows[r].size(); ++c) widths[c] = std::max(widths[c], displayWidth(rows[r][c]));
    }

    std::vector<std::string> out;
    out.reserve(rows.size());
    for (size_t r = 0; r < rows.size(); ++r) {
        std::string line = indent + "|";
        for (size_t c = 0; c < columns; ++c) {
            line += ' ';
            if (r == delimiterRow) {
                std::string dashes(widths[c], '-');
                if (aligns[c] == Align::Left || aligns[c] == Align::Center) dashes.front() = ':';
                if (aligns[c] == Align::Right || aligns[c] == Align::Center) dashes.back() = ':';
                line += dashes;
            } else {
                line += pad(c < rows[r].size() ? rows[r][c] : std::string(), widths[c], aligns[c]);
            }
            line += " |";
        }
        out.push_back(line);
    }
    return out;
}

std::string newTable(int columns, int rows, const std::string& eol) {
    std::vector<std::string> lines;
    std::string header = "|", delimiter = "|", empty = "|";
    for (int c = 1; c <= columns; ++c) {
        header += " Column " + std::to_string(c) + " |";
        delimiter += " --- |";
        empty += "  |";
    }
    lines.push_back(header);
    lines.push_back(delimiter);
    for (int r = 0; r < rows; ++r) lines.push_back(empty);
    return joinLines(formatTable(lines), eol);
}

namespace {

// Positions of the unescaped pipes outside of code spans (same rules as splitRow).
std::vector<size_t> pipePositions(const std::string& line) {
    std::vector<size_t> pipes;
    bool inCode = false;
    for (size_t i = 0; i < line.size(); ++i) {
        if (line[i] == '\\') ++i;
        else if (line[i] == '`') inCode = !inCode;
        else if (line[i] == '|' && !inCode) pipes.push_back(i);
    }
    return pipes;
}

bool hasLeadingPipe(const std::string& line, const std::vector<size_t>& pipes) {
    if (pipes.empty()) return false;
    for (size_t i = 0; i < pipes.front(); ++i) {
        if (!isSpace(line[i])) return false;
    }
    return true;
}

}  // namespace

size_t tableColumnAt(const std::string& line, size_t offset) {
    const std::vector<size_t> pipes = pipePositions(line);
    size_t column = 0;
    for (const size_t p : pipes) {
        if (p < offset) ++column;
    }
    if (hasLeadingPipe(line, pipes) && column > 0) --column;
    return std::min(column, splitRow(line).size() - 1);
}

size_t tableCellOffset(const std::string& line, size_t column) {
    const std::vector<size_t> pipes = pipePositions(line);
    if (pipes.empty()) return 0;
    const size_t leading = hasLeadingPipe(line, pipes) ? 1 : 0;
    if (!leading && column == 0) {
        size_t i = 0;
        while (i < line.size() && isSpace(line[i])) ++i;
        return i;
    }
    const size_t separator = column + leading - 1;
    if (separator >= pipes.size()) return line.size();
    const size_t start = pipes[separator] + 1;
    size_t text = start;
    while (text < line.size() && isSpace(line[text])) ++text;
    if (text >= line.size() || line[text] == '|') return std::min(start + 1, text);  // empty cell
    return text;
}

TableEdit editTable(const std::vector<std::string>& lines, size_t row, size_t column, TableOp op) {
    TableEdit result;
    if (lines.empty() || row >= lines.size()) {
        result.error = "No table found.";
        return result;
    }
    size_t indentLen = 0;
    while (indentLen < lines[0].size() && isSpace(lines[0][indentLen])) ++indentLen;
    const std::string indent = lines[0].substr(0, indentLen);

    std::vector<std::vector<std::string>> rows;
    size_t columns = 0;
    for (const auto& l : lines) {
        rows.push_back(splitRow(l));
        columns = std::max(columns, rows.back().size());
    }
    for (auto& r : rows) r.resize(columns);
    column = std::min(column, columns - 1);

    const size_t delimiterRow = findDelimiterRow(rows);
    const size_t firstBody = delimiterRow < rows.size() ? delimiterRow + 1 : 0;
    const bool inHead = delimiterRow < rows.size() && row <= delimiterRow;

    size_t newRow = row, newColumn = column;
    switch (op) {
        case TableOp::RowAbove:
            if (row < firstBody && delimiterRow < rows.size() && row == 0) {
                result.error = "A row cannot be inserted above the table header.";
                return result;
            }
            newRow = std::max(row, firstBody);
            rows.insert(rows.begin() + static_cast<std::ptrdiff_t>(newRow), std::vector<std::string>(columns));
            break;
        case TableOp::RowBelow:
            newRow = std::max(row + 1, firstBody);
            rows.insert(rows.begin() + static_cast<std::ptrdiff_t>(newRow), std::vector<std::string>(columns));
            break;
        case TableOp::DeleteRow:
            if (inHead) {
                result.error = "The header and delimiter rows cannot be deleted.";
                return result;
            }
            if (rows.size() == 1) {
                result.error = "The last row of the table cannot be deleted.";
                return result;
            }
            rows.erase(rows.begin() + static_cast<std::ptrdiff_t>(row));
            newRow = std::min(row, rows.size() - 1);
            if (delimiterRow < rows.size() && newRow <= delimiterRow) newRow = delimiterRow + 1 < rows.size() ? delimiterRow + 1 : 0;
            break;
        case TableOp::ColumnLeft:
        case TableOp::ColumnRight: {
            newColumn = op == TableOp::ColumnLeft ? column : column + 1;
            for (size_t r = 0; r < rows.size(); ++r) {
                rows[r].insert(rows[r].begin() + static_cast<std::ptrdiff_t>(newColumn), r == delimiterRow ? "---" : "");
            }
            break;
        }
        case TableOp::DeleteColumn:
            if (columns == 1) {
                result.error = "The last column of the table cannot be deleted.";
                return result;
            }
            for (auto& r : rows) r.erase(r.begin() + static_cast<std::ptrdiff_t>(column));
            newColumn = std::min(column, columns - 2);
            break;
    }

    std::vector<std::string> raw;
    raw.reserve(rows.size());
    for (const auto& r : rows) {
        std::string line = indent + "|";
        for (const auto& cell : r) line += " " + cell + " |";
        raw.push_back(line);
    }
    result.lines = formatTable(raw);
    result.row = newRow;
    result.column = newColumn;
    result.ok = true;
    return result;
}

// =================================================================================================
// table of contents
// =================================================================================================

namespace {

// NOTE: mirrored in web/src/slug.js - keep both implementations identical.
char32_t lowerCodePoint(char32_t c) {
    if (c >= 0x41 && c <= 0x5A) return c + 0x20;
    if (c >= 0xC0 && c <= 0xDE && c != 0xD7) return c + 0x20;
    if ((c >= 0x100 && c <= 0x137) || (c >= 0x14A && c <= 0x177)) return c % 2 == 0 ? c + 1 : c;
    if ((c >= 0x139 && c <= 0x148) || (c >= 0x179 && c <= 0x17E)) return c % 2 == 1 ? c + 1 : c;
    if (c == 0x178) return 0xFF;
    if (c >= 0x391 && c <= 0x3A9 && c != 0x3A2) return c + 0x20;
    if (c >= 0x410 && c <= 0x42F) return c + 0x20;
    if (c >= 0x400 && c <= 0x40F) return c + 0x50;
    return c;
}

bool isDropped(char32_t c) {
    if (c < 0x80) {
        const bool keep = (c >= 0x30 && c <= 0x39) || (c >= 0x61 && c <= 0x7A) || c == 0x5F || c == 0x2D || c == 0x20;
        return !keep;
    }
    if (c <= 0xBF || c == 0xD7 || c == 0xF7) return true;
    if (c >= 0x2000 && c <= 0x2BFF) return true;
    if (c >= 0x3000 && c <= 0x303F) return true;
    if (c >= 0xFE00 && c <= 0xFE0F) return true;
    if (c >= 0x1F000 && c <= 0x1FAFF) return true;
    return false;
}

struct Heading {
    int level;
    std::string text;
};

bool parseAtxHeading(const std::string& line, Heading& h) {
    size_t i = 0;
    while (i < line.size() && i < 3 && line[i] == ' ') ++i;
    size_t j = i;
    while (j < line.size() && line[j] == '#') ++j;
    const size_t level = j - i;
    if (level < 1 || level > 6) return false;
    if (j < line.size() && !isSpace(line[j])) return false;
    std::string text = trim(line.substr(j));
    // optional closing sequence: "## Title ##"
    size_t end = text.size();
    while (end > 0 && text[end - 1] == '#') --end;
    if (end < text.size() && (end == 0 || isSpace(text[end - 1]))) text = trim(text.substr(0, end));
    h.level = static_cast<int>(level);
    h.text = text;
    return true;
}

bool isFence(const std::string& line, char& fenceChar, size_t& fenceLen) {
    size_t i = 0;
    while (i < line.size() && i < 3 && line[i] == ' ') ++i;
    if (i >= line.size() || (line[i] != '`' && line[i] != '~')) return false;
    size_t j = i;
    while (j < line.size() && line[j] == line[i]) ++j;
    if (j - i < 3) return false;
    fenceChar = line[i];
    fenceLen = j - i;
    return true;
}

bool isSetextUnderline(const std::string& line, char ch) {
    const std::string t = trim(line);
    return !t.empty() && std::all_of(t.begin(), t.end(), [&](char c) { return c == ch; });
}

}  // namespace

std::string slugify(const std::string& text) {
    const std::string t = trim(text);
    std::string out;
    for (size_t i = 0; i < t.size();) {
        const char32_t c = lowerCodePoint(decodeUtf8(t, i));
        if (isDropped(c)) continue;
        appendUtf8(out, c == 0x20 ? U'-' : c);
    }
    return out;
}

std::string headingPlainText(const std::string& s) {
    std::string out;
    const size_t n = s.size();
    for (size_t i = 0; i < n; ++i) {
        const char c = s[i];
        if (c == '\\' && i + 1 < n && !isAlnum(s[i + 1]) && !isSpace(s[i + 1])) {
            out += s[++i];
        } else if (c == '!' && i + 1 < n && s[i + 1] == '[') {
            // image: drop completely
            const size_t close = s.find(']', i + 2);
            if (close == std::string::npos) { out += c; continue; }
            i = close;
            if (i + 1 < n && s[i + 1] == '(') {
                const size_t paren = s.find(')', i + 2);
                if (paren != std::string::npos) i = paren;
            }
        } else if (c == '[') {
            // link: keep the text only
            const size_t close = s.find(']', i + 1);
            const bool isLink = close != std::string::npos && close + 1 < n && (s[close + 1] == '(' || s[close + 1] == '[');
            if (!isLink) { out += c; continue; }
            out += headingPlainText(s.substr(i + 1, close - i - 1));
            i = close;
            const size_t end = s.find(s[i + 1] == '(' ? ')' : ']', i + 2);
            if (end != std::string::npos) i = end;
        } else if (c == '<') {
            const size_t close = s.find('>', i + 1);
            if (close == std::string::npos) out += c;
            else i = close;
        } else if (c == '`' || c == '*') {
            continue;
        } else if ((c == '~' || c == '=') && i + 1 < n && s[i + 1] == c) {
            ++i;
        } else if (c == '_') {
            const bool intraWord = i > 0 && i + 1 < n && isAlnum(s[i - 1]) && isAlnum(s[i + 1]);
            if (intraWord) out += c;
        } else {
            out += c;
        }
    }
    return trim(out);
}

std::vector<std::string> buildToc(const std::vector<std::string>& lines) {
    std::vector<Heading> headings;
    bool inFence = false, inToc = false;
    char fenceChar = 0;
    size_t fenceLen = 0;

    size_t start = 0;
    if (!lines.empty() && trim(lines[0]) == "---") {  // front matter
        for (size_t i = 1; i < lines.size(); ++i) {
            const std::string t = trim(lines[i]);
            if (t == "---" || t == "...") { start = i + 1; break; }
        }
    }

    for (size_t i = start; i < lines.size(); ++i) {
        const std::string& line = lines[i];
        char fc = 0;
        size_t fl = 0;
        if (isFence(line, fc, fl)) {
            if (!inFence) { inFence = true; fenceChar = fc; fenceLen = fl; }
            else if (fc == fenceChar && fl >= fenceLen) inFence = false;
            continue;
        }
        if (inFence) continue;
        const std::string t = trim(line);
        if (t == kTocBegin) { inToc = true; continue; }
        if (t == kTocEnd) { inToc = false; continue; }
        if (inToc) continue;

        Heading h{};
        if (parseAtxHeading(line, h)) {
            if (!h.text.empty()) headings.push_back(h);
            continue;
        }
        // setext heading: text line followed by === or ---
        if (i + 1 < lines.size() && !t.empty() && (i == start || isBlank(lines[i - 1]))) {
            const LineParts p = parseLine(line);
            if (p.marker.empty() && p.quote.empty() && p.indent.size() < 4 && t.find('|') == std::string::npos) {
                if (isSetextUnderline(lines[i + 1], '=')) { headings.push_back({1, t}); ++i; }
                else if (isSetextUnderline(lines[i + 1], '-') && trim(lines[i + 1]).size() >= 2) { headings.push_back({2, t}); ++i; }
            }
        }
    }

    // slugs are numbered across all headings, exactly like the preview does
    std::map<std::string, int> seen;
    std::vector<std::string> slugs;
    for (auto& h : headings) {
        h.text = headingPlainText(h.text);
        const std::string base = slugify(h.text);
        const int count = seen[base]++;
        slugs.push_back(count == 0 ? base : base + "-" + std::to_string(count));
    }

    // a single leading H1 is the document title: leave it out
    size_t first = 0;
    const auto h1Count = std::count_if(headings.begin(), headings.end(), [](const Heading& h) { return h.level == 1; });
    if (headings.size() > 1 && h1Count == 1 && headings[0].level == 1) first = 1;

    int minLevel = 6;
    for (size_t i = first; i < headings.size(); ++i) minLevel = std::min(minLevel, headings[i].level);

    std::vector<std::string> out;
    for (size_t i = first; i < headings.size(); ++i) {
        std::string text;
        for (char c : headings[i].text) {
            if (c == '[' || c == ']') text += '\\';
            text += c;
        }
        out.push_back(std::string(static_cast<size_t>(headings[i].level - minLevel) * 2, ' ') + "- [" + text + "](#" + slugs[i] + ")");
    }
    return out;
}

}  // namespace md
