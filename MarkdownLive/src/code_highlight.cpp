#include "code_highlight.h"

#include <functional>

#include "editor.h"
#include "npp_api.h"
#include "plugin.h"
#include "util.h"

namespace {

constexpr int kFallbackIndicator = 13;           // used if Notepad++ cannot allocate one (< 8.5.6)
constexpr intptr_t kMaxDocumentBytes = 8 * 1024 * 1024;

using DirectFunction = intptr_t (*)(intptr_t, unsigned int, uintptr_t, intptr_t);

// Invisible parent of the lexer view: its notifications must not reach Notepad++, which would
// take them for changes of a document.
HWND createHostWindow() {
    static const wchar_t kClass[] = L"MarkdownLiveLexerHost";
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = g_module;
    wc.lpszClassName = kClass;
    RegisterClassExW(&wc);
    return CreateWindowExW(WS_EX_TOOLWINDOW, kClass, L"", WS_POPUP, 0, 0, 0, 0, nullptr, nullptr, g_module, nullptr);
}

int toScintillaColor(uint32_t rgb) {
    return static_cast<int>(((rgb & 0xFF) << 16) | (rgb & 0xFF00) | ((rgb >> 16) & 0xFF));
}

std::wstring parentDirectory(const std::wstring& path) {
    std::wstring dir = path;
    while (!dir.empty() && (dir.back() == L'\\' || dir.back() == L'/')) dir.pop_back();
    return directoryOf(dir);
}

std::wstring nppDirectory() {
    wchar_t path[MAX_PATH] = {0};
    SendMessageW(g_npp.nppHandle, NPPM_GETNPPDIRECTORY, MAX_PATH, reinterpret_cast<LPARAM>(path));
    return path;
}

std::wstring firstExisting(const std::vector<std::wstring>& candidates) {
    for (const auto& path : candidates) {
        if (!path.empty() && fileExists(path)) return path;
    }
    return {};
}

// Theme file of the running Notepad++. config.xml is written on exit, so it describes the state
// at the start of the session: if dark mode was switched since, the theme belonging to the new
// mode is used.
std::wstring activeThemePath(const std::wstring& settingsDir, const std::wstring& nppDir) {
    const syntax::NppConfig config = syntax::parseConfig(readFileBytes(settingsDir + L"\\config.xml"));
    const bool dark = isDarkMode();
    std::vector<std::wstring> candidates;
    if (!config.hasDarkModeEntry || config.darkModeEnabled == dark) {
        if (!config.stylerTheme.empty()) candidates.push_back(toWide(config.stylerTheme, CP_UTF8));
    } else {
        std::string name = dark ? config.darkThemeName : config.lightThemeName;
        if (dark && name.empty()) name = "DarkModeDefault.xml";
        if (!name.empty() && name != "stylers.xml") {
            const std::wstring file = toWide(name, CP_UTF8);
            candidates.push_back(settingsDir + L"\\themes\\" + file);
            candidates.push_back(nppDir + L"\\themes\\" + file);
        }
    }
    candidates.push_back(settingsDir + L"\\stylers.xml");
    candidates.push_back(nppDir + L"\\stylers.xml");
    candidates.push_back(nppDir + L"\\stylers.model.xml");
    return firstExisting(candidates);
}

}  // namespace

CodeHighlighter& CodeHighlighter::instance() {
    static CodeHighlighter highlighter;
    return highlighter;
}

// =================================================================================================
// lifecycle
// =================================================================================================

void CodeHighlighter::init() {
    if (ready_) return;
    ready_ = true;
    int first = 0;
    indicator_ = SendMessageW(g_npp.nppHandle, NPPM_ALLOCATEINDICATOR, 1, reinterpret_cast<LPARAM>(&first)) && first > 0
                     ? first
                     : kFallbackIndicator;
    for (const HWND view : {g_npp.scintillaMainHandle, g_npp.scintillaSecondHandle}) {
        SendMessageW(view, SCI_INDICSETSTYLE, static_cast<WPARAM>(indicator_), INDIC_TEXTFORE);
        SendMessageW(view, SCI_INDICSETFLAGS, static_cast<WPARAM>(indicator_), SC_INDICFLAG_VALUEFORE);
    }
    update();
}

void CodeHighlighter::setEnabled(bool enabled) {
    enabled_ = enabled;
    update();
}

void CodeHighlighter::onBufferActivated() { update(); }

void CodeHighlighter::onTextChanged() {
    if (!ready_ || !enabled_) return;
    const intptr_t length = Editor::current().length();
    const UINT delay = length < 200 * 1024 ? 150 : length < 1024 * 1024 ? 400 : 1000;
    if (timer_) KillTimer(nullptr, timer_);
    timer_ = SetTimer(nullptr, 0, delay, timerProc);
}

void CALLBACK CodeHighlighter::timerProc(HWND, UINT, UINT_PTR, DWORD) {
    CodeHighlighter& self = instance();
    if (self.timer_) KillTimer(nullptr, self.timer_);
    self.timer_ = 0;
    self.update();
}

void CodeHighlighter::onStylesChanged() {
    configLoaded_ = false;
    languageInfo_.clear();
    lexerLanguage_.clear();
    for (auto& entry : highlighted_) entry.second.clear();  // everything has to be colored again
    update();
}

void CodeHighlighter::onBufferClosed(intptr_t bufferId) { highlighted_.erase(bufferId); }

void CodeHighlighter::shutdown() {
    if (timer_) KillTimer(nullptr, timer_);
    timer_ = 0;
    ready_ = false;
    // the lexer view belongs to Notepad++, which destroys it on exit
}

// =================================================================================================
// configuration
// =================================================================================================

void CodeHighlighter::loadConfiguration() {
    configLoaded_ = true;
    // <settings>\plugins\Config -> <settings> (%APPDATA%\Notepad++ or the portable folder)
    const std::wstring settingsDir = parentDirectory(parentDirectory(configDirectory()));
    const std::wstring nppDir = nppDirectory();
    languages_ = syntax::parseLanguages(readFileBytes(
        firstExisting({settingsDir + L"\\langs.xml", nppDir + L"\\langs.xml", nppDir + L"\\langs.model.xml"})));
    const std::wstring themePath = activeThemePath(settingsDir, nppDir);
    theme_ = themePath.empty() ? syntax::Theme() : syntax::parseTheme(readFileBytes(themePath));
}

CodeHighlighter::LanguageInfo* CodeHighlighter::language(const std::string& name) {
    const auto found = languageInfo_.find(name);
    if (found != languageInfo_.end()) return found->second.supported ? &found->second : nullptr;

    LanguageInfo& info = languageInfo_[name];
    info.lexer = syntax::lexerFor(name);

    // colors: the language's own styles first, then the ones of embedded languages
    std::map<int, syntax::Style> styles;
    for (const std::string& source : syntax::styleSourcesFor(name)) {
        const auto lexer = theme_.styles.find(source);
        if (lexer == theme_.styles.end()) continue;
        for (const auto& style : lexer->second) styles.insert(style);
    }
    if (styles.empty()) {
        info.supported = false;  // the theme does not know the language
        return nullptr;
    }
    const auto base = styles.find(0);
    if (base != styles.end() && base->second.hasForeground) info.defaultColor = toScintillaColor(base->second.foreground);
    else if (theme_.defaultStyle.hasForeground) info.defaultColor = toScintillaColor(theme_.defaultStyle.foreground);
    for (const auto& style : styles) {
        if (style.second.hasForeground) info.colors[style.first] = toScintillaColor(style.second.foreground);
    }

    // keywords from langs.xml plus the user defined ones of the theme
    std::map<std::string, std::string> words;
    const auto lang = languages_.find(name);
    if (lang != languages_.end()) words = lang->second.keywords;
    const auto user = theme_.keywords.find(name);
    if (user != theme_.keywords.end()) {
        for (const auto& entry : user->second) words[entry.first] += " " + entry.second;
    }
    const auto keywordStyles = theme_.keywordStyles.find(name);
    std::map<int, std::string> lists;
    for (const auto& entry : words) {
        int styleId = -1;
        if (keywordStyles != theme_.keywordStyles.end()) {
            const auto style = keywordStyles->second.find(entry.first);
            if (style != keywordStyles->second.end()) styleId = style->second;
        }
        const int set = syntax::keywordSetFor(info.lexer, entry.first, styleId);
        if (set >= 0 && !entry.second.empty()) lists[set] += " " + entry.second;
    }
    if (info.lexer == "hypertext") {  // scripts embedded in HTML
        const auto js = languages_.find("javascript");
        if (js != languages_.end() && js->second.keywords.count("instre1")) lists[1] += " " + js->second.keywords.at("instre1");
    }
    info.keywords.assign(lists.begin(), lists.end());
    return &info;
}

// =================================================================================================
// highlighting
// =================================================================================================

void CodeHighlighter::clearRange(const Editor& editor, intptr_t start, intptr_t end) const {
    if (end > start) editor.call(SCI_INDICATORCLEARRANGE, static_cast<uintptr_t>(start), end - start);
}

void CodeHighlighter::update() {
    if (!ready_) return;
    if (timer_) KillTimer(nullptr, timer_);
    timer_ = 0;

    const Editor editor = Editor::current();
    const intptr_t buffer = currentBufferId();
    const std::wstring path = currentFilePath();
    const bool applies = enabled_ && (isMarkdownPath(path) || extensionOf(path).empty()) && editor.length() <= kMaxDocumentBytes;
    editor.call(SCI_SETINDICATORCURRENT, static_cast<uintptr_t>(indicator_));
    if (!applies) {
        if (highlighted_.erase(buffer)) clearRange(editor, 0, editor.length());
        return;
    }
    if (!configLoaded_) loadConfiguration();

    const std::string text = editor.text();
    std::unordered_set<size_t>& done = highlighted_[buffer];
    std::unordered_set<size_t> next;
    intptr_t pos = 0;
    for (const syntax::CodeBlock& block : syntax::findCodeBlocks(text)) {
        if (block.bodyEnd <= block.bodyStart) continue;
        const std::string name = syntax::resolveLanguage(block.language, languages_);
        LanguageInfo* info = name.empty() ? nullptr : language(name);
        if (!info) continue;  // left uncolored like any other text

        const auto start = static_cast<intptr_t>(block.bodyStart);
        const auto end = static_cast<intptr_t>(block.bodyEnd);
        clearRange(editor, pos, start);
        pos = end;
        const std::string code = text.substr(block.bodyStart, block.bodyEnd - block.bodyStart);
        const size_t key = std::hash<std::string>()(name + '\n' + code);
        // unchanged blocks keep their colors: Scintilla moves indicators along with the text
        if (done.count(key) && editor.call(SCI_INDICATORVALUEAT, static_cast<uintptr_t>(indicator_), start) != 0 &&
            editor.call(SCI_INDICATORVALUEAT, static_cast<uintptr_t>(indicator_), end - 1) != 0) {
            next.insert(key);
            continue;
        }
        clearRange(editor, start, end);
        if (colorize(editor, start, code, name, *info)) next.insert(key);
        editor.call(SCI_SETINDICATORCURRENT, static_cast<uintptr_t>(indicator_));
    }
    clearRange(editor, pos, static_cast<intptr_t>(text.size()));
    done = std::move(next);
}

bool CodeHighlighter::colorize(const Editor& editor, intptr_t start, const std::string& code, const std::string& languageName,
                               LanguageInfo& info) {
    if (!lexerView_) {
        if (!hostWindow_) hostWindow_ = createHostWindow();
        if (!hostWindow_) return false;
        lexerView_ = reinterpret_cast<HWND>(
            SendMessageW(g_npp.nppHandle, NPPM_CREATESCINTILLAHANDLE, 0, reinterpret_cast<LPARAM>(hostWindow_)));
        if (!lexerView_) return false;
        SendMessageW(lexerView_, SCI_SETUNDOCOLLECTION, 0, 0);
    }
    if (lexerLanguage_ != languageName) {
        void* lexer = reinterpret_cast<void*>(
            SendMessageW(g_npp.nppHandle, NPPM_CREATELEXER, 0, reinterpret_cast<LPARAM>(toWide(info.lexer, CP_UTF8).c_str())));
        if (!lexer) {
            info.supported = false;  // Notepad++ < 8.4 or unknown lexer
            return false;
        }
        SendMessageW(lexerView_, SCI_SETILEXER, 0, reinterpret_cast<LPARAM>(lexer));
        for (const auto& list : info.keywords) {
            SendMessageW(lexerView_, SCI_SETKEYWORDS, static_cast<WPARAM>(list.first), reinterpret_cast<LPARAM>(list.second.c_str()));
        }
        lexerLanguage_ = languageName;
    }

    const auto direct = reinterpret_cast<DirectFunction>(SendMessageW(lexerView_, SCI_GETDIRECTFUNCTION, 0, 0));
    const auto pointer = static_cast<intptr_t>(SendMessageW(lexerView_, SCI_GETDIRECTPOINTER, 0, 0));
    if (!direct || !pointer) return false;
    direct(pointer, SCI_SETCODEPAGE, static_cast<uintptr_t>(editor.call(SCI_GETCODEPAGE)), 0);
    direct(pointer, SCI_CLEARALL, 0, 0);
    direct(pointer, SCI_APPENDTEXT, code.size(), reinterpret_cast<intptr_t>(code.data()));
    direct(pointer, SCI_COLOURISE, 0, -1);

    editor.call(SCI_SETINDICATORCURRENT, static_cast<uintptr_t>(indicator_));
    const auto length = static_cast<intptr_t>(code.size());
    intptr_t runStart = 0;
    int runColor = -1;
    for (intptr_t i = 0; i <= length; ++i) {
        int color = -1;
        if (i < length) {
            const int style = static_cast<int>(direct(pointer, SCI_GETSTYLEAT, static_cast<uintptr_t>(i), 0) & 0xFF);
            const auto found = info.colors.find(style);
            color = found != info.colors.end() ? found->second : info.defaultColor;
            if (i > 0 && color == runColor) continue;
        }
        if (i > 0 && runColor >= 0) {
            editor.call(SCI_SETINDICATORVALUE, static_cast<uintptr_t>(SC_INDICVALUEBIT | runColor));
            editor.call(SCI_INDICATORFILLRANGE, static_cast<uintptr_t>(start + runStart), i - runStart);
        }
        runStart = i;
        runColor = color;
    }
    direct(pointer, SCI_CLEARALL, 0, 0);
    return true;
}
