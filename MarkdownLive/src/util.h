// Small Win32 string / path helpers.
#pragma once

#include <windows.h>

#include <string>

std::wstring toWide(const std::string& bytes, UINT codePage = CP_UTF8);
std::string toBytes(const std::wstring& wide, UINT codePage = CP_UTF8);

// Returns `text` as a quoted JSON string literal.
std::wstring jsonString(const std::wstring& text);

// Decodes %XX escapes (interpreted as UTF-8).
std::wstring percentDecode(const std::wstring& text);

std::wstring directoryOf(const std::wstring& path);   // without trailing backslash
std::wstring fileNameOf(const std::wstring& path);
std::wstring fileStemOf(const std::wstring& path);
std::wstring extensionOf(const std::wstring& path);   // lower case, without dot
bool isAbsolutePath(const std::wstring& path);
bool fileExists(const std::wstring& path);
bool startsWithNoCase(const std::wstring& text, const wchar_t* prefix);

// Path of `target` relative to `baseDir` using forward slashes; falls back to the
// absolute path when both are on different drives.
std::wstring relativeUrlPath(const std::wstring& baseDir, const std::wstring& target);

bool writeFileUtf8(const std::wstring& path, const std::wstring& content);
// Whole file content ("" if it cannot be read).
std::string readFileBytes(const std::wstring& path);
