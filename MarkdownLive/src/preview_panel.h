// Dockable live preview panel hosting a WebView2 control.
#pragma once

#include <windows.h>

#include <string>

struct ICoreWebView2;
struct ICoreWebView2Controller;
struct ICoreWebView2Environment;
struct ICoreWebView2WebResourceRequestedEventArgs;

class PreviewPanel {
public:
    static PreviewPanel& instance();

    void toggle();
    void show();
    void hide();
    bool visible() const { return visible_; }

    // Pinned: the preview keeps showing one document while other files are edited.
    bool pinned() const { return pinned_; }
    void togglePin();

    // Editor events
    void onDocumentChanged(bool resetScroll);  // other buffer activated, file saved / renamed
    void onBufferClosed(intptr_t bufferId);
    void onTextChanged(HWND view);     // text edited (debounced)
    void onEditorUpdateUI(HWND view);  // caret moved or view scrolled
    void onThemeChanged();
    void onSyncSettingChanged();

    void exportHtml();
    void exportPdf();

    void shutdown();

private:
    PreviewPanel() = default;

    bool createWindow();
    void initWebView();
    void onControllerCreated(ICoreWebView2Controller* controller);
    void resizeWebView();
    void setStatus(const std::wstring& text);
    void updateCaption();

    // the previewed document: the current one or the pinned one
    std::wstring sourcePath() const;
    HWND sourceView() const;  // view showing it, nullptr if it is in a background tab
    HWND activateSource();    // brings it to front if necessary
    void setPinned(bool pinned);
    void sendPinState();
    void paint();

    void post(const std::wstring& json);
    void sendTheme(bool dark);
    void render(bool resetScroll);
    void scheduleRender();

    void onWebMessage(const std::wstring& message);
    void onPreviewScrolled(double line, bool atEnd);
    void openLink(const std::wstring& href);
    void serveLocalFile(ICoreWebView2WebResourceRequestedEventArgs* args);

    void requestExport(const std::wstring& kind);
    void startExport();
    void finishExport(const std::wstring& kind, const std::wstring& html);

    static INT_PTR CALLBACK dialogProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);

    HWND hwnd_ = nullptr;
    HICON tabIcon_ = nullptr;
    bool registered_ = false;
    bool visible_ = false;
    bool webViewRequested_ = false;
    bool ready_ = false;        // page loaded and listening
    bool dirty_ = true;         // content must be rendered when the page becomes ready / visible
    bool pendingReset_ = true;  // next render re-syncs the scroll position
    std::wstring status_;       // shown while the WebView is not available
    bool pinned_ = false;
    intptr_t pinnedBuffer_ = 0;
    std::wstring pinnedText_;  // last rendered text of the pinned document

    ICoreWebView2Environment* environment_ = nullptr;
    ICoreWebView2Controller* controller_ = nullptr;
    ICoreWebView2* webView_ = nullptr;

    intptr_t lastTopLine_ = -1;
    intptr_t lastCaretLine_ = -1;
    bool lastAtEnd_ = false;
    ULONGLONG ignoreEditorScrollUntil_ = 0;
    std::wstring pendingExport_;
};
