#include "code_syntax.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>

namespace syntax {

namespace {

bool isBlank(char c) {
    return c == ' ' || c == '\t';
}

std::string toLower(std::string s) {
    for (char& c : s) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    return s;
}

std::string trim(const std::string& s) {
    const char* ws = " \t\r\n";
    const size_t start = s.find_first_not_of(ws);
    if (start == std::string::npos) return {};
    return s.substr(start, s.find_last_not_of(ws) - start + 1);
}

// ---- XML ---------------------------------------------------------------------------------------

void appendUtf8(std::string& out, unsigned long cp) {
    if (cp < 0x80) {
        out += static_cast<char>(cp);
    } else if (cp < 0x800) {
        out += static_cast<char>(0xC0 | (cp >> 6));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        out += static_cast<char>(0xE0 | (cp >> 12));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp < 0x110000) {
        out += static_cast<char>(0xF0 | (cp >> 18));
        out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    }
}

std::string decodeEntities(const std::string& s) {
    if (s.find('&') == std::string::npos) return s;
    std::string out;
    for (size_t i = 0; i < s.size(); ++i) {
        const size_t semi = s[i] == '&' ? s.find(';', i) : std::string::npos;
        if (semi == std::string::npos || semi - i > 10) {
            out += s[i];
            continue;
        }
        const std::string name = s.substr(i + 1, semi - i - 1);
        if (name == "amp")
            out += '&';
        else if (name == "lt")
            out += '<';
        else if (name == "gt")
            out += '>';
        else if (name == "quot")
            out += '"';
        else if (name == "apos")
            out += '\'';
        else if (name.size() > 1 && name[0] == '#') {
            const bool hex = name[1] == 'x' || name[1] == 'X';
            appendUtf8(out, std::strtoul(name.c_str() + (hex ? 2 : 1), nullptr, hex ? 16 : 10));
        } else {
            out += s.substr(i, semi - i + 1);
        }
        i = semi;
    }
    return out;
}

class XmlParser {
public:
    explicit XmlParser(const std::string& xml) : s_(xml) {}

    bool parse(XmlElement& document) {
        std::vector<XmlElement*> stack{&document};
        while (pos_ < s_.size()) {
            const size_t lt = s_.find('<', pos_);
            const size_t textEnd = lt == std::string::npos ? s_.size() : lt;
            if (textEnd > pos_) stack.back()->text += decodeEntities(s_.substr(pos_, textEnd - pos_));
            if (lt == std::string::npos) break;
            pos_ = lt;
            if (startsWith("<!--")) {
                if (!skipPast("-->")) return false;
            } else if (startsWith("<![CDATA[")) {
                const size_t end = s_.find("]]>", pos_);
                if (end == std::string::npos) return false;
                stack.back()->text += s_.substr(pos_ + 9, end - pos_ - 9);
                pos_ = end + 3;
            } else if (startsWith("<?") || startsWith("<!")) {
                if (!skipPast(">")) return false;
            } else if (startsWith("</")) {
                if (!skipPast(">")) return false;
                if (stack.size() > 1) stack.pop_back();
            } else {
                XmlElement element;
                bool selfClosing = false;
                if (!parseTag(element, selfClosing)) return false;
                stack.back()->children.push_back(std::move(element));
                if (!selfClosing) stack.push_back(&stack.back()->children.back());
            }
        }
        return true;
    }

private:
    bool startsWith(const char* prefix) const { return s_.compare(pos_, std::strlen(prefix), prefix) == 0; }

    bool skipPast(const char* terminator) {
        const size_t end = s_.find(terminator, pos_);
        if (end == std::string::npos) return false;
        pos_ = end + std::strlen(terminator);
        return true;
    }

    void skipSpace() {
        while (pos_ < s_.size() && std::strchr(" \t\r\n", s_[pos_])) ++pos_;
    }

    std::string name() {
        const size_t start = pos_;
        while (pos_ < s_.size() && !std::strchr(" \t\r\n/>=", s_[pos_])) ++pos_;
        return s_.substr(start, pos_ - start);
    }

    bool parseTag(XmlElement& element, bool& selfClosing) {
        ++pos_;  // '<'
        element.name = name();
        if (element.name.empty()) return false;
        for (;;) {
            skipSpace();
            if (pos_ >= s_.size()) return false;
            if (s_[pos_] == '>') {
                ++pos_;
                return true;
            }
            if (startsWith("/>")) {
                pos_ += 2;
                selfClosing = true;
                return true;
            }
            const std::string key = name();
            if (key.empty()) return false;
            skipSpace();
            std::string value;
            if (pos_ < s_.size() && s_[pos_] == '=') {
                ++pos_;
                skipSpace();
                if (pos_ >= s_.size()) return false;
                const char quote = s_[pos_];
                if (quote == '"' || quote == '\'') {
                    const size_t end = s_.find(quote, pos_ + 1);
                    if (end == std::string::npos) return false;
                    value = decodeEntities(s_.substr(pos_ + 1, end - pos_ - 1));
                    pos_ = end + 1;
                } else {
                    value = name();
                }
            }
            element.attributes.emplace_back(key, value);
        }
    }

    const std::string& s_;
    size_t pos_ = 0;
};

// Depth first search for all elements called `name`.
void collect(const XmlElement& root, const char* name, std::vector<const XmlElement*>& out) {
    for (const XmlElement& child : root.children) {
        if (child.name == name) out.push_back(&child);
        collect(child, name, out);
    }
}

std::vector<const XmlElement*> all(const XmlElement& root, const char* name) {
    std::vector<const XmlElement*> out;
    collect(root, name, out);
    return out;
}

Style parseStyle(const XmlElement& e) {
    Style style;
    const std::string fg = trim(e.attributeOr("fgColor"));
    if (fg.size() == 6 && fg.find_first_not_of("0123456789abcdefABCDEF") == std::string::npos) {
        style.hasForeground = true;
        style.foreground = static_cast<uint32_t>(std::strtoul(fg.c_str(), nullptr, 16));
    }
    style.fontStyle = std::atoi(e.attributeOr("fontStyle", "0").c_str());
    return style;
}

// ---- languages ----------------------------------------------------------------------------------

// Common info strings of code blocks -> Notepad++ language name.
const std::pair<const char*, const char*> kAliases[] = {
    {"c", "c"},
    {"h", "c"},
    {"cpp", "cpp"},
    {"c++", "cpp"},
    {"cxx", "cpp"},
    {"cc", "cpp"},
    {"hpp", "cpp"},
    {"hxx", "cpp"},
    {"cs", "cs"},
    {"csharp", "cs"},
    {"c#", "cs"},
    {"java", "java"},
    {"js", "javascript.js"},
    {"javascript", "javascript.js"},
    {"jsx", "javascript.js"},
    {"mjs", "javascript.js"},
    {"cjs", "javascript.js"},
    {"node", "javascript.js"},
    {"ts", "typescript"},
    {"tsx", "typescript"},
    {"json", "json"},
    {"jsonc", "json"},
    {"json5", "json"},
    {"py", "python"},
    {"python3", "python"},
    {"py3", "python"},
    {"gyp", "python"},
    {"sh", "bash"},
    {"shell", "bash"},
    {"zsh", "bash"},
    {"ksh", "bash"},
    {"shellscript", "bash"},
    {"console", "bash"},
    {"ps", "powershell"},
    {"ps1", "powershell"},
    {"psm1", "powershell"},
    {"pwsh", "powershell"},
    {"posh", "powershell"},
    {"bat", "batch"},
    {"cmd", "batch"},
    {"dos", "batch"},
    {"batchfile", "batch"},
    {"mysql", "sql"},
    {"pgsql", "sql"},
    {"postgresql", "sql"},
    {"plsql", "sql"},
    {"sqlite", "sql"},
    {"tsql", "mssql"},
    {"t-sql", "mssql"},
    {"xaml", "xml"},
    {"xsd", "xml"},
    {"xsl", "xml"},
    {"xslt", "xml"},
    {"svg", "xml"},
    {"csproj", "xml"},
    {"plist", "xml"},
    {"wsdl", "xml"},
    {"htm", "html"},
    {"xhtml", "html"},
    {"vue", "html"},
    {"scss", "css"},
    {"less", "css"},
    {"yml", "yaml"},
    {"cfg", "ini"},
    {"conf", "ini"},
    {"properties", "props"},
    {"editorconfig", "ini"},
    {"gitconfig", "ini"},
    {"pl", "perl"},
    {"rb", "ruby"},
    {"rs", "rust"},
    {"golang", "go"},
    {"vbnet", "vb"},
    {"vb.net", "vb"},
    {"visualbasic", "vb"},
    {"vbs", "vb"},
    {"vbscript", "vb"},
    {"vba", "vb"},
    {"patch", "diff"},
    {"make", "makefile"},
    {"mk", "makefile"},
    {"tex", "latex"},
    {"f90", "fortran"},
    {"f95", "fortran"},
    {"f77", "fortran77"},
    {"delphi", "pascal"},
    {"pas", "pascal"},
    {"hs", "haskell"},
    {"erl", "erlang"},
    {"objectivec", "objc"},
    {"objective-c", "objc"},
    {"nasm", "asm"},
    {"masm", "asm"},
    {"x86asm", "asm"},
    {"el", "lisp"},
    {"elisp", "lisp"},
    {"clojure", "lisp"},
    {"scm", "scheme"},
    {"coffee", "coffeescript"},
    {"reg", "registry"},
    {"nsi", "nsis"},
    {"iss", "inno"},
    {"m", "matlab"},
    {"octave", "matlab"},
    {"jl", "julia"},
};

// Notepad++ language -> Lexilla lexer (where they differ).
const std::pair<const char*, const char*> kLexers[] = {
    {"c", "cpp"},          {"cs", "cpp"},           {"java", "cpp"},       {"javascript.js", "cpp"},
    {"typescript", "cpp"}, {"go", "cpp"},           {"swift", "cpp"},      {"rc", "cpp"},
    {"objc", "cpp"},       {"actionscript", "cpp"}, {"html", "hypertext"}, {"asp", "hypertext"},
    {"php", "phpscript"},  {"ini", "props"},        {"mssql", "mssql"},    {"fortran77", "f77"},
    {"scheme", "lisp"},    {"shell", "bash"},       {"javascript", "cpp"},
};

const char* lookup(const std::pair<const char*, const char*>* table, size_t count, const std::string& key) {
    for (size_t i = 0; i < count; ++i) {
        if (key == table[i].first) return table[i].second;
    }
    return nullptr;
}

int keywordClassIndex(const std::string& keywordClass) {
    static const char* const names[] = {"instre1", "instre2", "type1", "type2", "type3",
                                        "type4",   "type5",   "type6", "type7"};
    for (int i = 0; i < 9; ++i) {
        if (keywordClass == names[i]) return i;
    }
    if (keywordClass.size() == 1 && keywordClass[0] >= '0' && keywordClass[0] <= '8') return keywordClass[0] - '0';
    return -1;
}

}  // namespace

// ---- fenced code blocks --------------------------------------------------------------------------

std::string normalizeInfo(const std::string& info) {
    std::string word = trim(info);
    if (!word.empty() && word[0] == '{') word.erase(0, 1);
    while (!word.empty() && word[0] == '.') word.erase(0, 1);
    const size_t end = word.find_first_of(" \t{},=");
    if (end != std::string::npos) word.erase(end);
    word = toLower(word);
    if (word.compare(0, 9, "language-") == 0) word.erase(0, 9);
    return word;
}

std::vector<CodeBlock> findCodeBlocks(const std::string& text) {
    std::vector<CodeBlock> blocks;
    bool inBlock = false;
    char fenceChar = 0;
    size_t fenceLen = 0;
    CodeBlock current;

    size_t pos = 0;
    while (pos < text.size()) {
        size_t eol = text.find_first_of("\r\n", pos);
        if (eol == std::string::npos) eol = text.size();
        size_t next = eol;
        if (next < text.size())
            next += (text[next] == '\r' && next + 1 < text.size() && text[next + 1] == '\n') ? 2 : 1;

        // fences may be indented (nested in list items)
        size_t i = pos;
        while (i < eol && isBlank(text[i])) ++i;
        size_t j = i;
        if (i < eol && (text[i] == '`' || text[i] == '~')) {
            while (j < eol && text[j] == text[i]) ++j;
        }
        const size_t length = j - i;
        if (length >= 3) {
            const std::string rest = text.substr(j, eol - j);
            if (!inBlock) {
                if (text[i] == '~' || rest.find('`') == std::string::npos) {
                    inBlock = true;
                    fenceChar = text[i];
                    fenceLen = length;
                    current = CodeBlock();
                    current.bodyStart = next;
                    current.language = normalizeInfo(rest);
                }
            } else if (text[i] == fenceChar && length >= fenceLen && trim(rest).empty()) {
                inBlock = false;
                current.bodyEnd = pos;
                blocks.push_back(current);
            }
        }
        pos = next;
    }
    if (inBlock) {  // unclosed: the block runs to the end of the document
        current.bodyEnd = text.size();
        if (current.bodyStart > current.bodyEnd) current.bodyStart = current.bodyEnd;
        blocks.push_back(current);
    }
    return blocks;
}

// ---- XML ---------------------------------------------------------------------------------------

const std::string* XmlElement::attribute(const std::string& key) const {
    for (const auto& a : attributes) {
        if (a.first == key) return &a.second;
    }
    return nullptr;
}

std::string XmlElement::attributeOr(const std::string& key, const std::string& fallback) const {
    const std::string* value = attribute(key);
    return value ? *value : fallback;
}

bool parseXml(const std::string& xml, XmlElement& document) {
    document = XmlElement();
    size_t start = 0;
    if (xml.compare(0, 3, "\xEF\xBB\xBF") == 0) start = 3;
    return XmlParser(start ? xml.substr(start) : xml).parse(document);
}

// ---- Notepad++ configuration -------------------------------------------------------------------

Theme parseTheme(const std::string& xml) {
    Theme theme;
    XmlElement doc;
    if (!parseXml(xml, doc)) return theme;
    for (const XmlElement* widget : all(doc, "WidgetStyle")) {
        if (widget->attributeOr("styleID") == "32" || widget->attributeOr("name") == "Default Style") {
            theme.defaultStyle = parseStyle(*widget);
            break;
        }
    }
    for (const XmlElement* lexer : all(doc, "LexerType")) {
        const std::string name = lexer->attributeOr("name");
        auto& styles = theme.styles[name];
        for (const XmlElement& words : lexer->children) {
            if (words.name != "WordsStyle") continue;
            const std::string* id = words.attribute("styleID");
            if (!id || id->empty()) continue;
            const int styleId = std::atoi(id->c_str());
            styles[styleId] = parseStyle(words);
            const std::string keywordClass = words.attributeOr("keywordClass");
            if (keywordClass.empty()) continue;
            theme.keywordStyles[name][keywordClass] = styleId;
            const std::string userWords = trim(words.text);
            if (!userWords.empty()) theme.keywords[name][keywordClass] = userWords;
        }
    }
    return theme;
}

std::map<std::string, Language> parseLanguages(const std::string& langsXml) {
    std::map<std::string, Language> languages;
    XmlElement doc;
    if (!parseXml(langsXml, doc)) return languages;
    for (const XmlElement* element : all(doc, "Language")) {
        Language& language = languages[toLower(element->attributeOr("name"))];
        const std::string ext = toLower(element->attributeOr("ext"));
        size_t pos = 0;
        while (pos < ext.size()) {
            const size_t start = ext.find_first_not_of(' ', pos);
            if (start == std::string::npos) break;
            size_t end = ext.find(' ', start);
            if (end == std::string::npos) end = ext.size();
            language.extensions.push_back(ext.substr(start, end - start));
            pos = end;
        }
        for (const XmlElement& keywords : element->children) {
            if (keywords.name == "Keywords") language.keywords[keywords.attributeOr("name")] = trim(keywords.text);
        }
    }
    return languages;
}

NppConfig parseConfig(const std::string& configXml) {
    NppConfig config;
    XmlElement doc;
    if (!parseXml(configXml, doc)) return config;
    for (const XmlElement* gui : all(doc, "GUIConfig")) {
        const std::string name = gui->attributeOr("name");
        if (name == "stylerTheme") {
            config.stylerTheme = gui->attributeOr("path");
        } else if (name == "DarkMode") {
            config.hasDarkModeEntry = true;
            config.darkModeEnabled = gui->attributeOr("enable") == "yes";
            config.darkThemeName = gui->attributeOr("darkThemeName");
            config.lightThemeName = gui->attributeOr("lightThemeName");
        }
    }
    return config;
}

// ---- languages ----------------------------------------------------------------------------------

std::string resolveLanguage(const std::string& info, const std::map<std::string, Language>& languages) {
    const std::string word = normalizeInfo(info);
    if (word.empty()) return {};
    if (const char* alias = lookup(kAliases, sizeof(kAliases) / sizeof(kAliases[0]), word)) {
        if (languages.empty() || languages.count(alias)) return alias;
    }
    if (languages.count(word)) return word;
    if (languages.empty()) return {};
    for (const auto& entry : languages) {
        const auto& exts = entry.second.extensions;
        if (std::find(exts.begin(), exts.end(), word) != exts.end()) return entry.first;
    }
    return {};
}

std::string lexerFor(const std::string& language) {
    const char* lexer = lookup(kLexers, sizeof(kLexers) / sizeof(kLexers[0]), language);
    return lexer ? lexer : language;
}

std::vector<std::string> styleSourcesFor(const std::string& language) {
    if (language == "html" || language == "asp") return {language, "javascript"};
    return {language};
}

int keywordSetFor(const std::string& lexer, const std::string& keywordClass, int styleId) {
    if (lexer == "cpp") {
        // C like languages: the list follows the style it colors
        switch (styleId) {
            case 5:
                return 0;  // instruction words
            case 16:
                return 1;  // type words
            case 17:
                return 2;  // doc comment keywords
            case 19:
                return 3;  // global classes
            default:
                break;
        }
        if (keywordClass == "instre1") return 0;
        if (keywordClass == "type1") return 1;
        return -1;
    }
    if (lexer == "phpscript") return keywordClass == "instre1" ? 4 : -1;  // the HTML lexer keeps PHP words in list 4
    return keywordClassIndex(keywordClass);
}

}  // namespace syntax
