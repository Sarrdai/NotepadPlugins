// Unit tests for the pure text transformations (runs on any platform).
#include "../src/code_syntax.h"
#include "../src/md_text.h"

#include <cstdio>
#include <string>
#include <vector>

static int g_failed = 0;
static int g_total = 0;

static std::string show(const std::string& s) { return "\"" + s + "\""; }
static std::string show(const std::vector<std::string>& v) {
    std::string out = "[";
    for (size_t i = 0; i < v.size(); ++i) out += (i ? ", " : "") + show(v[i]);
    return out + "]";
}
static std::string show(bool b) { return b ? "true" : "false"; }
static std::string show(size_t n) { return std::to_string(n); }
static std::string show(int n) { return std::to_string(n); }
static std::string show(uint32_t n) { return std::to_string(n); }

template <typename T, typename U>
static void expectEq(const T& actual, const U& expected, const char* expr, int line) {
    ++g_total;
    const T exp(expected);
    if (actual == exp) return;
    ++g_failed;
    std::printf("FAIL line %d: %s\n  actual:   %s\n  expected: %s\n", line, expr, show(actual).c_str(), show(exp).c_str());
}
#define EXPECT_EQ(actual, expected) expectEq((actual), (expected), #actual, __LINE__)

using Lines = std::vector<std::string>;
using namespace md;

static std::string applyWrap(const std::string& before, const std::string& sel, const std::string& after, const std::string& marker) {
    const WrapResult r = toggleWrap(sel, before, after, marker);
    return before.substr(0, before.size() - r.eraseBefore) + "<" + r.text.substr(0, r.selStart) + "|" +
           r.text.substr(r.selStart, r.selEnd - r.selStart) + "|" + r.text.substr(r.selEnd) + ">" + after.substr(r.eraseAfter);
}

int main() {
    // ---- splitLines / joinLines
    EXPECT_EQ(splitLines("a\r\nb\nc\rd"), (Lines{"a", "b", "c", "d"}));
    EXPECT_EQ(splitLines("a\n"), (Lines{"a", ""}));
    EXPECT_EQ(joinLines({"a", "b"}, "\r\n"), "a\r\nb");

    // ---- toggleWrap
    EXPECT_EQ(applyWrap("a ", "word", " b", "**"), "a <**|word|**> b");
    EXPECT_EQ(applyWrap("a **", "word", "** b", "**"), "a <|word|> b");
    EXPECT_EQ(applyWrap("a ", "**word**", " b", "**"), "a <|word|> b");
    EXPECT_EQ(applyWrap("", "", "", "**"), "<**||**>");
    EXPECT_EQ(applyWrap("x**", "", "**y", "**"), "x<||>y");
    EXPECT_EQ(applyWrap("", " word ", "", "*"), "< *|word|* >");
    // italic must not strip bold markers
    EXPECT_EQ(applyWrap("**", "word", "**", "*"), "**<*|word|*>**");
    EXPECT_EQ(applyWrap("", "**word**", "", "*"), "<*|**word**|*>");
    EXPECT_EQ(applyWrap("***", "word", "***", "*"), "**<|word|>**");
    EXPECT_EQ(applyWrap("***", "word", "***", "**"), "*<|word|>*");
    EXPECT_EQ(applyWrap("", "code", "", "`"), "<`|code|`>");
    EXPECT_EQ(applyWrap("`", "code", "`", "`"), "<|code|>");
    EXPECT_EQ(applyWrap("", "~~gone~~", "", "~~"), "<|gone|>");
    EXPECT_EQ(applyWrap("", "****", "", "**"), "<**|****|**>");

    // ---- toggleLinePrefix
    EXPECT_EQ(toggleLinePrefix({"a", "", "  b"}, LinePrefix::Bullet), (Lines{"- a", "", "  - b"}));
    EXPECT_EQ(toggleLinePrefix({"- a", "* b"}, LinePrefix::Bullet), (Lines{"a", "b"}));
    EXPECT_EQ(toggleLinePrefix({"1. a", "2. b"}, LinePrefix::Bullet), (Lines{"- a", "- b"}));
    EXPECT_EQ(toggleLinePrefix({"- a", "- [x] b", "c"}, LinePrefix::Ordered), (Lines{"1. a", "2. [x] b", "3. c"}));
    EXPECT_EQ(toggleLinePrefix({"1. a", "7) b"}, LinePrefix::Ordered), (Lines{"a", "b"}));
    EXPECT_EQ(toggleLinePrefix({"a", "", "- b"}, LinePrefix::Quote), (Lines{"> a", ">", "> - b"}));
    EXPECT_EQ(toggleLinePrefix({"> a", ">", "> > b"}, LinePrefix::Quote), (Lines{"a", "", "> b"}));
    EXPECT_EQ(toggleLinePrefix({""}, LinePrefix::Bullet), (Lines{"- "}));
    EXPECT_EQ(toggleLinePrefix({""}, LinePrefix::Quote), (Lines{"> "}));
    EXPECT_EQ(toggleLinePrefix({"**bold** text"}, LinePrefix::Bullet), (Lines{"- **bold** text"}));

    // ---- tasks
    EXPECT_EQ(toggleTask("buy milk"), "- [ ] buy milk");
    EXPECT_EQ(toggleTask("  - buy milk"), "  - [ ] buy milk");
    EXPECT_EQ(toggleTask("- [ ] buy milk"), "- [x] buy milk");
    EXPECT_EQ(toggleTask("* [X] buy milk"), "* [ ] buy milk");
    EXPECT_EQ(toggleTask("> 1. [ ] quoted"), "> 1. [x] quoted");
    EXPECT_EQ(toggleTask("- [ ]"), "- [x]");
    EXPECT_EQ(isTaskLine("- [ ] a"), true);
    EXPECT_EQ(isTaskLine("- [link](x)"), false);
    EXPECT_EQ(isTaskLine("[ ] a"), false);

    // ---- headings
    EXPECT_EQ(changeHeading({"Title", "## Sub", "###### Deep", ""}, 1), (Lines{"# Title", "### Sub", "###### Deep", ""}));
    EXPECT_EQ(changeHeading({"Title", "# One", "## Two"}, -1), (Lines{"Title", "One", "# Two"}));
    EXPECT_EQ(changeHeading({"#hashtag"}, 1), (Lines{"# #hashtag"}));
    EXPECT_EQ(changeHeading({"#"}, 1), (Lines{"## "}));

    // ---- smart enter
    {
        EnterResult r = continueList("- item", true);
        EXPECT_EQ(r.handled, true);
        EXPECT_EQ(r.prefix, "- ");
        r = continueList("  * [x] done", true);
        EXPECT_EQ(r.prefix, "  * [ ] ");
        r = continueList("9. nine", true);
        EXPECT_EQ(r.prefix, "10. ");
        r = continueList("1) one", false);
        EXPECT_EQ(r.prefix, "2) ");
        r = continueList("> quote", true);
        EXPECT_EQ(r.prefix, "> ");
        r = continueList(">quote", true);
        EXPECT_EQ(r.prefix, "> ");
        r = continueList("> - nested", true);
        EXPECT_EQ(r.prefix, "> - ");
        r = continueList("- ", true);
        EXPECT_EQ(r.handled, true);
        EXPECT_EQ(r.clearPrevious, true);
        r = continueList("- [ ] ", true);
        EXPECT_EQ(r.clearPrevious, true);
        r = continueList("> ", true);
        EXPECT_EQ(r.clearPrevious, true);
        r = continueList("- ", false);
        EXPECT_EQ(r.handled, false);
        EXPECT_EQ(continueList("plain text", true).handled, false);
        EXPECT_EQ(continueList("---", true).handled, false);
        EXPECT_EQ(continueList("* * *", true).handled, false);
        EXPECT_EQ(continueList("**bold** start", true).handled, false);
        EXPECT_EQ(continueList("2024. A year", true).prefix, "2025. ");
        EXPECT_EQ(continueList("", true).handled, false);
    }

    // ---- tables
    EXPECT_EQ(isTableLine("| a | b |"), true);
    EXPECT_EQ(isTableLine("no table"), false);
    EXPECT_EQ(formatTable({"|Name|Qty|Price|", "|:--|:-:|--:|", "|Apple|5|1.20|", "|Watermelon|12|"}),
              (Lines{"| Name       | Qty | Price |", "| :--------- | :-: | ----: |", "| Apple      |  5  |  1.20 |",
                     "| Watermelon | 12  |       |"}));
    EXPECT_EQ(formatTable({"  a | b", "  ---|---", "  `x|y` | 1 \\| 2"}),
              (Lines{"  | a     | b      |", "  | ----- | ------ |", "  | `x|y` | 1 \\| 2 |"}));
    EXPECT_EQ(formatTable({"| Größe | 日本 |", "|---|---|", "| a | b |"}),
              (Lines{"| Größe | 日本 |", "| ----- | ---- |", "| a     | b    |"}));
    EXPECT_EQ(formatTable(formatTable({"|a|b|", "|-|-|", "|1|2|"})), formatTable({"|a|b|", "|-|-|", "|1|2|"}));
    EXPECT_EQ(newTable(2, 1, "\n"), "| Column 1 | Column 2 |\n| -------- | -------- |\n|          |          |");
    // ---- table rows / columns
    {
        const Lines t = {"| A | B |", "| :-- | --: |", "| 1 | 2 |", "| 3 | 4 |"};
        EXPECT_EQ(tableColumnAt("| A | B |", 0), size_t{0});
        EXPECT_EQ(tableColumnAt("| A | B |", 3), size_t{0});
        EXPECT_EQ(tableColumnAt("| A | B |", 6), size_t{1});
        EXPECT_EQ(tableColumnAt("| A | B |", 9), size_t{1});  // behind the trailing pipe
        EXPECT_EQ(tableColumnAt("a | b", 4), size_t{1});       // no outer pipes
        EXPECT_EQ(tableColumnAt("| `x|y` | b |", 8), size_t{0});
        EXPECT_EQ(tableColumnAt("| a \\| b | c |", 8), size_t{0});
        EXPECT_EQ(tableCellOffset("| A   | B |", 0), size_t{2});
        EXPECT_EQ(tableCellOffset("| A   | B |", 1), size_t{8});
        EXPECT_EQ(tableCellOffset("|     | B |", 0), size_t{2});  // empty cell

        TableEdit e = editTable(t, 2, 0, TableOp::RowBelow);
        EXPECT_EQ(e.lines, (Lines{"| A   |   B |", "| :-- | --: |", "| 1   |   2 |", "|     |     |", "| 3   |   4 |"}));
        EXPECT_EQ(e.row, size_t{3});
        e = editTable(t, 2, 1, TableOp::RowAbove);
        EXPECT_EQ(e.lines[2], "|     |     |");
        EXPECT_EQ(e.row, size_t{2});
        e = editTable(t, 0, 0, TableOp::RowBelow);  // below the header: first body row
        EXPECT_EQ(e.lines[2], "|     |     |");
        EXPECT_EQ(e.row, size_t{2});
        e = editTable(t, 1, 0, TableOp::RowAbove);  // above the delimiter row: first body row
        EXPECT_EQ(e.lines[2], "|     |     |");
        EXPECT_EQ(editTable(t, 0, 0, TableOp::RowAbove).ok, false);
        e = editTable(t, 3, 1, TableOp::DeleteRow);
        EXPECT_EQ(e.lines, (Lines{"| A   |   B |", "| :-- | --: |", "| 1   |   2 |"}));
        EXPECT_EQ(e.row, size_t{2});
        EXPECT_EQ(editTable(t, 0, 0, TableOp::DeleteRow).ok, false);
        EXPECT_EQ(editTable(t, 1, 0, TableOp::DeleteRow).ok, false);
        e = editTable(editTable(t, 3, 0, TableOp::DeleteRow).lines, 2, 0, TableOp::DeleteRow);
        EXPECT_EQ(e.lines.size(), size_t{2});
        EXPECT_EQ(e.row, size_t{0});

        e = editTable(t, 2, 0, TableOp::ColumnRight);
        EXPECT_EQ(e.lines, (Lines{"| A   |     |   B |", "| :-- | --- | --: |", "| 1   |     |   2 |", "| 3   |     |   4 |"}));
        EXPECT_EQ(e.column, size_t{1});
        e = editTable(t, 2, 0, TableOp::ColumnLeft);
        EXPECT_EQ(e.lines[0], "|     | A   |   B |");
        EXPECT_EQ(e.column, size_t{0});
        e = editTable(t, 2, 1, TableOp::DeleteColumn);
        EXPECT_EQ(e.lines, (Lines{"| A   |", "| :-- |", "| 1   |", "| 3   |"}));
        EXPECT_EQ(e.column, size_t{0});
        EXPECT_EQ(editTable(e.lines, 0, 0, TableOp::DeleteColumn).ok, false);
        e = editTable({"  | A | B |", "  |---|---|", "  | 1 | 2 |"}, 2, 1, TableOp::ColumnRight);  // keeps indentation
        EXPECT_EQ(e.lines[0], "  | A   | B   |     |");
        e = editTable({"a | b", "1"}, 1, 0, TableOp::RowBelow);  // no delimiter row, ragged rows
        EXPECT_EQ(e.lines, (Lines{"| a   | b   |", "| 1   |     |", "|     |     |"}));
    }

    EXPECT_EQ(displayWidth("Größe"), size_t{5});
    EXPECT_EQ(displayWidth("日本"), size_t{4});

    // ---- slugs (must match web/src/slug.js)
    EXPECT_EQ(slugify("Hello World"), "hello-world");
    EXPECT_EQ(slugify("  What's new in v2.0?  "), "whats-new-in-v20");
    EXPECT_EQ(slugify("Größe & Übersicht"), "größe--übersicht");
    EXPECT_EQ(slugify("snake_case-name"), "snake_case-name");
    EXPECT_EQ(slugify("ÄÖÜ ÉÈ Ñ"), "äöü-éè-ñ");
    EXPECT_EQ(slugify("Привет Мир"), "привет-мир");
    EXPECT_EQ(slugify("Emoji 🚀 launch"), "emoji--launch");
    EXPECT_EQ(slugify("“Quotes” – dash"), "quotes--dash");
    EXPECT_EQ(slugify("日本語"), "日本語");

    EXPECT_EQ(headingPlainText("**Bold** and `code` and [link](http://x) ![img](y.png)"), "Bold and code and link");
    EXPECT_EQ(headingPlainText("snake_case _emph_ ~~del~~ 1 \\* 2"), "snake_case emph del 1 * 2");

    // ---- table of contents
    EXPECT_EQ(buildToc({"---", "title: x", "---", "# Title", "", "## Intro", "```", "# not a heading", "```", "### Deep `code`",
                        "## Intro", "<!-- TOC -->", "- [old](#old)", "<!-- /TOC -->", "## Closed ##", "", "Setext", "------"}),
              (Lines{"- [Intro](#intro)", "  - [Deep code](#deep-code)", "- [Intro](#intro-1)", "- [Closed](#closed)", "- [Setext](#setext)"}));
    EXPECT_EQ(buildToc({"# A", "# B", "## C [x]"}), (Lines{"- [A](#a)", "- [B](#b)", "  - [C \\[x\\]](#c-x)"}));
    EXPECT_EQ(buildToc({"text only"}), (Lines{}));
    EXPECT_EQ(buildToc({"### Only deep", "#### Deeper"}), (Lines{"- [Only deep](#only-deep)", "  - [Deeper](#deeper)"}));

    // ---- code blocks (editor highlighting)
    {
        const std::string doc = "# T\r\n```Python title=x\r\nprint(1)\r\n```\r\ntext\n  ~~~~ {.c++}\n```\nint x;\n~~~~~\n````\nopen";
        const auto blocks = syntax::findCodeBlocks(doc);
        EXPECT_EQ(blocks.size(), size_t{3});
        EXPECT_EQ(blocks[0].language, "python");
        EXPECT_EQ(doc.substr(blocks[0].bodyStart, blocks[0].bodyEnd - blocks[0].bodyStart), "print(1)\r\n");
        EXPECT_EQ(blocks[1].language, "c++");
        EXPECT_EQ(doc.substr(blocks[1].bodyStart, blocks[1].bodyEnd - blocks[1].bodyStart), "```\nint x;\n");
        EXPECT_EQ(blocks[2].language, "");
        EXPECT_EQ(doc.substr(blocks[2].bodyStart), "open");
        EXPECT_EQ(blocks[2].bodyEnd, doc.size());
        EXPECT_EQ(syntax::findCodeBlocks("``` a`b\nx\n").size(), size_t{0});  // inline code, not a fence
        EXPECT_EQ(syntax::findCodeBlocks("```\n").size(), size_t{1});
        EXPECT_EQ(syntax::normalizeInfo(" language-JS "), "js");
    }
    {
        syntax::XmlElement doc;
        EXPECT_EQ(syntax::parseXml("\xEF\xBB\xBF<?xml version=\"1.0\"?><!-- c --><a x='1 &amp; 2'><b y=\"&#65;\"/>t&lt;</a>", doc), true);
        EXPECT_EQ(doc.children.size(), size_t{1});
        EXPECT_EQ(doc.children[0].attributeOr("x"), "1 & 2");
        EXPECT_EQ(doc.children[0].children[0].attributeOr("y"), "A");
        EXPECT_EQ(doc.children[0].text, "t<");

        const auto langs = syntax::parseLanguages(
            "<NotepadPlus><Languages><Language name=\"python\" ext=\"py pyw\" commentLine=\"#\">"
            "<Keywords name=\"instre1\">def if</Keywords><Keywords name=\"instre2\">len</Keywords></Language>"
            "<Language name=\"rust\" ext=\"rs\"/><Language name=\"javascript.js\" ext=\"js\"/><Language name=\"cs\" ext=\"cs\"/>"
            "</Languages></NotepadPlus>");
        EXPECT_EQ(langs.at("python").keywords.at("instre2"), "len");
        EXPECT_EQ(syntax::resolveLanguage("py", langs), "python");
        EXPECT_EQ(syntax::resolveLanguage("pyw", langs), "python");
        EXPECT_EQ(syntax::resolveLanguage("Rust", langs), "rust");
        EXPECT_EQ(syntax::resolveLanguage("C#", langs), "cs");
        EXPECT_EQ(syntax::resolveLanguage("js", langs), "javascript.js");
        EXPECT_EQ(syntax::resolveLanguage("mermaid", langs), "");
        EXPECT_EQ(syntax::resolveLanguage("", langs), "");
        EXPECT_EQ(syntax::lexerFor("cs"), "cpp");
        EXPECT_EQ(syntax::lexerFor("python"), "python");
        EXPECT_EQ(syntax::lexerFor("ini"), "props");

        const auto theme = syntax::parseTheme(
            "<NotepadPlus><LexerStyles><LexerType name=\"cs\" desc=\"C#\">"
            "<WordsStyle name=\"INSTRUCTION WORD\" styleID=\"5\" fgColor=\"0000FF\" fontStyle=\"1\" keywordClass=\"instre1\" />"
            "<WordsStyle name=\"TYPE WORD\" styleID=\"16\" fgColor=\"8000FF\" keywordClass=\"type1\">var dynamic</WordsStyle>"
            "<WordsStyle name=\"DEFAULT\" styleID=\"0\" fgColor=\"\" />"
            "</LexerType></LexerStyles><GlobalStyles><WidgetStyle name=\"Default Style\" styleID=\"32\" fgColor=\"112233\" />"
            "</GlobalStyles></NotepadPlus>");
        EXPECT_EQ(theme.styles.at("cs").at(5).foreground, uint32_t{0x0000FF});
        EXPECT_EQ(theme.styles.at("cs").at(5).fontStyle, 1);
        EXPECT_EQ(theme.styles.at("cs").at(0).hasForeground, false);
        EXPECT_EQ(theme.defaultStyle.foreground, uint32_t{0x112233});
        EXPECT_EQ(theme.keywords.at("cs").at("type1"), "var dynamic");
        EXPECT_EQ(theme.keywordStyles.at("cs").at("type1"), 16);
        EXPECT_EQ(syntax::keywordSetFor("cpp", "type1", 16), 1);
        EXPECT_EQ(syntax::keywordSetFor("cpp", "instre2", 19), 3);
        EXPECT_EQ(syntax::keywordSetFor("python", "instre2", 14), 1);
        EXPECT_EQ(syntax::keywordSetFor("sql", "type3", -1), 4);
        EXPECT_EQ(syntax::keywordSetFor("phpscript", "instre1", 5), 4);

        const auto config = syntax::parseConfig(
            "<NotepadPlus><GUIConfigs><GUIConfig name=\"stylerTheme\" path=\"C:\\t\\Obsidian.xml\" />"
            "<GUIConfig name=\"DarkMode\" enable=\"yes\" darkThemeName=\"DarkModeDefault.xml\" lightThemeName=\"\" />"
            "</GUIConfigs></NotepadPlus>");
        EXPECT_EQ(config.stylerTheme, "C:\\t\\Obsidian.xml");
        EXPECT_EQ(config.darkModeEnabled, true);
        EXPECT_EQ(config.darkThemeName, "DarkModeDefault.xml");
    }

    std::printf("%d checks, %d failed\n", g_total, g_failed);
    return g_failed == 0 ? 0 : 1;
}
