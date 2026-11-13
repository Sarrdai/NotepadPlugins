#include "commands.h"

#include <windows.h>
#include <commdlg.h>
#include <shellapi.h>

#include <algorithm>
#include <functional>
#include <string>
#include <vector>

// GDI+ headers rely on the min/max macros that NOMINMAX removes.
namespace Gdiplus {
using std::max;
using std::min;
}  // namespace Gdiplus
#include <gdiplus.h>

#include "editor.h"
#include "md_text.h"
#include "npp_api.h"
#include "plugin.h"
#include "util.h"

namespace commands {

namespace {

using Lines = std::vector<std::string>;

struct LineRange {
    intptr_t first;
    intptr_t last;
};

// Lines touched by the selection (a selection ending at a line start does not include that line).
LineRange selectedLines(const Editor& ed) {
    const intptr_t start = ed.selectionStart();
    const intptr_t end = ed.selectionEnd();
    LineRange r{ed.lineFromPosition(start), ed.lineFromPosition(end)};
    if (r.last > r.first && end == ed.lineStart(r.last)) --r.last;
    return r;
}

Lines readLines(const Editor& ed, intptr_t first, intptr_t last) {
    Lines lines;
    for (intptr_t l = first; l <= last; ++l) lines.push_back(ed.line(l));
    return lines;
}

// Applies `fn` to the selected lines and keeps a sensible caret / selection.
void transformLines(const std::function<Lines(const Lines&)>& fn) {
    const Editor ed = Editor::current();
    if (ed.readOnly()) return;
    const LineRange r = selectedLines(ed);
    const Lines before = readLines(ed, r.first, r.last);
    const Lines after = fn(before);
    if (after == before) return;

    const bool hadSelection = ed.selectionStart() != ed.selectionEnd();
    const intptr_t start = ed.lineStart(r.first);
    const intptr_t end = ed.lineEnd(r.last);
    const intptr_t caretFromEnd = end - ed.caret();
    const std::string text = md::joinLines(after, ed.eol());

    UndoGroup undo(ed);
    ed.replaceRange(start, end, text);
    const intptr_t newEnd = start + static_cast<intptr_t>(text.size());
    if (hadSelection) {
        ed.select(start, newEnd);
    } else {
        const intptr_t caret = std::max(start, newEnd - std::max<intptr_t>(0, caretFromEnd));
        ed.select(caret, caret);
    }
}

void wrapSelection(const std::string& marker) {
    const Editor ed = Editor::current();
    if (ed.readOnly()) return;
    intptr_t a = ed.selectionStart();
    intptr_t b = ed.selectionEnd();
    if (a == b) {
        // no selection: use the word under the caret
        const intptr_t ws = ed.call(SCI_WORDSTARTPOSITION, static_cast<uintptr_t>(a), 1);
        const intptr_t we = ed.call(SCI_WORDENDPOSITION, static_cast<uintptr_t>(a), 1);
        if (ws < we) {
            a = ws;
            b = we;
        }
    }
    const intptr_t context = 3;
    const std::string before = ed.range(std::max<intptr_t>(0, a - context), a);
    const std::string after = ed.range(b, std::min(ed.length(), b + context));
    const md::WrapResult r = md::toggleWrap(ed.range(a, b), before, after, marker);

    UndoGroup undo(ed);
    const intptr_t base = a - static_cast<intptr_t>(r.eraseBefore);
    ed.replaceRange(base, b + static_cast<intptr_t>(r.eraseAfter), r.text);
    ed.select(base + static_cast<intptr_t>(r.selStart), base + static_cast<intptr_t>(r.selEnd));
}

bool looksLikeUrl(const std::wstring& s) {
    if (s.empty() || s.find_first_of(L" \t\r\n") != std::wstring::npos) return false;
    return startsWithNoCase(s, L"http://") || startsWithNoCase(s, L"https://") || startsWithNoCase(s, L"ftp://") ||
           startsWithNoCase(s, L"mailto:") || startsWithNoCase(s, L"www.");
}

std::wstring trimWide(const std::wstring& s) {
    const size_t a = s.find_first_not_of(L" \t\r\n");
    if (a == std::wstring::npos) return {};
    const size_t b = s.find_last_not_of(L" \t\r\n");
    return s.substr(a, b - a + 1);
}

std::wstring clipboardText() {
    std::wstring text;
    if (!IsClipboardFormatAvailable(CF_UNICODETEXT) || !OpenClipboard(g_npp.nppHandle)) return text;
    if (const HANDLE data = GetClipboardData(CF_UNICODETEXT)) {
        if (const auto* chars = static_cast<const wchar_t*>(GlobalLock(data))) {
            text = chars;
            GlobalUnlock(data);
        }
    }
    CloseClipboard();
    return text;
}

bool isImageExtension(const std::wstring& ext) {
    static const wchar_t* const known[] = {L"png", L"jpg", L"jpeg", L"gif", L"svg", L"webp", L"bmp", L"ico", L"avif"};
    return std::any_of(std::begin(known), std::end(known), [&](const wchar_t* e) { return ext == e; });
}

// Inserts "![alt](path)" for an image file, relative to the current document if possible.
void insertImageLink(const Editor& ed, const std::wstring& imagePath) {
    const std::wstring document = currentFilePath();
    const std::wstring baseDir = isAbsolutePath(document) ? directoryOf(document) : std::wstring();
    const UINT cp = ed.codePage();
    const intptr_t a = ed.selectionStart();
    const intptr_t b = ed.selectionEnd();
    std::string alt = ed.range(a, b);
    if (alt.empty() || alt.find_first_of("\r\n") != std::string::npos) alt = toBytes(fileStemOf(imagePath), cp);
    const std::string text = "![" + alt + "](" + toBytes(relativeUrlPath(baseDir, imagePath), cp) + ")";
    UndoGroup undo(ed);
    ed.replaceRange(a, b, text);
    ed.select(a + 2, a + 2 + static_cast<intptr_t>(alt.size()));
}

// Makes sure block level text starts on its own line; returns the insert position.
intptr_t blockInsertPosition(const Editor& ed, std::string& prefix) {
    const intptr_t line = ed.lineFromPosition(ed.caret());
    const std::string eol = ed.eol();
    if (ed.line(line).find_first_not_of(" \t") == std::string::npos) {
        prefix.clear();
        return ed.lineStart(line);
    }
    prefix = eol + eol;
    return ed.lineEnd(line);
}

bool saveClipboardBitmapAsPng(const std::wstring& path) {
    bool ok = false;
    if (!OpenClipboard(g_npp.nppHandle)) return false;
    if (const auto bitmap = static_cast<HBITMAP>(GetClipboardData(CF_BITMAP))) {
        Gdiplus::GdiplusStartupInput input;
        ULONG_PTR token = 0;
        if (Gdiplus::GdiplusStartup(&token, &input, nullptr) == Gdiplus::Ok) {
            {
                Gdiplus::Bitmap image(bitmap, static_cast<HPALETTE>(nullptr));
                // CLSID of the built-in GDI+ PNG encoder
                static const CLSID pngEncoder = {
                    0x557CF406, 0x1A04, 0x11D3, {0x9A, 0x73, 0x00, 0x00, 0xF8, 0x1E, 0xF3, 0x2E}};
                ok = image.GetLastStatus() == Gdiplus::Ok &&
                     image.Save(path.c_str(), &pngEncoder, nullptr) == Gdiplus::Ok;
            }
            Gdiplus::GdiplusShutdown(token);
        }
    }
    CloseClipboard();
    return ok;
}

std::wstring clipboardImageFile() {
    std::wstring path;
    if (!IsClipboardFormatAvailable(CF_HDROP) || !OpenClipboard(g_npp.nppHandle)) return path;
    if (const auto drop = static_cast<HDROP>(GetClipboardData(CF_HDROP))) {
        wchar_t buffer[MAX_PATH] = {0};
        if (DragQueryFileW(drop, 0, buffer, MAX_PATH) > 0 && isImageExtension(extensionOf(buffer))) path = buffer;
    }
    CloseClipboard();
    return path;
}

}  // namespace

void bold() {
    wrapSelection("**");
}
void italic() {
    wrapSelection("*");
}
void strikethrough() {
    wrapSelection("~~");
}
void inlineCode() {
    wrapSelection("`");
}

void codeBlock() {
    const Editor ed = Editor::current();
    if (ed.readOnly()) return;
    const std::string eol = ed.eol();
    UndoGroup undo(ed);
    if (ed.selectionStart() == ed.selectionEnd()) {
        std::string prefix;
        const intptr_t pos = blockInsertPosition(ed, prefix);
        ed.replaceRange(pos, pos, prefix + "```" + eol + eol + "```");
        const intptr_t caret =
            pos + static_cast<intptr_t>(prefix.size()) + 3;  // behind the opening fence: type the language
        ed.select(caret, caret);
        return;
    }
    const LineRange r = selectedLines(ed);
    const intptr_t start = ed.lineStart(r.first);
    const intptr_t end = ed.lineEnd(r.last);
    const std::string body = ed.range(start, end);
    ed.replaceRange(start, end, "```" + eol + body + eol + "```");
    ed.select(start + 3, start + 3);
}

void headingMore() {
    transformLines([](const Lines& l) { return md::changeHeading(l, 1); });
}
void headingLess() {
    transformLines([](const Lines& l) { return md::changeHeading(l, -1); });
}
void bulletList() {
    transformLines([](const Lines& l) { return md::toggleLinePrefix(l, md::LinePrefix::Bullet); });
}
void numberedList() {
    transformLines([](const Lines& l) { return md::toggleLinePrefix(l, md::LinePrefix::Ordered); });
}
void quote() {
    transformLines([](const Lines& l) { return md::toggleLinePrefix(l, md::LinePrefix::Quote); });
}
void task() {
    transformLines([](const Lines& lines) {
        Lines out;
        for (const auto& l : lines) {
            const bool blank = l.find_first_not_of(" \t") == std::string::npos;
            out.push_back(blank && lines.size() > 1 ? l : md::toggleTask(l));
        }
        return out;
    });
}

void toggleTaskAtLine(const Editor& ed, intptr_t line) {
    if (ed.readOnly() || line < 0 || line >= ed.lineCount()) return;
    const std::string text = ed.line(line);
    if (!md::isTaskLine(text)) return;
    ed.replaceRange(ed.lineStart(line), ed.lineEnd(line), md::toggleTask(text));
}

void link() {
    const Editor ed = Editor::current();
    if (ed.readOnly()) return;
    const UINT cp = ed.codePage();
    const intptr_t a = ed.selectionStart();
    const intptr_t b = ed.selectionEnd();
    const std::string selection = ed.range(a, b);
    const std::wstring clip = trimWide(clipboardText());
    const std::string clipUrl = looksLikeUrl(clip) ? toBytes(clip, cp) : std::string();

    std::string text;
    intptr_t selStart = 0, selEnd = 0;  // relative to `a`
    if (looksLikeUrl(toWide(selection, cp))) {
        // the selection is the target: ask for the link text
        text = "[](" + selection + ")";
        selStart = selEnd = 1;
    } else if (!selection.empty()) {
        if (clipUrl.empty()) {
            text = "[" + selection + "](url)";
            selStart = static_cast<intptr_t>(selection.size()) + 3;  // select the "url" placeholder
            selEnd = selStart + 3;
        } else {
            text = "[" + selection + "](" + clipUrl + ")";
            selStart = selEnd = static_cast<intptr_t>(text.size());
        }
    } else if (!clipUrl.empty()) {
        text = "[](" + clipUrl + ")";
        selStart = selEnd = 1;
    } else {
        text = "[text](url)";
        selStart = 1;
        selEnd = 5;
    }
    UndoGroup undo(ed);
    ed.replaceRange(a, b, text);
    ed.select(a + selStart, a + selEnd);
}

void image() {
    const Editor ed = Editor::current();
    if (ed.readOnly()) return;
    wchar_t file[MAX_PATH] = {0};
    const std::wstring document = currentFilePath();
    const std::wstring initialDir = isAbsolutePath(document) ? directoryOf(document) : std::wstring();
    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = g_npp.nppHandle;
    ofn.lpstrFilter = L"Images\0*.png;*.jpg;*.jpeg;*.gif;*.svg;*.webp;*.bmp;*.ico;*.avif\0All files\0*.*\0";
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrInitialDir = initialDir.empty() ? nullptr : initialDir.c_str();
    ofn.lpstrTitle = L"Insert Image";
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY | OFN_NOCHANGEDIR;
    if (!GetOpenFileNameW(&ofn)) return;
    insertImageLink(ed, file);
}

void pasteImage() {
    const Editor ed = Editor::current();
    if (ed.readOnly()) return;

    // an image file copied in Explorer: just link it
    const std::wstring copiedFile = clipboardImageFile();
    if (!copiedFile.empty()) {
        insertImageLink(ed, copiedFile);
        return;
    }
    if (!IsClipboardFormatAvailable(CF_BITMAP)) {
        showMessage(L"The clipboard does not contain an image.");
        return;
    }
    const std::wstring document = currentFilePath();
    if (!isAbsolutePath(document)) {
        showMessage(L"Please save the document first.\n\nThe image is stored in an \"images\" folder next to it.");
        return;
    }
    const std::wstring folder = directoryOf(document) + L"\\images";
    CreateDirectoryW(folder.c_str(), nullptr);
    SYSTEMTIME now;
    GetLocalTime(&now);
    wchar_t stamp[32];
    wsprintfW(stamp, L"%04d%02d%02d-%02d%02d%02d", now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute,
              now.wSecond);
    const std::wstring target = folder + L"\\" + fileStemOf(document) + L"-" + stamp + L".png";
    if (!saveClipboardBitmapAsPng(target)) {
        showMessage(L"The image could not be saved to\n" + target, MB_ICONWARNING);
        return;
    }
    insertImageLink(ed, target);
}

void insertTable() {
    const Editor ed = Editor::current();
    if (ed.readOnly()) return;
    const std::string eol = ed.eol();
    std::string prefix;
    UndoGroup undo(ed);
    const intptr_t pos = blockInsertPosition(ed, prefix);
    ed.replaceRange(pos, pos, prefix + md::newTable(3, 2, eol) + eol);
    const intptr_t headerStart = pos + static_cast<intptr_t>(prefix.size()) + 2;
    ed.select(headerStart, headerStart + 8);  // "Column 1"
}

namespace {

struct TableBlock {
    intptr_t caretLine, first, last;
};

// Finds the table around the caret; shows a hint and returns false if there is none.
bool findTable(const Editor& ed, TableBlock& block) {
    block.caretLine = ed.lineFromPosition(ed.caret());
    if (!md::isTableLine(ed.line(block.caretLine))) {
        showMessage(L"Place the caret inside a Markdown table.");
        return false;
    }
    block.first = block.last = block.caretLine;
    while (block.first > 0 && md::isTableLine(ed.line(block.first - 1))) --block.first;
    while (block.last + 1 < ed.lineCount() && md::isTableLine(ed.line(block.last + 1))) ++block.last;
    return true;
}

void applyTableEdit(md::TableOp op) {
    const Editor ed = Editor::current();
    if (ed.readOnly()) return;
    TableBlock block;
    if (!findTable(ed, block)) return;

    const Lines before = readLines(ed, block.first, block.last);
    const intptr_t caretOffset = ed.caret() - ed.lineStart(block.caretLine);
    const md::TableEdit edit =
        md::editTable(before, static_cast<size_t>(block.caretLine - block.first),
                      md::tableColumnAt(before[block.caretLine - block.first], static_cast<size_t>(caretOffset)), op);
    if (!edit.ok) {
        showMessage(toWide(edit.error));
        return;
    }
    UndoGroup undo(ed);
    const intptr_t start = ed.lineStart(block.first);
    ed.replaceRange(start, ed.lineEnd(block.last), md::joinLines(edit.lines, ed.eol()));
    // caret into the affected cell
    intptr_t caret = start;
    for (size_t i = 0; i < edit.row; ++i) caret += static_cast<intptr_t>(edit.lines[i].size() + ed.eol().size());
    caret += static_cast<intptr_t>(md::tableCellOffset(edit.lines[edit.row], edit.column));
    ed.select(caret, caret);
}

}  // namespace

void formatTable() {
    const Editor ed = Editor::current();
    if (ed.readOnly()) return;
    TableBlock block;
    if (!findTable(ed, block)) return;

    const Lines before = readLines(ed, block.first, block.last);
    const Lines after = md::formatTable(before);
    if (after == before) return;
    const intptr_t start = ed.lineStart(block.first);
    UndoGroup undo(ed);
    ed.replaceRange(start, ed.lineEnd(block.last), md::joinLines(after, ed.eol()));
    const intptr_t caret = ed.lineEnd(block.caretLine);
    ed.select(caret, caret);
}

void tableRowAbove() {
    applyTableEdit(md::TableOp::RowAbove);
}
void tableRowBelow() {
    applyTableEdit(md::TableOp::RowBelow);
}
void tableDeleteRow() {
    applyTableEdit(md::TableOp::DeleteRow);
}
void tableColumnLeft() {
    applyTableEdit(md::TableOp::ColumnLeft);
}
void tableColumnRight() {
    applyTableEdit(md::TableOp::ColumnRight);
}
void tableDeleteColumn() {
    applyTableEdit(md::TableOp::DeleteColumn);
}

void tableOfContents() {
    const Editor ed = Editor::current();
    if (ed.readOnly()) return;
    const Lines lines = md::splitLines(ed.text());
    const Lines toc = md::buildToc(lines);
    if (toc.empty()) {
        showMessage(L"No headings found.");
        return;
    }
    auto isMarker = [](const std::string& line, const char* marker) {
        const size_t a = line.find_first_not_of(" \t");
        const size_t b = line.find_last_not_of(" \t");
        return a != std::string::npos && line.substr(a, b - a + 1) == marker;
    };
    intptr_t begin = -1, end = -1;
    for (size_t i = 0; i < lines.size(); ++i) {
        if (begin < 0 && isMarker(lines[i], md::kTocBegin))
            begin = static_cast<intptr_t>(i);
        else if (begin >= 0 && isMarker(lines[i], md::kTocEnd)) {
            end = static_cast<intptr_t>(i);
            break;
        }
    }
    const std::string eol = ed.eol();
    const std::string body = md::joinLines(toc, eol) + eol;
    UndoGroup undo(ed);
    if (begin >= 0 && end > begin) {
        // update the existing table of contents
        ed.replaceRange(ed.lineStart(begin + 1), ed.lineStart(end), body);
        return;
    }
    std::string prefix;
    const intptr_t pos = blockInsertPosition(ed, prefix);
    ed.replaceRange(pos, pos, prefix + md::kTocBegin + eol + body + md::kTocEnd + eol);
}

void smartEnter() {
    const Editor ed = Editor::current();
    if (ed.readOnly() || ed.selectionStart() != ed.selectionEnd() || ed.call(SCI_GETSELECTIONS) > 1) return;
    const intptr_t caret = ed.caret();
    const intptr_t line = ed.lineFromPosition(caret);
    if (line < 1) return;

    const std::string current = ed.line(line);
    size_t indent = current.find_first_not_of(" \t");
    if (indent == std::string::npos) indent = current.size();
    const intptr_t start = ed.lineStart(line);
    if (caret > start + static_cast<intptr_t>(indent)) return;  // caret is not at the start of the new line

    const md::EnterResult r = md::continueList(ed.line(line - 1), indent == current.size());
    if (!r.handled) return;

    UndoGroup undo(ed);
    if (r.clearPrevious) {
        // empty list item: remove the marker and stay on that line
        const intptr_t previousStart = ed.lineStart(line - 1);
        ed.replaceRange(previousStart, start + static_cast<intptr_t>(indent), "");
        ed.select(previousStart, previousStart);
    } else {
        ed.replaceRange(start, start + static_cast<intptr_t>(indent), r.prefix);
        const intptr_t pos = start + static_cast<intptr_t>(r.prefix.size());
        ed.select(pos, pos);
    }
}

}  // namespace commands
