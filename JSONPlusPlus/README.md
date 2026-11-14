# JSON++ for Notepad++

JSON++ shows **compact (minified) JSON** in a docked, **pretty printed and editable** panel.
The panel and the editor stay synchronised in both directions, and the file itself **stays compact**:
saving always writes the compact form.

```
 Notepad++ editor (compact, saved to disk) - only as high as its content
┌──────────────────────────────────────────────────────────┐
│{"name":"demo","tags":["a","b"],"n":1}                    │
╞══════════════════════════════════════════════════════════╡ ⇅
│{                                                         │ JSON++ panel (pretty, editable)
│    "name": "demo",                                       │ uses the remaining height
│    "tags": [                                             │
│        "a",                                              │
│        "b"                                               │
│    ],                                                    │
│    "n": 1                                                │
│}                                                         │
│✔ Valid JSON • 38 bytes compact                       Pin │
└──────────────────────────────────────────────────────────┘
```

## Features

- **Opens automatically** for JSON files (`.json`, `.jsonc`, `.geojson`, ... or language *JSON*) whose
  content is compact: it starts with `{`/`[` and has (almost) no line breaks.
- **Pretty view in a separate panel.** Opening it does not change the file.
- **Docked below the editor, sized to fit:** the editor keeps just the height its (usually one line
  of) compact JSON needs, the panel gets the rest. The height is fitted when the panel opens and
  when the number of lines in the editor changes; a height you set by dragging the splitter is kept
  until then. At most `MaxEditorHeightPercent` of the height goes to the editor. If you dock the panel
  elsewhere (left, right, floating), nothing is resized. Notepad++ remembers where a panel was
  docked: if JSON++ was docked on the right before, drag it to the bottom once.
- **Edit on both sides:**
  - Edits in the editor show up in the panel, re-indented.
  - Edits in the panel are written into the document as a **minimal compact edit**. Only the changed
    tokens are replaced, so the rest of the document keeps its layout and the undo history stays small.
  - Once you stop typing and the JSON is valid again, the panel is re-indented. The caret keeps its
    position.
- **Saving writes the compact form.** Pending panel edits are written into the document before
  every save.
- **Undo/redo** use the document's history. Each panel edit is one undo step there, so Notepad++'s
  *Edit → Undo* reverts it.
- **Live validation** (RFC 8259) with a squiggle at the error and a status line such as
  `⚠ Ln 12, Col 5: Expected ',' or '}'`. JSONC comments and JSON Lines are supported.
- **Caret sync:** clicking in one view moves the caret in the other to the same JSON position.
- **Panel editor:** uses Notepad++'s own JSON lexer and your theme colours (dark mode included). It
  also has folding, brace matching, indentation guides, line numbers and auto-indent.
- Works when the document is shown in either view, after *Move/Clone to other view*, and for files
  that are not in the active tab.

## Installation

1. Download the artifact `JsonPlusPlus-x64` (workflow *JSON++*) or the bundle `NotepadPlugins-x64`
   (workflow *Bundle*) from GitHub Actions or the [releases](../../releases).
2. Extract it and copy the folder `JsonPlusPlus` (DLL, README, LICENSE) into `<Notepad++>\plugins\`, so that
   `plugins\JsonPlusPlus\JsonPlusPlus.dll` exists. The folder name must match the DLL name.
3. Restart Notepad++. The plugin appears under **Plugins → JSON++**.

Requires Notepad++ **8.4 or newer** (Scintilla 5 / Lexilla, `NPPM_CREATELEXER`).

## Menu

The panel belongs to its file: switching to another tab closes it, switching back reopens it (like
the panels of Markdown Live and Tiff++).
**Pin** (right end of the panel's status row) keeps it open on its file while you work in other tabs;
closing the panel or the file releases the pin.

| Command | Description |
|---|---|
| Toggle Pretty View | Turns the panel on or off for the active file (also via the toolbar button). Overrides the automatic detection. |
| Pin Pretty View to Current File | Keeps the panel on its file while other tabs are active (same as **Pin** in the panel's status row). |
| Re-indent Pretty View Now | Writes pending edits and re-indents the panel immediately. |
| Open Automatically for Compact JSON | Turns automatic activation on or off. |
| Synchronize Caret | Keeps the caret of both views on the same JSON position. |
| Settings... | Opens `JsonPlusPlus.ini` in Notepad++. Changes apply when you save it. |

## Settings (`plugins\config\JsonPlusPlus.ini`)

| Key | Default | Meaning |
|---|---|---|
| `AutoActivate` | `1` | Open the panel automatically for compact JSON. |
| `Extensions` | `json;jsonc;json5;geojson;...` | File extensions treated as JSON (besides the JSON language). |
| `MinLength` | `64` | Smaller documents are never considered compact. |
| `MinAverageLineLength` | `300` | Multi-line documents count as compact above this average line length. |
| `MaxFileSizeMB` | `32` | Upper limit for automatic activation. |
| `IndentSize` / `UseTabs` | `4` / `0` | Indentation of the pretty view. |
| `SpaceAfterColon` | `1` | `"a": 1` instead of `"a":1`. |
| `AllowComments` | `1` | Do not report `//` and `/* */` comments as errors. |
| `SyncCaret` | `1` | Caret synchronisation. |
| `AutoFitHeight` | `1` | When docked at the bottom, size the panel so the editor just shows the whole document. |
| `MaxEditorHeightPercent` | `50` | Upper limit (10-90) for the editor's share of the height when fitting. |
| `DocumentToPanelDelayMs` | `150` | Debounce for editor → panel. |
| `PanelToDocumentDelayMs` | `120` | Debounce for panel → editor. |
| `ReformatDelayMs` | `1500` | Idle time before the panel is re-indented (`0` = never). |

## How it works

```
             SCN_MODIFIED (hidden mirror view)        prettyPrint + diffText
 document ───────────────────────────────────────▶ ─────────────────────────▶ panel
 (Notepad++ buffer, compact)                                                (own Scintilla)
          ◀─────────────────────────────────────── ◀─────────────────────────
             one undo action                         diffContent (token aware, minimal)
```

- **Mirror view:** a hidden Scintilla view shares the buffer's document (`SCI_SETDOCPOINTER`). It
  reports every document change, whichever view is active. All writes to the document go through it,
  even when the buffer is not shown at the moment, for example after *Ctrl+Tab* or *Save all*.
- **`diffContent`:** compares the *significant* characters of both sides (whitespace outside strings
  removed) and grows the change to whole tokens. It then maps the change back to byte positions in the
  document. Inserted parts are always compact.
- **Significant index:** maps caret positions between the compact and pretty layouts.
- **Unterminated strings:** while you type a string in the panel that is not closed yet, whitespace
  becomes part of the string, so positions cannot be mapped. The update is held back until the string
  is closed, or until the file is saved or the panel loses focus.

The JSON engine in `src/core` does not depend on Windows. Its unit tests run on Linux and Windows,
including a randomised round-trip test.

## Building

Visual Studio 2022 (x64), run inside `JSONPlusPlus/`:

```bash
cmake -S . -B build -A x64
cmake --build build --config Release
ctest --test-dir build -C Release
```

Core tests only (any platform):

```bash
cmake -S . -B build && cmake --build build && ctest --test-dir build
```

CI: `.github/workflows/jsonplusplus.yml` in the repository root.

## Project layout

```
src/core/      JSON lexer, pretty printer, compactor, validator, diff (portable C++20)
src/plugin/    Notepad++ plugin: entry points, docking panel, settings
tests/         unit tests for the core
third_party/   Notepad++ plugin headers (GPL) and Scintilla headers
```

## Known limitations

- Comments in compact files are kept, but a `// line comment` needs its line break, even in compact
  form.
- The panel's own undo history is cleared when the document changes. Use Notepad++'s *Undo*, which
  covers all edits.

## License

GPL-3.0-or-later, see `LICENSE`. The headers in `third_party/npp` come from the
Notepad++ project.
