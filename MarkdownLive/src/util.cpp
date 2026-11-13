#include "util.h"

#include <shlwapi.h>

#include <cwctype>

std::wstring toWide(const std::string& bytes, UINT codePage) {
    if (bytes.empty()) return {};
    const int n = MultiByteToWideChar(codePage, 0, bytes.data(), static_cast<int>(bytes.size()), nullptr, 0);
    if (n <= 0) return {};
    std::wstring wide(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(codePage, 0, bytes.data(), static_cast<int>(bytes.size()), &wide[0], n);
    return wide;
}

std::string toBytes(const std::wstring& wide, UINT codePage) {
    if (wide.empty()) return {};
    const int n = WideCharToMultiByte(codePage, 0, wide.data(), static_cast<int>(wide.size()), nullptr, 0, nullptr, nullptr);
    if (n <= 0) return {};
    std::string bytes(static_cast<size_t>(n), '\0');
    WideCharToMultiByte(codePage, 0, wide.data(), static_cast<int>(wide.size()), &bytes[0], n, nullptr, nullptr);
    return bytes;
}

std::wstring jsonString(const std::wstring& text) {
    static const wchar_t* const hex = L"0123456789abcdef";
    std::wstring out;
    out.reserve(text.size() + text.size() / 16 + 2);
    out += L'"';
    for (size_t i = 0; i < text.size(); ++i) {
        const wchar_t c = text[i];
        switch (c) {
            case L'"': out += L"\\\""; break;
            case L'\\': out += L"\\\\"; break;
            case L'\n': out += L"\\n"; break;
            case L'\r': out += L"\\r"; break;
            case L'\t': out += L"\\t"; break;
            default:
                if (c < 0x20) {
                    out += L"\\u00";
                    out += hex[(c >> 4) & 0xF];
                    out += hex[c & 0xF];
                } else if (c >= 0xD800 && c <= 0xDBFF) {
                    // keep valid surrogate pairs, replace lone surrogates
                    if (i + 1 < text.size() && text[i + 1] >= 0xDC00 && text[i + 1] <= 0xDFFF) {
                        out += c;
                        out += text[++i];
                    } else {
                        out += L'\xFFFD';
                    }
                } else if (c >= 0xDC00 && c <= 0xDFFF) {
                    out += L'\xFFFD';
                } else {
                    out += c;
                }
        }
    }
    out += L'"';
    return out;
}

std::wstring percentDecode(const std::wstring& text) {
    auto hexValue = [](wchar_t c) -> int {
        if (c >= L'0' && c <= L'9') return c - L'0';
        if (c >= L'a' && c <= L'f') return c - L'a' + 10;
        if (c >= L'A' && c <= L'F') return c - L'A' + 10;
        return -1;
    };
    std::wstring out;
    std::string pending;  // consecutive %XX bytes form one UTF-8 sequence
    auto flush = [&] {
        if (!pending.empty()) {
            out += toWide(pending, CP_UTF8);
            pending.clear();
        }
    };
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] == L'%' && i + 2 < text.size() && hexValue(text[i + 1]) >= 0 && hexValue(text[i + 2]) >= 0) {
            pending += static_cast<char>(hexValue(text[i + 1]) * 16 + hexValue(text[i + 2]));
            i += 2;
        } else {
            flush();
            out += text[i];
        }
    }
    flush();
    return out;
}

std::wstring directoryOf(const std::wstring& path) {
    const size_t pos = path.find_last_of(L"\\/");
    return pos == std::wstring::npos ? std::wstring() : path.substr(0, pos);
}

std::wstring fileNameOf(const std::wstring& path) {
    const size_t pos = path.find_last_of(L"\\/");
    return pos == std::wstring::npos ? path : path.substr(pos + 1);
}

std::wstring fileStemOf(const std::wstring& path) {
    const std::wstring name = fileNameOf(path);
    const size_t dot = name.find_last_of(L'.');
    return dot == std::wstring::npos || dot == 0 ? name : name.substr(0, dot);
}

std::wstring extensionOf(const std::wstring& path) {
    const std::wstring name = fileNameOf(path);
    const size_t dot = name.find_last_of(L'.');
    if (dot == std::wstring::npos) return {};
    std::wstring ext = name.substr(dot + 1);
    for (auto& c : ext) c = static_cast<wchar_t>(std::towlower(c));
    return ext;
}

bool isAbsolutePath(const std::wstring& path) {
    if (path.size() >= 3 && path[1] == L':' && (path[2] == L'\\' || path[2] == L'/')) return true;
    return path.size() >= 2 && (path[0] == L'\\' || path[0] == L'/') && (path[1] == L'\\' || path[1] == L'/');
}

bool fileExists(const std::wstring& path) {
    const DWORD attrs = GetFileAttributesW(path.c_str());
    return attrs != INVALID_FILE_ATTRIBUTES && !(attrs & FILE_ATTRIBUTE_DIRECTORY);
}

bool startsWithNoCase(const std::wstring& text, const wchar_t* prefix) {
    const size_t n = wcslen(prefix);
    return text.size() >= n && _wcsnicmp(text.c_str(), prefix, n) == 0;
}

std::wstring relativeUrlPath(const std::wstring& baseDir, const std::wstring& target) {
    std::wstring result = target;
    wchar_t buffer[MAX_PATH] = {0};
    if (!baseDir.empty() && baseDir.size() < MAX_PATH && target.size() < MAX_PATH &&
        PathRelativePathToW(buffer, baseDir.c_str(), FILE_ATTRIBUTE_DIRECTORY, target.c_str(), FILE_ATTRIBUTE_NORMAL)) {
        result = buffer;
        if (result.compare(0, 2, L".\\") == 0) result.erase(0, 2);
    }
    std::wstring out;
    for (wchar_t c : result) {
        if (c == L'\\') out += L'/';
        else if (c == L' ') out += L"%20";
        else if (c == L'(') out += L"%28";
        else if (c == L')') out += L"%29";
        else out += c;
    }
    return out;
}

bool writeFileUtf8(const std::wstring& path, const std::wstring& content) {
    const std::string bytes = toBytes(content, CP_UTF8);
    const HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    const BOOL ok = WriteFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr);
    CloseHandle(file);
    return ok && written == bytes.size();
}

std::string readFileBytes(const std::wstring& path) {
    const HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                                    OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return {};
    std::string bytes;
    LARGE_INTEGER size = {};
    if (GetFileSizeEx(file, &size) && size.QuadPart > 0 && size.QuadPart < 64 * 1024 * 1024) {
        bytes.resize(static_cast<size_t>(size.QuadPart));
        DWORD read = 0;
        if (!ReadFile(file, &bytes[0], static_cast<DWORD>(bytes.size()), &read, nullptr)) read = 0;
        bytes.resize(read);
    }
    CloseHandle(file);
    return bytes;
}
