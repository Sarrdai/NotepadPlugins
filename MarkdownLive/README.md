# Markdown Live – Markdown plugin for Notepad++

Live preview and editing helpers for Markdown in Notepad++ (64-bit).

The preview runs in a dockable panel based on **WebView2** (Chromium) and is updated incrementally while typing. All
libraries are bundled with the plugin – no internet connection is needed.

## Features

**Live preview**

- GitHub Flavored Markdown: tables, task lists, strikethrough, autolinks, footnotes, definition lists, `==highlight==`,
  super-/subscript, emoji shortcodes (`:rocket:`), GitHub alerts (`> [!NOTE]`), YAML front matter, embedded HTML
  (sanitized)
- Syntax highlighting for code blocks (highlight.js) including a "Copy" button
- Math with KaTeX (`$…$`, `$$…$$`) and diagrams with Mermaid (` ```mermaid `)
- Synchronized scrolling in both directions, the paragraph at the caret is marked
- Double click in the preview jumps to the source line, checkboxes can be clicked in the preview
- Local images (relative to the document); links open in the browser, `.md` files in Notepad++
- Light/dark design follows the Notepad++ dark mode
- Export as standalone **HTML** or as **PDF**
- **Pin**: the pin button at the top right of the preview (or *Pin Live Preview to Current File*) keeps the preview on
  the current document while other files are edited – e.g. a Markdown guide next to the source code. If the pinned
  document is visible (also in the second view), it is still updated live and scrolled in sync; a double click or
  ticking a checkbox in the preview brings it to the front. The panel title shows the pinned document. Clicking again,
  closing the preview or closing the document releases the pin.
- **Closes automatically**: switching to a non-Markdown file closes the preview, the next Markdown file reopens it (like
  the image panel of Tiff++ and the pretty view of JSON++). Unsaved documents without extension keep it open; only the
  pin keeps it for other files.

**Code highlighting in the editor**

Fenced code blocks (` ```python `, `~~~cs` …) are colored in the editor with the lexer of the given language – as if the
whole file were in that language, limited to the code block. Colors and keywords come from the Notepad++ configuration:
active theme (`stylers.xml` or `themes\*.xml`, including user keywords from the Style Configurator) and `langs.xml`.
Changing the theme, the dark mode or the Style Configurator is applied immediately.

- Language by name, short name or file extension: `py`, `cs`/`c#`, `js`, `ts`, `sh`, `ps1`, `bat`, `sql`, `xml`,
  `html`, `json`, `yaml`, `ini`, `rs`, `go`, `lua` … – everything Notepad++ knows
- Blocks without or with an unknown language (e.g. `mermaid`) stay unchanged
- Only text colors are applied (no bold/italic and no background colors), because the coloring is a Scintilla
  indicator on top of the Markdown lexer
- Requires Notepad++ 8.4 or newer; can be switched off with *Highlight Code Blocks in Editor*

**Editing helpers** (menu *Plugins → Markdown Live*)

| Command | Shortcut | Description |
| :-- | :-- | :-- |
| Toggle Live Preview | `Ctrl+Shift+M` | Show/hide the preview (also via the toolbar) |
| Pin Live Preview to Current File | – | Pin the preview to the current document or release it |
| Bold / Italic | `Ctrl+Shift+E` / `Ctrl+Shift+I` | Toggle for the selection or the word at the caret |
| Strikethrough, Inline Code, Code Block | – | Wrap the selection or unwrap it again |
| Heading: Add # / Remove # | – | Change the heading level (also for several lines) |
| Toggle Bullet / Numbered List, Blockquote | – | Toggle the line prefixes of the selected lines |
| Task: Add / Check / Uncheck | – | Create a task or tick it |
| Insert Link | – | Uses a URL from the clipboard as target automatically |
| Insert Image… / Paste Image from Clipboard | – | Link an image or save a screenshot as PNG in `images\` |
| Insert Table / Format Table | – | Insert a table or align its columns |
| Table: Insert Row Above / Below, Delete Row | – | Insert or delete a row relative to the caret (header and separator rows are protected) |
| Table: Insert Column Left / Right, Delete Column | – | Insert or delete a column; the table is realigned and the caret moves into the new cell |
| Insert / Update Table of Contents | – | Table of contents between `<!-- TOC -->` markers |
| Export as HTML… / PDF… | – | Export the current preview |

In addition, lists, tasks and quotes are continued on **Enter** (numbered lists keep counting; Enter on an empty list
item ends the list).

The options at the end of the menu are saved in `MarkdownLive.ini` in the Notepad++ plugin config folder:

| Option | Default | Description |
| :-- | :-- | :-- |
| Synchronize Scrolling | on | Editor and preview scroll together |
| Continue Lists on Enter | on | List continuation (see above) |
| Open Preview Automatically for Markdown Files | off | Opens the preview when a Markdown file is activated (it always closes for other files) |
| Highlight Code Blocks in Editor | on | Code highlighting in code blocks (see above) |

More shortcuts can be assigned in *Settings → Shortcut Mapper → Plugin commands*.

**Tool panel** (optional)

A slim bar with buttons for all commands and options above – without going through the menu. It is shown via
*Tool Panel: Top / Bottom / Hidden* in the plugin menu or by right-clicking the bar **at the top** (below the
Notepad++ toolbar), **at the bottom** (above the status bar) or not at all; hidden by default, the choice is saved.

- Toggles (preview, pin and all options) show their current state
- Tooltips show the command and its currently assigned shortcut
- If the window is too narrow, the remaining commands are available via `»` at the right edge
- Follows the Notepad++ dark mode and scales with the screen resolution (DPI)

## Installation

1. Download the artifact `MarkdownLive-x64` (workflow *MarkdownLive*) or the bundle `NotepadPlugins-x64` (workflow
   *Bundle*) from *Actions*, or the attachment of a release.
2. Close Notepad++ and copy the contained folder `MarkdownLive` to `…\Notepad++\plugins\`, so that
   `plugins\MarkdownLive\MarkdownLive.dll` exists. Copy the complete folder (`WebView2Loader.dll` and `assets\` belong
   to it).
3. Start Notepad++, open a `.md` file and press `Ctrl+Shift+M`.

Requirements: Notepad++ 8.x (64-bit) and the *Microsoft Edge WebView2 Runtime*. It is preinstalled on Windows 11 and
current Windows 10; if it is missing, the preview panel shows a hint with the download address.

## Layout

| Path | Content |
| :-- | :-- |
| `src/` | Plugin DLL (C++17): Notepad++ interface, WebView2 panel, tool panel, editor commands |
| `src/md_text.*` | Platform independent text logic (lists, tables, TOC, slugs) |
| `src/code_syntax.*` | Platform independent: find code blocks, language → lexer, read Notepad++ XML (themes, `langs.xml`, `config.xml`) |
| `src/code_highlight.*` | Coloring of code blocks in the editor (lexer via `NPPM_CREATELEXER`, indicator with text color) |
| `web/` | Preview page (markdown-it, highlight.js, KaTeX, Mermaid, DOMPurify, morphdom) |
| `tests/`, `web/test/` | Unit tests of the text logic, browser tests of the preview |

## Building

Windows with Visual Studio 2022 and Node.js >= 20, in the folder `MarkdownLive/` (CI:
`.github/workflows/markdownlive.yml` in the repository root):

```bash
cd web && npm ci && npm run build && cd ..   # preview page -> web/dist
cmake -S . -B build -A x64
cmake --build build --config Release           # -> build/package/MarkdownLive
ctest --test-dir build -C Release
```

The WebView2 SDK is downloaded from NuGet automatically during configuration (checksum verified). On Linux the same
CMake call only builds the unit tests of the text logic.

## Test status

- Text logic: unit tests (`ctest`); preview page: end-to-end tests in headless Chromium (`npm test`)
- Plugin DLL (at the time a MinGW build): checked in Notepad++ 8.7.1 under Wine (loading, menu, docking, all editor
  commands, list continuation, settings)
- The embedded WebView2 runtime cannot run under Wine and has therefore only been compiled, not yet run on a real
  Windows system.
- Pin, automatic closing and code highlighting in the editor: code block detection, language mapping and reading the
  Notepad++ configuration are covered by unit tests, the pin button by a browser test; the interaction with Notepad++
  has only been compiled and not yet run in Notepad++ itself.
- Tool panel: checked under Wine in a test window with the structure of the Notepad++ main window (two rebars,
  toolbar, status bar): top/bottom/hidden, clicks, toggles, tooltips, overflow menu, dark mode, 200 % DPI, exit. It has
  not yet run in Notepad++ itself. Notepad++ offers no plugin interface for such a bar, so it is added as an additional
  band to the rebars of the main window (see `src/tool_panel.h`).

## License

GPL-3.0-or-later, see `LICENSE`.

The package contains `WebView2Loader.dll` (Microsoft, BSD-style license, see `WebView2-LICENSE.txt`) and bundled open
source libraries (mostly MIT/BSD/Apache-2.0; the license notices are at the end of the files in `assets\`).
