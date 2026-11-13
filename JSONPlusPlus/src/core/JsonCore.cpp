// JSON++ - Notepad++ plugin
// Copyright (C) 2026 JSON++ contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "JsonCore.h"

#include <algorithm>

namespace jpp {

namespace {

constexpr bool isWhitespace(char c) noexcept {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

constexpr bool isStructural(char c) noexcept {
    return c == '{' || c == '}' || c == '[' || c == ']' || c == ':' || c == ',';
}

constexpr bool startsValue(TokenType t) noexcept {
    return t == TokenType::BeginObject || t == TokenType::BeginArray || t == TokenType::String ||
           t == TokenType::Scalar;
}

constexpr bool endsValue(TokenType t) noexcept {
    return t == TokenType::EndObject || t == TokenType::EndArray || t == TokenType::String || t == TokenType::Scalar;
}

constexpr bool isComment(TokenType t) noexcept {
    return t == TokenType::LineComment || t == TokenType::BlockComment;
}

constexpr bool isUtf8Continuation(char c) noexcept {
    return (static_cast<unsigned char>(c) & 0xC0) == 0x80;
}

// Drives the canonical compact representation. Calls sink(char, srcPos, synthetic) for every
// character of compact(src); stops early when the sink returns false.
// Synthetic separators report the position of the last byte of the preceding
// token, so "srcPos + 1" is always the insertion point after that character.
template <class Sink>
void scanCompact(std::string_view src, Sink&& sink) {
    Lexer lexer(src);
    Token tok;
    Token prev;
    bool hasPrev = false;
    int depth = 0;
    while (lexer.next(tok)) {
        if (hasPrev) {
            char separator = 0;
            if (prev.type == TokenType::LineComment) {
                separator = '\n';
            } else if (depth == 0 && endsValue(prev.type) && startsValue(tok.type)) {
                separator = '\n';  // JSON Lines / JSON sequences
            } else if (prev.type == TokenType::Scalar && tok.type == TokenType::Scalar) {
                separator = ' ';  // keep "1 2" from collapsing into "12"
            }
            if (separator != 0 && !sink(separator, prev.end - 1, true)) return;
        }
        for (std::size_t i = tok.begin; i < tok.end; ++i) {
            if (!sink(src[i], i, false)) return;
        }
        if (tok.type == TokenType::BeginObject || tok.type == TokenType::BeginArray) {
            ++depth;
        } else if ((tok.type == TokenType::EndObject || tok.type == TokenType::EndArray) && depth > 0) {
            --depth;
        }
        prev = tok;
        hasPrev = true;
    }
}

// Token that contains byte position `pos` (begin < pos < end), or an empty span at `pos`.
Token tokenContaining(std::string_view text, std::size_t pos) noexcept {
    Lexer lexer(text);
    Token tok;
    while (lexer.next(tok)) {
        if (tok.end > pos) return tok.begin < pos ? tok : Token{TokenType::Scalar, pos, pos, true};
    }
    return Token{TokenType::Scalar, pos, pos, true};
}

bool isHex(char c) noexcept {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

bool isDigit(char c) noexcept {
    return c >= '0' && c <= '9';
}

bool isValidNumber(std::string_view s) noexcept {
    std::size_t i = 0;
    const std::size_t n = s.size();
    if (i < n && s[i] == '-') ++i;
    if (i >= n) return false;
    if (s[i] == '0') {
        ++i;
    } else if (s[i] >= '1' && s[i] <= '9') {
        while (i < n && isDigit(s[i])) ++i;
    } else {
        return false;
    }
    if (i < n && s[i] == '.') {
        ++i;
        if (i >= n || !isDigit(s[i])) return false;
        while (i < n && isDigit(s[i])) ++i;
    }
    if (i < n && (s[i] == 'e' || s[i] == 'E')) {
        ++i;
        if (i < n && (s[i] == '+' || s[i] == '-')) ++i;
        if (i >= n || !isDigit(s[i])) return false;
        while (i < n && isDigit(s[i])) ++i;
    }
    return i == n;
}

struct Issue {
    bool ok = true;
    std::size_t begin = 0;
    std::size_t end = 0;
    const char* message = "";
};

Issue checkString(std::string_view src, const Token& tok) noexcept {
    if (src[tok.begin] != '"') return {false, tok.begin, tok.end, "Strings must use double quotes"};
    const bool closed = tok.end - tok.begin >= 2 && src[tok.end - 1] == '"';
    std::size_t i = tok.begin + 1;
    const std::size_t contentEnd = closed ? tok.end - 1 : tok.end;
    while (i < contentEnd) {
        const auto c = static_cast<unsigned char>(src[i]);
        if (c < 0x20) return {false, i, i + 1, "Control characters must be escaped in strings"};
        if (c == '\\') {
            if (i + 1 >= contentEnd) return {false, tok.begin, tok.end, "Unterminated string"};
            const char e = src[i + 1];
            if (e == 'u') {
                if (i + 6 > contentEnd || !isHex(src[i + 2]) || !isHex(src[i + 3]) || !isHex(src[i + 4]) ||
                    !isHex(src[i + 5])) {
                    return {false, i, std::min(i + 6, contentEnd), "Invalid unicode escape sequence"};
                }
                i += 6;
                continue;
            }
            if (e != '"' && e != '\\' && e != '/' && e != 'b' && e != 'f' && e != 'n' && e != 'r' && e != 't') {
                return {false, i, i + 2, "Invalid escape sequence"};
            }
            i += 2;
            continue;
        }
        ++i;
    }
    if (!closed) return {false, tok.begin, tok.end, "Unterminated string"};
    return {};
}

Issue checkScalar(std::string_view src, const Token& tok) noexcept {
    const std::string_view text = src.substr(tok.begin, tok.end - tok.begin);
    if (text == "true" || text == "false" || text == "null") return {};
    if (isDigit(text.front()) || text.front() == '-') {
        if (isValidNumber(text)) return {};
        return {false, tok.begin, tok.end, "Invalid number"};
    }
    return {false, tok.begin, tok.end, "Unexpected token (expected a value)"};
}

}  // namespace

// ---------------------------------------------------------------------------
// Lexer
// ---------------------------------------------------------------------------

bool Lexer::next(Token& tok) noexcept {
    const std::size_t n = _src.size();
    while (_pos < n && isWhitespace(_src[_pos])) ++_pos;
    if (_pos >= n) return false;

    const char c = _src[_pos];
    tok.begin = _pos;
    tok.closed = true;

    switch (c) {
        case '{':
            tok.type = TokenType::BeginObject;
            ++_pos;
            break;
        case '}':
            tok.type = TokenType::EndObject;
            ++_pos;
            break;
        case '[':
            tok.type = TokenType::BeginArray;
            ++_pos;
            break;
        case ']':
            tok.type = TokenType::EndArray;
            ++_pos;
            break;
        case ':':
            tok.type = TokenType::Colon;
            ++_pos;
            break;
        case ',':
            tok.type = TokenType::Comma;
            ++_pos;
            break;
        case '"':
        case '\'': {
            tok.type = TokenType::String;
            tok.closed = false;
            ++_pos;
            while (_pos < n) {
                const char d = _src[_pos];
                if (d == '\n' || d == '\r') break;  // unterminated: never swallow following lines
                if (d == '\\') {
                    if (_pos + 1 < n && _src[_pos + 1] != '\n' && _src[_pos + 1] != '\r') {
                        _pos += 2;
                    } else {
                        ++_pos;
                    }
                    continue;
                }
                ++_pos;
                if (d == c) {
                    tok.closed = true;
                    break;
                }
            }
            break;
        }
        default:
            if (c == '/' && _pos + 1 < n && _src[_pos + 1] == '/') {
                tok.type = TokenType::LineComment;
                while (_pos < n && _src[_pos] != '\n' && _src[_pos] != '\r') ++_pos;
            } else if (c == '/' && _pos + 1 < n && _src[_pos + 1] == '*') {
                tok.type = TokenType::BlockComment;
                const std::size_t close = _src.find("*/", _pos + 2);
                tok.closed = close != std::string_view::npos;
                _pos = tok.closed ? close + 2 : n;
            } else {
                tok.type = TokenType::Scalar;
                while (_pos < n) {
                    const char d = _src[_pos];
                    if (isWhitespace(d) || isStructural(d) || d == '"' || d == '\'') break;
                    if (d == '/' && _pos + 1 < n && (_src[_pos + 1] == '/' || _src[_pos + 1] == '*')) break;
                    ++_pos;
                }
            }
            break;
    }
    tok.end = _pos;
    return true;
}

// ---------------------------------------------------------------------------
// Formatting
// ---------------------------------------------------------------------------

std::string prettyPrint(std::string_view src, const FormatOptions& options) {
    std::string out;
    out.reserve(src.size() + src.size() / 2 + 16);

    const std::string unit = options.useTabs
                                 ? std::string("\t")
                                 : std::string(static_cast<std::size_t>(std::max(0, options.indentSize)), ' ');
    int level = 0;
    bool pendingNewline = false;
    bool hasPrev = false;
    TokenType prev = TokenType::Comma;

    auto flush = [&] {
        if (!pendingNewline) return;
        pendingNewline = false;
        out.push_back('\n');
        for (int i = 0; i < level; ++i) out += unit;
    };
    auto separateValue = [&](TokenType current) {
        if (!hasPrev || pendingNewline) return;
        if (level == 0 && endsValue(prev) && startsValue(current)) {
            pendingNewline = true;
        } else if ((endsValue(prev) || prev == TokenType::BlockComment) && startsValue(current)) {
            out.push_back(' ');
        }
    };

    Lexer lexer(src);
    Token tok;
    Token ahead;
    bool hasAhead = lexer.next(ahead);
    while (hasAhead) {
        tok = ahead;
        hasAhead = lexer.next(ahead);
        const std::string_view text = src.substr(tok.begin, tok.end - tok.begin);

        switch (tok.type) {
            case TokenType::BeginObject:
            case TokenType::BeginArray: {
                separateValue(tok.type);
                flush();
                out += text;
                const TokenType close = tok.type == TokenType::BeginObject ? TokenType::EndObject : TokenType::EndArray;
                if (hasAhead && ahead.type == close) {  // keep empty containers on one line
                    out += src.substr(ahead.begin, ahead.end - ahead.begin);
                    hasAhead = lexer.next(ahead);
                    prev = close;
                    hasPrev = true;
                    continue;
                }
                ++level;
                pendingNewline = true;
                break;
            }
            case TokenType::EndObject:
            case TokenType::EndArray:
                if (level > 0) --level;
                pendingNewline = true;
                flush();
                out += text;
                break;
            case TokenType::Comma:
                flush();
                out.push_back(',');
                pendingNewline = true;
                break;
            case TokenType::Colon:
                flush();
                out.push_back(':');
                if (options.spaceAfterColon) out.push_back(' ');
                break;
            case TokenType::String:
            case TokenType::Scalar:
                separateValue(tok.type);
                flush();
                out += text;
                break;
            case TokenType::LineComment:
            case TokenType::BlockComment:
                if (hasPrev && !pendingNewline) {
                    if (!out.empty() && out.back() != ' ') out.push_back(' ');
                } else {
                    flush();
                }
                out += text;
                if (tok.type == TokenType::LineComment) pendingNewline = true;
                break;
        }
        prev = tok.type;
        hasPrev = true;
    }
    return out;
}

std::string compact(std::string_view src) {
    std::string out;
    out.reserve(src.size());
    scanCompact(src, [&out](char c, std::size_t, bool) {
        out.push_back(c);
        return true;
    });
    return out;
}

bool hasUnterminatedTokens(std::string_view src) noexcept {
    Lexer lexer(src);
    Token tok;
    while (lexer.next(tok)) {
        if (!tok.closed) return true;
    }
    return false;
}

std::size_t significantIndexAt(std::string_view src, std::size_t pos) noexcept {
    std::size_t count = 0;
    scanCompact(src, [&](char, std::size_t srcPos, bool) {
        if (srcPos >= pos) return false;
        ++count;
        return true;
    });
    return count;
}

std::size_t positionAfterSignificantIndex(std::string_view src, std::size_t index) noexcept {
    if (index == 0) return 0;
    std::size_t count = 0;
    std::size_t result = src.size();
    scanCompact(src, [&](char, std::size_t srcPos, bool) {
        if (++count == index) {
            result = srcPos + 1;
            return false;
        }
        return true;
    });
    return result;
}

std::vector<std::size_t> positionsOfSignificantIndices(std::string_view src,
                                                       const std::vector<std::size_t>& sortedIndices) {
    std::vector<std::size_t> result(sortedIndices.size(), src.size());
    if (sortedIndices.empty()) return result;
    std::size_t next = 0;
    std::size_t count = 0;
    scanCompact(src, [&](char, std::size_t srcPos, bool) {
        while (next < sortedIndices.size() && sortedIndices[next] == count) {
            result[next++] = srcPos;
        }
        ++count;
        return next < sortedIndices.size();
    });
    return result;
}

// ---------------------------------------------------------------------------
// Diffing
// ---------------------------------------------------------------------------

TextEdit diffText(std::string_view before, std::string_view after) {
    const std::size_t limit = std::min(before.size(), after.size());
    std::size_t prefix = 0;
    while (prefix < limit && before[prefix] == after[prefix]) ++prefix;
    if (prefix == before.size() && prefix == after.size()) return TextEdit{prefix, prefix, {}};
    while (prefix > 0 && prefix < before.size() && isUtf8Continuation(before[prefix])) --prefix;

    std::size_t suffix = 0;
    const std::size_t suffixLimit = limit - prefix;
    while (suffix < suffixLimit && before[before.size() - 1 - suffix] == after[after.size() - 1 - suffix]) ++suffix;
    while (suffix > 0 && isUtf8Continuation(before[before.size() - suffix])) --suffix;

    TextEdit edit;
    edit.start = prefix;
    edit.end = before.size() - suffix;
    edit.replacement.assign(after.substr(prefix, after.size() - suffix - prefix));
    return edit;
}

TextEdit diffContent(std::string_view document, std::string_view edited) {
    const std::string before = compact(document);
    const std::string after = compact(edited);
    TextEdit edit = diffText(before, after);
    if (edit.empty()) return TextEdit{};

    // Grow the edit to whole tokens of both texts. The lexer is context free at
    // token boundaries, so the unchanged prefix / suffix then tokenize identically
    // and whitespace of the document can never be glued into a token of `edited`.
    std::size_t p = edit.start;
    std::size_t suffix = before.size() - edit.end;
    for (bool changed = true; changed;) {
        changed = false;
        for (const std::string_view text : {std::string_view(before), std::string_view(after)}) {
            const auto start = tokenContaining(text, p);
            if (start.begin < p && p < start.end) {
                p = start.begin;
                changed = true;
            }
            const std::size_t end = text.size() - suffix;
            const auto last = tokenContaining(text, end);
            if (last.begin < end && end < last.end) {
                suffix = text.size() - last.end;
                changed = true;
            }
        }
    }
    const std::size_t q = before.size() - suffix;
    const std::size_t afterEnd = after.size() - suffix;
    edit.replacement.assign(after.substr(p, afterEnd - p));

    // Map the compact range [p, q) back to byte positions of the document.
    // A synthetic separator has no width: it sits directly behind the previous token.
    struct Anchor {
        std::size_t start;  // insertion point before the character
        std::size_t end;    // position behind the character
    };
    std::size_t lookup[2] = {p < before.size() ? p : (p > 0 ? p - 1 : 0), q > p ? q - 1 : 0};
    Anchor anchors[2] = {{document.size(), document.size()}, {document.size(), document.size()}};
    if (!before.empty()) {
        std::size_t count = 0;
        const std::size_t last = std::max(lookup[0], lookup[1]);
        scanCompact(document, [&](char, std::size_t srcPos, bool synthetic) {
            const Anchor anchor{synthetic ? srcPos + 1 : srcPos, srcPos + 1};
            if (count == lookup[0]) anchors[0] = anchor;
            if (count == lookup[1]) anchors[1] = anchor;
            return ++count <= last;
        });
    }

    std::size_t rawStart = 0;
    if (!before.empty()) rawStart = p < before.size() ? anchors[0].start : anchors[0].end;
    const std::size_t rawEnd = q > p ? anchors[1].end : rawStart;

    edit.start = rawStart;
    edit.end = std::max(rawStart, rawEnd);
    return edit;
}

// ---------------------------------------------------------------------------
// Validation
// ---------------------------------------------------------------------------

ValidationResult validate(std::string_view src, bool allowComments) {
    enum class State { Value, ArrayFirst, ObjectFirst, Key, Colon, AfterValue };

    ValidationResult result;
    std::vector<char> stack;
    State state = State::Value;
    TokenType prevType = TokenType::Comma;
    bool hasPrev = false;

    auto fail = [&](std::size_t begin, std::size_t end, std::string message) {
        result.valid = false;
        result.errorBegin = begin;
        result.errorEnd = std::max(end, begin);
        result.message = std::move(message);
        return result;
    };
    auto completeValue = [&] {
        if (stack.empty()) ++result.valueCount;
        state = State::AfterValue;
    };

    Lexer lexer(src);
    Token tok;
    while (lexer.next(tok)) {
        if (isComment(tok.type)) {
            if (!allowComments) return fail(tok.begin, tok.end, "Comments are not allowed in strict JSON");
            if (tok.type == TokenType::BlockComment &&
                (tok.end - tok.begin < 4 || src.substr(tok.end - 2, 2) != "*/")) {
                return fail(tok.begin, tok.end, "Unterminated comment");
            }
            continue;
        }

        if (state == State::AfterValue && stack.empty()) {
            if (!startsValue(tok.type))
                return fail(tok.begin, tok.end, "Unexpected character after the end of the value");
            state = State::Value;  // next value of a JSON sequence
        }

        switch (state) {
            case State::ArrayFirst:
                if (tok.type == TokenType::EndArray) {
                    stack.pop_back();
                    completeValue();
                    break;
                }
                [[fallthrough]];
            case State::Value:
                switch (tok.type) {
                    case TokenType::BeginObject:
                        stack.push_back('{');
                        state = State::ObjectFirst;
                        break;
                    case TokenType::BeginArray:
                        stack.push_back('[');
                        state = State::ArrayFirst;
                        break;
                    case TokenType::String: {
                        const Issue issue = checkString(src, tok);
                        if (!issue.ok) return fail(issue.begin, issue.end, issue.message);
                        completeValue();
                        break;
                    }
                    case TokenType::Scalar: {
                        const Issue issue = checkScalar(src, tok);
                        if (!issue.ok) return fail(issue.begin, issue.end, issue.message);
                        completeValue();
                        break;
                    }
                    default:
                        if (hasPrev && prevType == TokenType::Comma &&
                            (tok.type == TokenType::EndArray || tok.type == TokenType::EndObject)) {
                            return fail(tok.begin, tok.end, "Trailing commas are not allowed");
                        }
                        return fail(tok.begin, tok.end, "Expected a value");
                }
                break;
            case State::ObjectFirst:
            case State::Key:
                if (state == State::ObjectFirst && tok.type == TokenType::EndObject) {
                    stack.pop_back();
                    completeValue();
                    break;
                }
                if (tok.type == TokenType::String) {
                    const Issue issue = checkString(src, tok);
                    if (!issue.ok) return fail(issue.begin, issue.end, issue.message);
                    state = State::Colon;
                    break;
                }
                if (tok.type == TokenType::EndObject)
                    return fail(tok.begin, tok.end, "Trailing commas are not allowed");
                if (tok.type == TokenType::Scalar) {
                    return fail(tok.begin, tok.end, "Property names must be double-quoted strings");
                }
                return fail(
                    tok.begin, tok.end,
                    state == State::ObjectFirst ? "Expected a property name or '}'" : "Expected a property name");
            case State::Colon:
                if (tok.type != TokenType::Colon) return fail(tok.begin, tok.end, "Expected ':' after property name");
                state = State::Value;
                break;
            case State::AfterValue: {
                const char open = stack.back();
                if (tok.type == TokenType::Comma) {
                    state = open == '{' ? State::Key : State::Value;
                } else if ((tok.type == TokenType::EndObject && open == '{') ||
                           (tok.type == TokenType::EndArray && open == '[')) {
                    stack.pop_back();
                    completeValue();
                } else {
                    return fail(tok.begin, tok.end, open == '{' ? "Expected ',' or '}'" : "Expected ',' or ']'");
                }
                break;
            }
        }
        prevType = tok.type;
        hasPrev = true;
    }

    if (!stack.empty()) {
        return fail(src.size(), src.size(),
                    std::string("Unexpected end of input, expected '") + (stack.back() == '{' ? '}' : ']') + "'");
    }
    if (state == State::Colon || (state == State::Value && hasPrev)) {
        return fail(src.size(), src.size(), "Unexpected end of input");
    }
    result.empty = result.valueCount == 0;
    return result;
}

// ---------------------------------------------------------------------------
// Detection
// ---------------------------------------------------------------------------

bool looksCompact(std::string_view head, std::size_t totalLength, std::size_t lineCount,
                  const DetectOptions& options) noexcept {
    if (totalLength < options.minLength) return false;
    std::size_t i = 0;
    while (i < head.size() && isWhitespace(head[i])) ++i;
    if (i >= head.size() || (head[i] != '{' && head[i] != '[')) return false;
    if (lineCount <= 1) return true;
    return totalLength / lineCount >= options.minAverageLineLength;
}

}  // namespace jpp
