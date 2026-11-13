// JSON++ - Notepad++ plugin
// Copyright (C) 2026 JSON++ contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Platform independent JSON text engine used by the plugin.
// All functions work on raw bytes (UTF-8 or any ASCII compatible code page);
// structural JSON characters are ASCII, so multi-byte sequences are never split
// by the lexer.

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace jpp {

enum class TokenType : std::uint8_t {
    BeginObject,
    EndObject,
    BeginArray,
    EndArray,
    Colon,
    Comma,
    String,        // "..." or '...' (may be unterminated, never spans a line break)
    Scalar,        // numbers, literals and any other run of bytes (tolerant)
    LineComment,   // // ... (without the terminating line break)
    BlockComment,  // /* ... */ (may be unterminated)
};

struct Token {
    TokenType type{};
    std::size_t begin = 0;
    std::size_t end = 0;  // exclusive
    bool closed = true;   // false for unterminated strings / block comments
};

// Tolerant lexer: never fails. Every byte that is not insignificant whitespace
// belongs to exactly one token.
class Lexer {
public:
    explicit Lexer(std::string_view src) noexcept : _src(src) {}
    bool next(Token& tok) noexcept;

private:
    std::string_view _src;
    std::size_t _pos = 0;
};

struct FormatOptions {
    int indentSize = 4;
    bool useTabs = false;
    bool spaceAfterColon = true;
};

// Pretty prints the given text. Works on invalid JSON as well (best effort).
std::string prettyPrint(std::string_view src, const FormatOptions& options);

// Removes all insignificant whitespace. Line comments keep their terminating
// '\n', top level values (JSON Lines / sequences) are separated by '\n'.
std::string compact(std::string_view src);

// True if the text contains an unterminated string or block comment. In that
// state whitespace becomes significant, so layouts cannot be mapped safely.
bool hasUnterminatedTokens(std::string_view src) noexcept;

// "Significant index": number of characters of compact(src) produced by the
// bytes before byte position `pos` of `src`. Used to map positions between
// different layouts of the same JSON content.
std::size_t significantIndexAt(std::string_view src, std::size_t pos) noexcept;

// Inverse of significantIndexAt: byte position in `src` directly after the
// (index-1)-th significant character (0 for index 0, src.size() if out of range).
std::size_t positionAfterSignificantIndex(std::string_view src, std::size_t index) noexcept;

// Byte position in `src` of each given significant index. `sortedIndices` must be
// ascending. Indices beyond the end map to src.size().
std::vector<std::size_t> positionsOfSignificantIndices(std::string_view src,
                                                       const std::vector<std::size_t>& sortedIndices);

// Minimal single-range text replacement turning `before` into `after`.
struct TextEdit {
    std::size_t start = 0;  // byte position in `before`
    std::size_t end = 0;    // exclusive end in `before`
    std::string replacement;
    bool empty() const noexcept { return start == end && replacement.empty(); }
};

// Common prefix / suffix diff. Never splits UTF-8 sequences.
TextEdit diffText(std::string_view before, std::string_view after);

// Computes the edit that transfers the *content* of `edited` (any layout) into
// `document` while keeping the layout of all unchanged parts of `document`.
// Inserted parts are written in compact form.
TextEdit diffContent(std::string_view document, std::string_view edited);

struct ValidationResult {
    bool valid = true;
    bool empty = false;          // document contains no value
    std::size_t valueCount = 0;  // > 1 for JSON Lines / sequences
    std::size_t errorBegin = 0;
    std::size_t errorEnd = 0;
    std::string message;
};

// Strict RFC 8259 validation (optionally accepting comments and multiple top
// level values). Iterative, so deeply nested documents cannot overflow the stack.
ValidationResult validate(std::string_view src, bool allowComments);

struct DetectOptions {
    std::size_t minLength = 64;  // shorter documents are never "compact"
    std::size_t minAverageLineLength = 300;
};

// Heuristic used for automatic activation: the document starts with an object
// or array and has (almost) no line breaks compared to its size.
bool looksCompact(std::string_view head, std::size_t totalLength, std::size_t lineCount,
                  const DetectOptions& options) noexcept;

}  // namespace jpp
