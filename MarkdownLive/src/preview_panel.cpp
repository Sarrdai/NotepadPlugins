#include "preview_panel.h"

#include <commdlg.h>
#include <objbase.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shlwapi.h>

#include <atomic>
#include <cwchar>
#include <deque>
#include <functional>
#include <utility>

#include <WebView2.h>

#include "commands.h"
#include "editor.h"
#include "npp_api.h"
#include "plugin.h"
#include "util.h"

namespace {

constexpr wchar_t kAssetHost[] = L"mdlive.assets";
constexpr wchar_t kAssetOrigin[] = L"https://mdlive.assets/";
constexpr wchar_t kPageUrl[] = L"https://mdlive.assets/preview.html";
constexpr wchar_t kFileUrlPrefix[] = L"https://mdlive.file/";
constexpr wchar_t kRuntimeHint[] =
    L"The Microsoft Edge WebView2 Runtime is required for the preview.\n\n"
    L"It is part of Windows 11 and of up-to-date Windows 10 systems. If it is missing, install the "
    L"\"Evergreen Runtime\" from\nhttps://developer.microsoft.com/microsoft-edge/webview2/";

constexpr UINT_PTR kRenderTimer = 1;
constexpr UINT WM_APP_WEBMESSAGE = WM_APP + 1;
constexpr intptr_t kMaxPreviewBytes = 8 * 1024 * 1024;

// Interface ids (WebView2.h only declares them via __uuidof, which is not portable).
const IID kIID_EnvironmentCompleted = {0x4e8a3389, 0xc9d8, 0x4bd2, {0xb6, 0xb5, 0x12, 0x4f, 0xee, 0x6c, 0xc1, 0x4d}};
const IID kIID_ControllerCompleted = {0x6c4819f3, 0xc9b7, 0x4260, {0x81, 0x27, 0xc9, 0xf5, 0xbd, 0xe7, 0xf6, 0x8c}};
const IID kIID_WebMessageReceived = {0x57213f19, 0x00e6, 0x49fa, {0x8e, 0x07, 0x89, 0x8e, 0xa0, 0x1e, 0xcb, 0xd2}};
const IID kIID_NavigationStarting = {0x9adbe429, 0xf36d, 0x432b, {0x9d, 0xdc, 0xf8, 0x88, 0x1f, 0xbd, 0x76, 0xe3}};
const IID kIID_NewWindowRequested = {0xd4c185fe, 0xc81c, 0x4989, {0x97, 0xaf, 0x2d, 0x3f, 0xa7, 0xab, 0x56, 0x51}};
const IID kIID_WebResourceRequested = {0xab00b74c, 0x15f1, 0x4646, {0x80, 0xe8, 0xe7, 0x63, 0x41, 0xd2, 0x5d, 0x71}};
const IID kIID_PrintToPdfCompleted = {0xccf1ef04, 0xfd8e, 0x4d5f, {0xb2, 0xde, 0x09, 0x83, 0xe4, 0x1b, 0x8c, 0x36}};
const IID kIID_WebView2_3 = {0xA0D6DF20, 0x3B92, 0x416D, {0xAA, 0x0C, 0x43, 0x7A, 0x9C, 0x72, 0x78, 0x57}};
const IID kIID_WebView2_7 = {0x79c24d83, 0x09a3, 0x45ae, {0x94, 0x18, 0x48, 0x7f, 0x32, 0xa5, 0x87, 0x40}};
const IID kIID_Controller2 = {0xc979903e, 0xd4ca, 0x4228, {0x92, 0xeb, 0x47, 0xee, 0x3f, 0xa9, 0x6e, 0xab}};
const IID kIID_Environment6 = {0xe59ee362, 0xacbd, 0x4857, {0x9a, 0x8e, 0xd3, 0x64, 0x4d, 0x94, 0x59, 0xa9}};

// Generic COM callback object wrapping a std::function.
template <typename Interface, typename... Args>
class Handler final : public Interface {
public:
    using Function = std::function<HRESULT(Args...)>;

    Handler(const IID& iid, Function fn) : iid_(iid), fn_(std::move(fn)) {}

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** object) override {
        if (!object) return E_POINTER;
        if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, iid_)) {
            *object = static_cast<Interface*>(this);
            AddRef();
            return S_OK;
        }
        *object = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs_; }
    ULONG STDMETHODCALLTYPE Release() override {
        const ULONG refs = --refs_;
        if (refs == 0) delete this;
        return refs;
    }
    HRESULT STDMETHODCALLTYPE Invoke(Args... args) override { return fn_(args...); }

private:
    ~Handler() = default;

    IID iid_;
    Function fn_;
    std::atomic<ULONG> refs_{1};
};

using EnvironmentCompleted =
    Handler<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler, HRESULT, ICoreWebView2Environment*>;
using ControllerCompleted =
    Handler<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler, HRESULT, ICoreWebView2Controller*>;
using WebMessageReceived =
    Handler<ICoreWebView2WebMessageReceivedEventHandler, ICoreWebView2*, ICoreWebView2WebMessageReceivedEventArgs*>;
using NavigationStarting =
    Handler<ICoreWebView2NavigationStartingEventHandler, ICoreWebView2*, ICoreWebView2NavigationStartingEventArgs*>;
using NewWindowRequested =
    Handler<ICoreWebView2NewWindowRequestedEventHandler, ICoreWebView2*, ICoreWebView2NewWindowRequestedEventArgs*>;
using WebResourceRequested =
    Handler<ICoreWebView2WebResourceRequestedEventHandler, ICoreWebView2*, ICoreWebView2WebResourceRequestedEventArgs*>;
using PrintToPdfCompleted = Handler<ICoreWebView2PrintToPdfCompletedHandler, HRESULT, BOOL>;

typedef HRESULT(STDAPICALLTYPE* CreateEnvironmentFn)(PCWSTR browserExecutableFolder, PCWSTR userDataFolder,
                                                     ICoreWebView2EnvironmentOptions* options,
                                                     ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler* handler);

template <typename T>
void safeRelease(T*& pointer) {
    if (pointer) {
        pointer->Release();
        pointer = nullptr;
    }
}

// Takes ownership of a CoTaskMem string returned by WebView2.
std::wstring takeString(LPWSTR value) {
    std::wstring result = value ? value : L"";
    CoTaskMemFree(value);
    return result;
}

std::deque<std::wstring> g_webMessages;  // messages from the page, handled outside the WebView2 callback

DockedWidgetData g_dockData;
wchar_t g_dockModuleName[MAX_PATH];
wchar_t g_dockAddInfo[MAX_PATH];  // caption suffix, e.g. the name of the pinned document

// Scintilla view that shows `buffer`, nullptr if it is not the visible tab of either view.
HWND viewShowing(intptr_t buffer) {
    if (currentBufferId() == buffer) return Editor::currentHandle();
    const auto current = SendMessageW(g_npp.nppHandle, NPPM_GETCURRENTVIEW, 0, 0);
    const int other = current == MAIN_VIEW ? SUB_VIEW : MAIN_VIEW;
    const LRESULT index = SendMessageW(g_npp.nppHandle, NPPM_GETCURRENTDOCINDEX, 0, other);
    if (index < 0 || SendMessageW(g_npp.nppHandle, NPPM_GETBUFFERIDFROMPOS, static_cast<WPARAM>(index), other) != buffer) {
        return nullptr;
    }
    return other == MAIN_VIEW ? g_npp.scintillaMainHandle : g_npp.scintillaSecondHandle;
}

bool isWebUrl(const std::wstring& url) {
    return startsWithNoCase(url, L"http://") || startsWithNoCase(url, L"https://") || startsWithNoCase(url, L"mailto:");
}

void openExternal(const std::wstring& url) {
    if (isWebUrl(url)) ShellExecuteW(g_npp.nppHandle, L"open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

const wchar_t* imageMimeType(const std::wstring& ext) {
    if (ext == L"png") return L"image/png";
    if (ext == L"jpg" || ext == L"jpeg") return L"image/jpeg";
    if (ext == L"gif") return L"image/gif";
    if (ext == L"svg") return L"image/svg+xml";
    if (ext == L"webp") return L"image/webp";
    if (ext == L"bmp") return L"image/bmp";
    if (ext == L"ico") return L"image/x-icon";
    if (ext == L"avif") return L"image/avif";
    return nullptr;
}

std::wstring saveFileDialog(const std::wstring& suggestedName, const wchar_t* filter, const wchar_t* extension,
                            const std::wstring& initialDir) {
    wchar_t file[MAX_PATH] = {0};
    wcsncpy(file, suggestedName.c_str(), MAX_PATH - 1);
    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = g_npp.nppHandle;
    ofn.lpstrFilter = filter;
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrDefExt = extension;
    ofn.lpstrInitialDir = initialDir.empty() ? nullptr : initialDir.c_str();
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY | OFN_NOCHANGEDIR;
    return GetSaveFileNameW(&ofn) ? std::wstring(file) : std::wstring();
}

}  // namespace

PreviewPanel& PreviewPanel::instance() {
    static PreviewPanel panel;
    return panel;
}

// =================================================================================================
// window
// =================================================================================================

bool PreviewPanel::createWindow() {
    // In-memory dialog template: an empty child dialog (no menu, class or title).
    alignas(DWORD) BYTE buffer[sizeof(DLGTEMPLATE) + 4 * sizeof(WORD)] = {};
    auto* dialog = reinterpret_cast<DLGTEMPLATE*>(buffer);
    dialog->style = WS_CHILD | WS_CLIPCHILDREN;
    dialog->cx = 220;
    dialog->cy = 300;
    hwnd_ = CreateDialogIndirectParamW(g_module, dialog, g_npp.nppHandle, dialogProc, 0);
    return hwnd_ != nullptr;
}

INT_PTR CALLBACK PreviewPanel::dialogProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    PreviewPanel& self = instance();
    switch (message) {
        case WM_SIZE:
            self.resizeWebView();
            InvalidateRect(hwnd, nullptr, TRUE);
            return TRUE;

        case WM_MOVE:
            if (self.controller_) self.controller_->NotifyParentWindowPositionChanged();
            return FALSE;

        case WM_SHOWWINDOW:
            if (self.controller_) self.controller_->put_IsVisible(wParam ? TRUE : FALSE);
            if (wParam && self.dirty_) self.scheduleRender();
            return FALSE;

        case WM_SETFOCUS:
            if (self.controller_) self.controller_->MoveFocus(COREWEBVIEW2_MOVE_FOCUS_REASON_PROGRAMMATIC);
            return TRUE;

        case WM_ERASEBKGND:
            return TRUE;

        case WM_PAINT:
            self.paint();
            return TRUE;

        case WM_TIMER:
            if (wParam == kRenderTimer) {
                KillTimer(hwnd, kRenderTimer);
                self.render(false);
            }
            return TRUE;

        case WM_APP_WEBMESSAGE:
            while (!g_webMessages.empty()) {
                const std::wstring text = std::move(g_webMessages.front());
                g_webMessages.pop_front();
                self.onWebMessage(text);
            }
            return TRUE;

        case WM_NOTIFY: {
            const auto* header = reinterpret_cast<const NMHDR*>(lParam);
            if (header && header->hwndFrom == g_npp.nppHandle && LOWORD(header->code) == DMN_CLOSE) {
                // closed with the "x" of the docking container
                self.visible_ = false;
                setMenuCheck(MenuTogglePreview, false);
                self.setPinned(false);
            }
            return FALSE;
        }
    }
    return FALSE;
}

void PreviewPanel::paint() {
    PAINTSTRUCT ps;
    const HDC dc = BeginPaint(hwnd_, &ps);
    RECT rect;
    GetClientRect(hwnd_, &rect);
    const bool dark = isDarkMode();
    const HBRUSH brush = CreateSolidBrush(dark ? RGB(13, 17, 23) : RGB(255, 255, 255));
    FillRect(dc, &rect, brush);
    DeleteObject(brush);
    if (!status_.empty()) {
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, dark ? RGB(145, 152, 161) : RGB(89, 99, 110));
        const HGDIOBJ oldFont = SelectObject(dc, GetStockObject(DEFAULT_GUI_FONT));
        InflateRect(&rect, -16, -16);
        DrawTextW(dc, status_.c_str(), -1, &rect, DT_LEFT | DT_TOP | DT_WORDBREAK | DT_EDITCONTROL | DT_NOPREFIX);
        SelectObject(dc, oldFont);
    }
    EndPaint(hwnd_, &ps);
}

void PreviewPanel::setStatus(const std::wstring& text) {
    status_ = text;
    if (hwnd_) InvalidateRect(hwnd_, nullptr, TRUE);
}

void PreviewPanel::resizeWebView() {
    if (!controller_ || !hwnd_) return;
    RECT bounds;
    GetClientRect(hwnd_, &bounds);
    controller_->put_Bounds(bounds);
}

// =================================================================================================
// show / hide
// =================================================================================================

void PreviewPanel::toggle() {
    if (visible_) hide();
    else show();
}

void PreviewPanel::show() {
    if (!hwnd_ && !createWindow()) return;

    if (!registered_) {
        wchar_t modulePath[MAX_PATH] = {0};
        GetModuleFileNameW(g_module, modulePath, MAX_PATH);
        wcsncpy(g_dockModuleName, fileNameOf(modulePath).c_str(), MAX_PATH - 1);
        tabIcon_ = createPluginIcon(16, isDarkMode());

        g_dockData.hClient = hwnd_;
        g_dockData.pszName = L"Markdown Preview";
        g_dockData.dlgID = MenuTogglePreview;
        g_dockData.uMask = DWS_DF_CONT_RIGHT | DWS_ICONTAB | DWS_ADDINFO | DWS_USEOWNDARKMODE;
        g_dockData.hIconTab = tabIcon_;
        g_dockData.pszAddInfo = g_dockAddInfo;
        g_dockData.pszModuleName = g_dockModuleName;
        SendMessageW(g_npp.nppHandle, NPPM_DMMREGASDCKDLG, 0, reinterpret_cast<LPARAM>(&g_dockData));
        registered_ = true;
    }

    SendMessageW(g_npp.nppHandle, NPPM_DMMSHOW, 0, reinterpret_cast<LPARAM>(hwnd_));
    visible_ = true;
    setMenuCheck(MenuTogglePreview, true);

    if (!webViewRequested_) initWebView();
    dirty_ = true;
    pendingReset_ = true;
    render(true);
}

void PreviewPanel::hide() {
    if (!hwnd_ || !visible_) return;
    SendMessageW(g_npp.nppHandle, NPPM_DMMHIDE, 0, reinterpret_cast<LPARAM>(hwnd_));
    visible_ = false;
    setMenuCheck(MenuTogglePreview, false);
    setPinned(false);  // a pin only lives as long as the panel is open
}

// =================================================================================================
// pinning
// =================================================================================================

void PreviewPanel::togglePin() {
    if (pinned_) {
        setPinned(false);
        render(true);  // back to the current document
        return;
    }
    if (!visible_) show();
    if (!visible_) return;
    pinnedBuffer_ = currentBufferId();
    const Editor ed = Editor::current();
    setPinned(true);
    pinnedText_ = toWide(ed.text(), ed.codePage());  // in case the page is not ready to render yet
    dirty_ = true;
    render(false);
}

void PreviewPanel::setPinned(bool pinned) {
    if (!pinned) pinnedText_.clear();
    if (pinned_ == pinned) return;
    pinned_ = pinned;
    setMenuCheck(MenuPinPreview, pinned);
    sendPinState();
    updateCaption();
}

void PreviewPanel::sendPinState() {
    post(std::wstring(L"{\"type\":\"pin\",\"pinned\":") + (pinned_ ? L"true" : L"false") +
         L",\"name\":" + jsonString(pinned_ ? fileNameOf(sourcePath()) : std::wstring()) + L"}");
}

void PreviewPanel::updateCaption() {
    const std::wstring info = pinned_ ? L"pinned: " + fileNameOf(sourcePath()) : std::wstring();
    if (info == g_dockAddInfo || !registered_) return;
    wcsncpy(g_dockAddInfo, info.c_str(), MAX_PATH - 1);
    SendMessageW(g_npp.nppHandle, NPPM_DMMUPDATEDISPINFO, 0, reinterpret_cast<LPARAM>(hwnd_));
}

std::wstring PreviewPanel::sourcePath() const { return pinned_ ? bufferFilePath(pinnedBuffer_) : currentFilePath(); }

HWND PreviewPanel::sourceView() const { return pinned_ ? viewShowing(pinnedBuffer_) : Editor::currentHandle(); }

HWND PreviewPanel::activateSource() {
    if (const HWND view = sourceView()) return view;
    // the pinned document is in a background tab
    const LRESULT pos = SendMessageW(g_npp.nppHandle, NPPM_GETPOSFROMBUFFERID, static_cast<WPARAM>(pinnedBuffer_),
                                     SendMessageW(g_npp.nppHandle, NPPM_GETCURRENTVIEW, 0, 0));
    if (pos == -1) return nullptr;
    SendMessageW(g_npp.nppHandle, NPPM_ACTIVATEDOC, static_cast<WPARAM>(pos >> 30), pos & 0x3FFFFFFF);
    return sourceView();
}

void PreviewPanel::shutdown() {
    if (hwnd_) KillTimer(hwnd_, kRenderTimer);
    ready_ = false;
    if (controller_) controller_->Close();
    safeRelease(webView_);
    safeRelease(controller_);
    safeRelease(environment_);
    if (tabIcon_) {
        DestroyIcon(tabIcon_);
        tabIcon_ = nullptr;
    }
}

// =================================================================================================
// WebView2 setup
// =================================================================================================

void PreviewPanel::initWebView() {
    webViewRequested_ = true;
    setStatus(L"Loading preview ...");

    const std::wstring assets = pluginDirectory() + L"\\assets";
    if (!fileExists(assets + L"\\preview.html")) {
        setStatus(L"The preview files are missing:\n" + assets +
                  L"\n\nPlease reinstall the plugin (copy the complete MarkdownLive folder).");
        return;
    }
    const HMODULE loader = LoadLibraryW((pluginDirectory() + L"\\WebView2Loader.dll").c_str());
    const auto createEnvironment =
        loader ? reinterpret_cast<CreateEnvironmentFn>(reinterpret_cast<void*>(
                     GetProcAddress(loader, "CreateCoreWebView2EnvironmentWithOptions")))
               : nullptr;
    if (!createEnvironment) {
        setStatus(L"WebView2Loader.dll is missing in\n" + pluginDirectory() +
                  L"\n\nPlease reinstall the plugin (copy the complete MarkdownLive folder).");
        return;
    }

    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);  // no-op when Notepad++ already did it

    // The default user data folder would be next to notepad++.exe, which is usually not writable.
    const std::wstring userData = configDirectory() + L"\\MarkdownLive\\WebView2";
    SHCreateDirectoryExW(nullptr, userData.c_str(), nullptr);

    auto* handler = new EnvironmentCompleted(
        kIID_EnvironmentCompleted, [this](HRESULT result, ICoreWebView2Environment* environment) -> HRESULT {
            if (FAILED(result) || !environment || !hwnd_) {
                setStatus(kRuntimeHint);
                return S_OK;
            }
            environment_ = environment;
            environment_->AddRef();
            auto* controllerHandler = new ControllerCompleted(
                kIID_ControllerCompleted, [this](HRESULT hr, ICoreWebView2Controller* controller) -> HRESULT {
                    if (FAILED(hr) || !controller || !hwnd_) {
                        setStatus(L"The WebView2 control could not be created.\n\n" + std::wstring(kRuntimeHint));
                        return S_OK;
                    }
                    onControllerCreated(controller);
                    return S_OK;
                });
            const HRESULT hr = environment_->CreateCoreWebView2Controller(hwnd_, controllerHandler);
            controllerHandler->Release();
            if (FAILED(hr)) setStatus(kRuntimeHint);
            return S_OK;
        });
    const HRESULT hr = createEnvironment(nullptr, userData.c_str(), nullptr, handler);
    handler->Release();
    if (FAILED(hr)) setStatus(kRuntimeHint);
}

void PreviewPanel::onControllerCreated(ICoreWebView2Controller* controller) {
    controller_ = controller;
    controller_->AddRef();
    if (FAILED(controller_->get_CoreWebView2(&webView_)) || !webView_) {
        setStatus(L"The WebView2 control could not be created.");
        return;
    }

    ICoreWebView2Settings* settings = nullptr;
    if (SUCCEEDED(webView_->get_Settings(&settings)) && settings) {
        settings->put_IsStatusBarEnabled(FALSE);
        settings->put_AreDefaultScriptDialogsEnabled(FALSE);
        settings->Release();
    }

    // Preview page and its scripts are served from the plugin's "assets" folder.
    ICoreWebView2_3* webView3 = nullptr;
    if (FAILED(webView_->QueryInterface(kIID_WebView2_3, reinterpret_cast<void**>(&webView3))) || !webView3) {
        setStatus(L"The installed WebView2 Runtime is too old.\n\n" + std::wstring(kRuntimeHint));
        return;
    }
    webView3->SetVirtualHostNameToFolderMapping(kAssetHost, (pluginDirectory() + L"\\assets").c_str(),
                                                COREWEBVIEW2_HOST_RESOURCE_ACCESS_KIND_ALLOW);
    webView3->Release();

    EventRegistrationToken token = {};

    auto* messageHandler = new WebMessageReceived(
        kIID_WebMessageReceived, [this](ICoreWebView2*, ICoreWebView2WebMessageReceivedEventArgs* args) -> HRESULT {
            LPWSTR source = nullptr;
            if (FAILED(args->get_Source(&source))) return S_OK;
            if (!startsWithNoCase(takeString(source), kAssetOrigin)) return S_OK;  // only our own page may talk to us
            LPWSTR text = nullptr;
            if (FAILED(args->TryGetWebMessageAsString(&text))) return S_OK;
            // Handled asynchronously: commands may open dialogs, which must not happen inside this callback.
            g_webMessages.push_back(takeString(text));
            PostMessageW(hwnd_, WM_APP_WEBMESSAGE, 0, 0);
            return S_OK;
        });
    webView_->add_WebMessageReceived(messageHandler, &token);
    messageHandler->Release();

    auto* navigationHandler = new NavigationStarting(
        kIID_NavigationStarting, [](ICoreWebView2*, ICoreWebView2NavigationStartingEventArgs* args) -> HRESULT {
            LPWSTR raw = nullptr;
            if (FAILED(args->get_Uri(&raw))) return S_OK;
            const std::wstring uri = takeString(raw);
            if (!startsWithNoCase(uri, kPageUrl)) {
                // the preview never navigates away: open everything else in the default browser
                args->put_Cancel(TRUE);
                openExternal(uri);
            }
            return S_OK;
        });
    webView_->add_NavigationStarting(navigationHandler, &token);
    navigationHandler->Release();

    auto* newWindowHandler = new NewWindowRequested(
        kIID_NewWindowRequested, [](ICoreWebView2*, ICoreWebView2NewWindowRequestedEventArgs* args) -> HRESULT {
            args->put_Handled(TRUE);
            LPWSTR raw = nullptr;
            if (SUCCEEDED(args->get_Uri(&raw))) openExternal(takeString(raw));
            return S_OK;
        });
    webView_->add_NewWindowRequested(newWindowHandler, &token);
    newWindowHandler->Release();

    // Local images of the document are delivered by serveLocalFile().
    webView_->AddWebResourceRequestedFilter((std::wstring(kFileUrlPrefix) + L"*").c_str(), COREWEBVIEW2_WEB_RESOURCE_CONTEXT_ALL);
    auto* resourceHandler = new WebResourceRequested(
        kIID_WebResourceRequested, [this](ICoreWebView2*, ICoreWebView2WebResourceRequestedEventArgs* args) -> HRESULT {
            serveLocalFile(args);
            return S_OK;
        });
    webView_->add_WebResourceRequested(resourceHandler, &token);
    resourceHandler->Release();

    sendTheme(isDarkMode());
    resizeWebView();
    controller_->put_IsVisible(IsWindowVisible(hwnd_) ? TRUE : FALSE);
    setStatus(L"");
    if (FAILED(webView_->Navigate(kPageUrl))) setStatus(L"The preview page could not be loaded.");
}

void PreviewPanel::serveLocalFile(ICoreWebView2WebResourceRequestedEventArgs* args) {
    if (!environment_) return;
    std::wstring path;
    ICoreWebView2WebResourceRequest* request = nullptr;
    if (SUCCEEDED(args->get_Request(&request)) && request) {
        LPWSTR raw = nullptr;
        if (SUCCEEDED(request->get_Uri(&raw))) {
            const std::wstring uri = takeString(raw);
            const size_t query = uri.find(L"?p=");
            if (query != std::wstring::npos) {
                std::wstring encoded = uri.substr(query + 3);
                const size_t end = encoded.find_first_of(L"&#");
                if (end != std::wstring::npos) encoded.erase(end);
                path = percentDecode(encoded);
                for (auto& c : path) {
                    if (c == L'/') c = L'\\';
                }
            }
        }
        request->Release();
    }

    ICoreWebView2WebResourceResponse* response = nullptr;
    const wchar_t* mime = imageMimeType(extensionOf(path));  // only images are ever served
    IStream* stream = nullptr;
    if (mime && isAbsolutePath(path) &&
        SUCCEEDED(SHCreateStreamOnFileEx(path.c_str(), STGM_READ | STGM_SHARE_DENY_NONE, 0, FALSE, nullptr, &stream)) && stream) {
        const std::wstring headers = std::wstring(L"Content-Type: ") + mime + L"\r\nCache-Control: no-cache";
        environment_->CreateWebResourceResponse(stream, 200, L"OK", headers.c_str(), &response);
        stream->Release();
    } else {
        environment_->CreateWebResourceResponse(nullptr, 404, L"Not Found", L"", &response);
    }
    if (response) {
        args->put_Response(response);
        response->Release();
    }
}

// =================================================================================================
// host -> page
// =================================================================================================

void PreviewPanel::post(const std::wstring& json) {
    if (webView_ && ready_) webView_->PostWebMessageAsJson(json.c_str());
}

void PreviewPanel::sendTheme(bool dark) {
    if (controller_) {
        ICoreWebView2Controller2* controller2 = nullptr;
        if (SUCCEEDED(controller_->QueryInterface(kIID_Controller2, reinterpret_cast<void**>(&controller2))) && controller2) {
            const COREWEBVIEW2_COLOR color = dark ? COREWEBVIEW2_COLOR{255, 13, 17, 23} : COREWEBVIEW2_COLOR{255, 255, 255, 255};
            controller2->put_DefaultBackgroundColor(color);
            controller2->Release();
        }
    }
    post(dark ? L"{\"type\":\"theme\",\"dark\":true}" : L"{\"type\":\"theme\",\"dark\":false}");
}

void PreviewPanel::scheduleRender() {
    if (!hwnd_) return;
    // larger documents are rendered less often while typing
    const HWND view = sourceView();
    const intptr_t length = view ? Editor(view).length() : 0;
    const UINT delay = length < 100 * 1024 ? 60 : length < 500 * 1024 ? 180 : 450;
    SetTimer(hwnd_, kRenderTimer, delay, nullptr);
}

void PreviewPanel::render(bool resetScroll) {
    if (hwnd_) KillTimer(hwnd_, kRenderTimer);
    if (resetScroll) pendingReset_ = true;
    if (!ready_ || !visible_ || !hwnd_ || (!IsWindowVisible(hwnd_) && pendingExport_.empty())) {
        dirty_ = true;
        return;
    }
    dirty_ = false;
    const bool reset = pendingReset_;
    pendingReset_ = false;

    const std::wstring path = sourcePath();
    const std::wstring ext = extensionOf(path);
    if (!ext.empty() && ext != L"txt" && !isMarkdownPath(path)) {
        post(L"{\"type\":\"placeholder\",\"text\":" +
             jsonString(fileNameOf(path) + L"\n\nNo preview: this is not a Markdown file.") + L"}");
        return;
    }

    std::wstring text;
    const HWND view = sourceView();
    if (view) {
        const Editor ed(view);
        if (ed.length() > kMaxPreviewBytes) {
            post(L"{\"type\":\"placeholder\",\"text\":" + jsonString(fileNameOf(path) + L"\n\nThis document is too large for the live preview.") + L"}");
            return;
        }
        bool atEnd = false;
        lastTopLine_ = ed.firstVisibleDocLine(&atEnd);
        lastAtEnd_ = atEnd;
        lastCaretLine_ = ed.lineFromPosition(ed.caret());
        text = toWide(ed.text(), ed.codePage());
        if (pinned_) pinnedText_ = text;
    } else {
        text = pinnedText_;  // pinned document in a background tab: it cannot have changed
    }
    const bool atEnd = lastAtEnd_;

    std::wstring json = L"{\"type\":\"render\",\"text\":";
    json += jsonString(text);
    json += L",\"baseDir\":" + jsonString(isAbsolutePath(path) ? directoryOf(path) : std::wstring());
    json += L",\"fileName\":" + jsonString(fileNameOf(path));
    json += L",\"reset\":";
    json += reset ? L"true" : L"false";
    json += L",\"line\":" + std::to_wstring(lastTopLine_);
    json += L",\"atEnd\":";
    json += atEnd ? L"true" : L"false";
    json += L",\"cursor\":" + std::to_wstring(lastCaretLine_) + L"}";
    post(json);
}

// =================================================================================================
// editor events
// =================================================================================================

void PreviewPanel::onDocumentChanged(bool resetScroll) {
    if (!visible_) return;
    if (pinned_ && currentBufferId() != pinnedBuffer_) {
        updateCaption();  // e.g. the pinned document was renamed
        return;
    }
    render(resetScroll);
}

void PreviewPanel::onBufferClosed(intptr_t bufferId) {
    if (!pinned_ || bufferId != pinnedBuffer_) return;
    setPinned(false);
    if (visible_) render(true);
}

void PreviewPanel::onTextChanged(HWND view) {
    if (!visible_ || view != sourceView()) return;
    dirty_ = true;
    if (ready_) scheduleRender();
}

void PreviewPanel::onEditorUpdateUI(HWND view) {
    if (!visible_ || !ready_ || view != sourceView()) return;
    const Editor ed(view);
    const intptr_t caretLine = ed.lineFromPosition(ed.caret());
    if (caretLine != lastCaretLine_) {
        lastCaretLine_ = caretLine;
        post(L"{\"type\":\"cursor\",\"line\":" + std::to_wstring(caretLine) + L"}");
    }
    bool atEnd = false;
    const intptr_t top = ed.firstVisibleDocLine(&atEnd);
    if (top == lastTopLine_ && atEnd == lastAtEnd_) return;
    lastTopLine_ = top;
    lastAtEnd_ = atEnd;
    if (!g_settings.syncScroll || GetTickCount64() < ignoreEditorScrollUntil_) return;
    post(L"{\"type\":\"scroll\",\"line\":" + std::to_wstring(top) + L",\"atEnd\":" + (atEnd ? L"true" : L"false") + L"}");
}

void PreviewPanel::onThemeChanged() {
    if (hwnd_) InvalidateRect(hwnd_, nullptr, TRUE);
    sendTheme(isDarkMode());
}

void PreviewPanel::onSyncSettingChanged() {
    post(g_settings.syncScroll ? L"{\"type\":\"sync\",\"enabled\":true}" : L"{\"type\":\"sync\",\"enabled\":false}");
}

// =================================================================================================
// page -> host
// =================================================================================================

void PreviewPanel::onWebMessage(const std::wstring& message) {
    // wire format: "type\nfield\nfield..." (see encodeMessage in web/src/preview.js)
    std::wstring rest;
    const size_t firstBreak = message.find(L'\n');
    const std::wstring type = message.substr(0, firstBreak);
    if (firstBreak != std::wstring::npos) rest = message.substr(firstBreak + 1);
    auto takeField = [&rest]() {
        const size_t pos = rest.find(L'\n');
        const std::wstring field = rest.substr(0, pos);
        rest = pos == std::wstring::npos ? std::wstring() : rest.substr(pos + 1);
        return field;
    };

    if (type == L"ready") {
        ready_ = true;
        sendTheme(isDarkMode());
        onSyncSettingChanged();
        sendPinState();
        dirty_ = true;
        if (!pendingExport_.empty()) startExport();
        else render(true);
    } else if (type == L"scroll") {
        const double line = std::wcstod(takeField().c_str(), nullptr);
        onPreviewScrolled(line, takeField() == L"1");
    } else if (type == L"toggleTask") {
        const intptr_t line = static_cast<intptr_t>(_wtoi64(takeField().c_str()));
        if (const HWND view = activateSource()) commands::toggleTaskAtLine(Editor(view), line);
    } else if (type == L"togglePin") {
        runMenuCommand(MenuPinPreview);
    } else if (type == L"gotoLine") {
        const HWND view = activateSource();
        if (!view) return;
        const Editor ed(view);
        const intptr_t line = static_cast<intptr_t>(_wtoi64(takeField().c_str()));
        if (line >= 0 && line < ed.lineCount()) {
            ed.call(SCI_ENSUREVISIBLE, static_cast<uintptr_t>(line));
            ed.call(SCI_GOTOLINE, static_cast<uintptr_t>(line));
            ed.call(SCI_GRABFOCUS);
        }
    } else if (type == L"openLink") {
        openLink(rest);
    } else if (type == L"exportReady") {
        const std::wstring kind = takeField();
        finishExport(kind, rest);
    }
}

void PreviewPanel::onPreviewScrolled(double line, bool atEnd) {
    if (!g_settings.syncScroll) return;
    const HWND view = sourceView();
    if (!view) return;  // pinned document in a background tab
    const Editor ed(view);
    const intptr_t lastLine = ed.lineCount() - 1;
    intptr_t docLine = static_cast<intptr_t>(line);
    if (docLine < 0) docLine = 0;
    if (atEnd || docLine > lastLine) docLine = lastLine;  // Scintilla clamps to the last possible scroll position
    ed.call(SCI_SETFIRSTVISIBLELINE, static_cast<uintptr_t>(ed.call(SCI_VISIBLEFROMDOCLINE, static_cast<uintptr_t>(docLine))));
    // the resulting SCN_UPDATEUI must not be echoed back to the preview
    ignoreEditorScrollUntil_ = GetTickCount64() + 250;
    lastTopLine_ = ed.firstVisibleDocLine(&lastAtEnd_);
}

void PreviewPanel::openLink(const std::wstring& href) {
    if (isWebUrl(href)) {
        openExternal(href);
        return;
    }
    if (href.find(L':') != std::wstring::npos && !startsWithNoCase(href, L"file:") &&
        !(href.size() > 2 && href[1] == L':')) {
        return;  // unknown scheme (javascript:, ms-settings:, ...): ignore
    }

    std::wstring target = href;
    const size_t cut = target.find_first_of(L"#?");
    if (cut != std::wstring::npos) target.erase(cut);
    target = percentDecode(target);
    if (startsWithNoCase(target, L"file:")) {
        target.erase(0, 5);
        if (target.compare(0, 3, L"///") == 0) target.erase(0, 3);         // file:///C:/dir/file
        else if (target.compare(0, 2, L"//") != 0 && !target.empty() && target[0] == L'/') target.erase(0, 1);
    }
    for (auto& c : target) {
        if (c == L'/') c = L'\\';
    }
    if (target.empty()) return;
    if (!isAbsolutePath(target)) {
        const std::wstring document = sourcePath();
        if (!isAbsolutePath(document)) return;
        target = directoryOf(document) + L"\\" + target;
    }
    wchar_t full[MAX_PATH] = {0};
    if (!GetFullPathNameW(target.c_str(), MAX_PATH, full, nullptr) || !fileExists(full)) {
        showMessage(L"The linked file does not exist:\n" + target, MB_ICONWARNING);
        return;
    }

    // Documents and media are opened with their default application, everything else as text in Notepad++.
    static const wchar_t* const viewerTypes[] = {L"pdf",  L"png", L"jpg",  L"jpeg", L"gif",  L"webp", L"bmp", L"svg",
                                                 L"html", L"htm", L"docx", L"xlsx", L"pptx", L"odt",  L"mp4", L"mp3"};
    const std::wstring ext = extensionOf(full);
    for (const wchar_t* type : viewerTypes) {
        if (ext == type) {
            ShellExecuteW(g_npp.nppHandle, L"open", full, nullptr, nullptr, SW_SHOWNORMAL);
            return;
        }
    }
    SendMessageW(g_npp.nppHandle, NPPM_DOOPEN, 0, reinterpret_cast<LPARAM>(full));
}

// =================================================================================================
// export
// =================================================================================================

void PreviewPanel::exportHtml() { requestExport(L"html"); }
void PreviewPanel::exportPdf() { requestExport(L"pdf"); }

void PreviewPanel::requestExport(const std::wstring& kind) {
    if (!visible_) show();
    if (!hwnd_) return;
    if (webViewRequested_ && !webView_ && !status_.empty() && status_ != L"Loading preview ...") {
        showMessage(status_, MB_ICONWARNING);  // WebView2 is not available
        return;
    }
    pendingExport_ = kind;
    if (ready_) startExport();  // otherwise started as soon as the page reports "ready"
}

void PreviewPanel::startExport() {
    const std::wstring kind = pendingExport_;
    if (kind == L"pdf") sendTheme(false);  // PDFs are always printed with the light theme
    dirty_ = true;
    render(false);  // make sure the latest text is exported
    pendingExport_.clear();
    post(L"{\"type\":\"export\",\"kind\":" + jsonString(kind) + L"}");
}

void PreviewPanel::finishExport(const std::wstring& kind, const std::wstring& html) {
    const std::wstring document = sourcePath();
    const std::wstring initialDir = isAbsolutePath(document) ? directoryOf(document) : std::wstring();
    const std::wstring stem = fileStemOf(document);

    if (kind == L"html") {
        const std::wstring path = saveFileDialog(stem + L".html", L"HTML document\0*.html;*.htm\0All files\0*.*\0", L"html", initialDir);
        if (!path.empty() && !writeFileUtf8(path, html)) showMessage(L"The file could not be written:\n" + path, MB_ICONWARNING);
        return;
    }

    const std::wstring path = saveFileDialog(stem + L".pdf", L"PDF document\0*.pdf\0All files\0*.*\0", L"pdf", initialDir);
    ICoreWebView2_7* webView7 = nullptr;
    if (path.empty() || !webView_ ||
        FAILED(webView_->QueryInterface(kIID_WebView2_7, reinterpret_cast<void**>(&webView7))) || !webView7) {
        if (!path.empty()) showMessage(L"PDF export requires a newer WebView2 Runtime.", MB_ICONWARNING);
        sendTheme(isDarkMode());
        return;
    }

    // print with backgrounds (code blocks, table headers); falls back to the defaults on old runtimes
    ICoreWebView2PrintSettings* printSettings = nullptr;
    ICoreWebView2Environment6* environment6 = nullptr;
    if (environment_ && SUCCEEDED(environment_->QueryInterface(kIID_Environment6, reinterpret_cast<void**>(&environment6))) && environment6) {
        if (SUCCEEDED(environment6->CreatePrintSettings(&printSettings)) && printSettings) {
            printSettings->put_ShouldPrintBackgrounds(TRUE);
        }
        environment6->Release();
    }

    auto* handler = new PrintToPdfCompleted(kIID_PrintToPdfCompleted, [this, path](HRESULT result, BOOL success) -> HRESULT {
        sendTheme(isDarkMode());
        if (FAILED(result) || !success) showMessage(L"The PDF could not be written:\n" + path, MB_ICONWARNING);
        return S_OK;
    });
    if (FAILED(webView7->PrintToPdf(path.c_str(), printSettings, handler))) {
        sendTheme(isDarkMode());
        showMessage(L"The PDF could not be written:\n" + path, MB_ICONWARNING);
    }
    handler->Release();
    if (printSettings) printSettings->Release();
    webView7->Release();
}
