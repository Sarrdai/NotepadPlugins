// JSON++ - unit tests for the platform independent core.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "JsonCore.h"

#include <cstdio>
#include <random>
#include <string>
#include <string_view>
#include <type_traits>

namespace {

int g_failures = 0;
int g_checks = 0;

#define CHECK(cond)                                                              \
    do {                                                                         \
        ++g_checks;                                                              \
        if (!(cond)) {                                                           \
            ++g_failures;                                                        \
            std::printf("%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
        }                                                                        \
    } while (0)

template <class A, class B>
void checkEq(const A& a, const B& b, const char* expr, int line) {
    ++g_checks;
    if (a == b) return;
    ++g_failures;
    std::printf("%s:%d: CHECK_EQ failed: %s\n", __FILE__, line, expr);
    if constexpr (std::is_convertible_v<A, std::string_view>) {
        std::printf("  left:  [%s]\n  right: [%s]\n", std::string(a).c_str(), std::string(b).c_str());
    } else {
        std::printf("  left:  %llu\n  right: %llu\n", static_cast<unsigned long long>(a),
                    static_cast<unsigned long long>(b));
    }
}

#define CHECK_EQ(a, b) checkEq((a), (b), #a " == " #b, __LINE__)

std::string applyEdit(std::string_view text, const jpp::TextEdit& edit) {
    std::string out(text.substr(0, edit.start));
    out += edit.replacement;
    out += text.substr(edit.end);
    return out;
}

void testPrettyPrint() {
    const jpp::FormatOptions opt;
    CHECK_EQ(jpp::prettyPrint(R"({"a":1,"b":[true,null,{}],"c":{"d":"x y"}})", opt),
             std::string("{\n"
                         "    \"a\": 1,\n"
                         "    \"b\": [\n"
                         "        true,\n"
                         "        null,\n"
                         "        {}\n"
                         "    ],\n"
                         "    \"c\": {\n"
                         "        \"d\": \"x y\"\n"
                         "    }\n"
                         "}"));
    CHECK_EQ(jpp::prettyPrint("[]", opt), std::string("[]"));
    CHECK_EQ(jpp::prettyPrint("", opt), std::string(""));

    jpp::FormatOptions tabs;
    tabs.useTabs = true;
    tabs.spaceAfterColon = false;
    CHECK_EQ(jpp::prettyPrint(R"({"a":[1]})", tabs), std::string("{\n\t\"a\":[\n\t\t1\n\t]\n}"));

    // Strings keep their content, including escapes and structural characters.
    CHECK_EQ(jpp::prettyPrint(R"(["{,:}\"]"])", opt), std::string("[\n    \"{,:}\\\"]\"\n]"));

    // JSON Lines: one value per line.
    CHECK_EQ(jpp::prettyPrint("{\"a\":1}\n{\"b\":2}", opt), std::string("{\n    \"a\": 1\n}\n{\n    \"b\": 2\n}"));

    // Comments (JSONC).
    CHECK_EQ(jpp::prettyPrint("{// c\n\"a\":1/*x*/}", opt), std::string("{\n    // c\n    \"a\": 1 /*x*/\n}"));
}

void testCompact() {
    CHECK_EQ(jpp::compact("{\n  \"a\" : [ 1 , 2 ],\n  \"b\": \"x  y\"\n}\n"), std::string(R"({"a":[1,2],"b":"x  y"})"));
    CHECK_EQ(jpp::compact("[1 2]"), std::string("[1 2]"));
    CHECK_EQ(jpp::compact("{\"a\":1} {\"b\":2}"), std::string("{\"a\":1}\n{\"b\":2}"));
    CHECK_EQ(jpp::compact("[1, // one\n 2]"), std::string("[1,// one\n2]"));

    const std::string samples[] = {
        R"({"a":1,"b":[true,null,{}],"c":{"d":"x y","e":[[],[1,[2,[3]]]]}})",
        R"([{"k":"v\"\\","n":-1.5e+10},"äöü€😀",{"":""}])",
        "{\"a\":1}\n{\"b\":[2]}",
    };
    for (const auto& s : samples) {
        const std::string pretty = jpp::prettyPrint(s, {});
        CHECK_EQ(jpp::compact(pretty), s);
        CHECK_EQ(jpp::prettyPrint(jpp::compact(pretty), {}), pretty);
    }
}

void testValidation() {
    CHECK(jpp::validate(R"({"a":[1,2.5,-3e2,true,false,null,"x\nä"]})", false).valid);
    CHECK(jpp::validate("  ", false).valid);
    CHECK(jpp::validate("  ", false).empty);
    CHECK(jpp::validate("{\"a\":1}\n{\"b\":2}", false).valueCount == 2);

    auto r = jpp::validate(R"({"a":1,})", false);
    CHECK(!r.valid);
    CHECK_EQ(r.message, std::string("Trailing commas are not allowed"));
    CHECK_EQ(r.errorBegin, std::size_t{7});

    r = jpp::validate(R"([1,])", false);
    CHECK(!r.valid);
    CHECK_EQ(r.message, std::string("Trailing commas are not allowed"));

    r = jpp::validate(R"({"a" 1})", false);
    CHECK_EQ(r.message, std::string("Expected ':' after property name"));

    r = jpp::validate(R"({"a":01})", false);
    CHECK_EQ(r.message, std::string("Invalid number"));

    r = jpp::validate(R"({a:1})", false);
    CHECK_EQ(r.message, std::string("Property names must be double-quoted strings"));

    r = jpp::validate(R"(["\x"])", false);
    CHECK_EQ(r.message, std::string("Invalid escape sequence"));

    r = jpp::validate("[\"abc\\\"]", false);
    CHECK_EQ(r.message, std::string("Unterminated string"));

    r = jpp::validate(R"({"a":[1)", false);
    CHECK_EQ(r.message, std::string("Unexpected end of input, expected ']'"));

    r = jpp::validate("[1 2]", false);
    CHECK_EQ(r.message, std::string("Expected ',' or ']'"));

    CHECK(!jpp::validate("[1] // c", false).valid);
    CHECK(jpp::validate("[1] // c", true).valid);
    CHECK(!jpp::validate("[1] /* c", true).valid);

    // Deep nesting must not overflow the stack.
    std::string deep(200000, '[');
    deep += std::string(200000, ']');
    CHECK(jpp::validate(deep, false).valid);
}

void testSignificantMapping() {
    const std::string compactText = R"({"a":[1,22]})";
    const std::string pretty = jpp::prettyPrint(compactText, {});
    // Caret behind "22" in the compact text.
    const std::size_t caret = compactText.find("22") + 2;
    const std::size_t k = jpp::significantIndexAt(compactText, caret);
    const std::size_t mapped = jpp::positionAfterSignificantIndex(pretty, k);
    CHECK_EQ(pretty.substr(mapped - 2, 2), std::string("22"));
    CHECK_EQ(jpp::significantIndexAt(pretty, mapped), k);
    CHECK_EQ(jpp::positionAfterSignificantIndex(pretty, 0), std::size_t{0});
}

void testDiffText() {
    auto e = jpp::diffText("abcdef", "abXYef");
    CHECK_EQ(e.start, std::size_t{2});
    CHECK_EQ(e.end, std::size_t{4});
    CHECK_EQ(e.replacement, std::string("XY"));
    CHECK(jpp::diffText("same", "same").empty());
    // Never split UTF-8 sequences ("ä" = C3 A4, "ö" = C3 B6).
    e = jpp::diffText("x\xC3\xA4y", "x\xC3\xB6y");
    CHECK_EQ(e.start, std::size_t{1});
    CHECK_EQ(applyEdit("x\xC3\xA4y", e), std::string("x\xC3\xB6y"));
}

void testDiffContent() {
    // Editing the pretty view transfers a minimal compact edit into the document.
    const std::string doc = R"({"name":"old","list":[1,2,3]})";
    std::string pretty = jpp::prettyPrint(doc, {});
    pretty.replace(pretty.find("old"), 3, "new value");
    auto e = jpp::diffContent(doc, pretty);
    CHECK_EQ(applyEdit(doc, e), std::string(R"({"name":"new value","list":[1,2,3]})"));
    CHECK(e.end - e.start <= 5);  // only the "old" string token is replaced

    // Adding a member while editing in pretty layout.
    pretty = jpp::prettyPrint(doc, {});
    pretty.insert(pretty.rfind('}'), ",\n    \"added\": {\n        \"x\": true\n    }\n");
    e = jpp::diffContent(doc, pretty);
    CHECK_EQ(applyEdit(doc, e), std::string(R"({"name":"old","list":[1,2,3],"added":{"x":true}})"));

    // Layout of untouched parts of the document is preserved.
    const std::string spaced = "{ \"a\": 1, \"b\": 2 }";
    e = jpp::diffContent(spaced, "{\n  \"a\": 1,\n  \"b\": 3\n}");
    CHECK_EQ(applyEdit(spaced, e), std::string("{ \"a\": 1, \"b\": 3 }"));

    // Deleting everything.
    e = jpp::diffContent(doc, "");
    CHECK_EQ(applyEdit(doc, e), std::string(""));
    // Inserting into an empty document.
    e = jpp::diffContent("", "[\n    1\n]");
    CHECK_EQ(applyEdit("", e), std::string("[1]"));
    // No content change -> empty edit.
    CHECK(jpp::diffContent(doc, jpp::prettyPrint(doc, {})).empty());
}

void testDiffContentRandomized() {
    // Random edits on the pretty text must always yield compact(document') == compact(pretty').
    std::mt19937 rng(12345);
    const std::string alphabet = "{}[],:\"ab 1\n\\/*'";
    const std::string base = R"({"a":[1,2,{"b":"c d"}],"e":{"f":null,"g":"h"},"i":[[],{}]})";
    int checkedRounds = 0;
    for (int round = 0; round < 20000; ++round) {
        std::string doc = round % 2 ? base : jpp::prettyPrint(base, {});
        std::string view = jpp::prettyPrint(doc, {});
        const int edits = 1 + static_cast<int>(rng() % 4);
        for (int i = 0; i < edits; ++i) {
            const std::size_t pos = view.empty() ? 0 : rng() % (view.size() + 1);
            if (rng() % 2 && !view.empty() && pos < view.size()) {
                view.erase(pos, 1 + rng() % 3);
            } else {
                view.insert(pos, 1, alphabet[rng() % alphabet.size()]);
            }
        }
        // Unterminated strings make whitespace significant; the plugin defers those states.
        if (jpp::hasUnterminatedTokens(view)) continue;
        ++checkedRounds;
        const auto e = jpp::diffContent(doc, view);
        const int failuresBefore = g_failures;
        CHECK_EQ(jpp::compact(applyEdit(doc, e)), jpp::compact(view));
        if (g_failures != failuresBefore) {
            std::printf("  doc:  [%s]\n  view: [%s]\n  edit: %zu-%zu [%s]\n", doc.c_str(), view.c_str(), e.start,
                        e.end, e.replacement.c_str());
        }
        if (g_failures > 20) return;
    }
    CHECK(checkedRounds > 1000);
}

void testUnterminated() {
    CHECK(!jpp::hasUnterminatedTokens(R"({"a":"b\\"})"));
    CHECK(jpp::hasUnterminatedTokens(R"({"a":"b})"));
    CHECK(jpp::hasUnterminatedTokens(R"({"a":"b\"})"));
    CHECK(jpp::hasUnterminatedTokens("[1] /* x"));
}

void testDetection() {
    jpp::DetectOptions opt;
    const std::string compactJson = R"({"a":[1,2,3],"b":"some longer value to reach the minimum length"})";
    CHECK(jpp::looksCompact(compactJson, compactJson.size(), 1, opt));
    CHECK(!jpp::looksCompact("{\"a\":1}", 7, 1, opt));
    CHECK(!jpp::looksCompact("hello world", 200, 1, opt));
    const std::string pretty = jpp::prettyPrint(compactJson, {});
    CHECK(!jpp::looksCompact(pretty, pretty.size(), 5, opt));
    CHECK(jpp::looksCompact(compactJson, 5000, 3, opt));
}

}  // namespace

int main() {
    testPrettyPrint();
    testCompact();
    testValidation();
    testSignificantMapping();
    testDiffText();
    testDiffContent();
    testDiffContentRandomized();
    testUnterminated();
    testDetection();
    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
