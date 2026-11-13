# Notepad++ Plugins

Collection of Notepad++ plugins. Each plugin lives in its own folder with its own README.

| Folder | Plugin | Technology |
| :-- | :-- | :-- |
| [`JSONPlusPlus/`](JSONPlusPlus) | JSON++ – synchronized, editable pretty view for compact JSON | C++20, CMake |
| [`TiffPlusPlus/`](TiffPlusPlus) | Tiff++ – embedded XML and image viewer for TIFF | C# / .NET Framework 4.8, WebView2 |
| [`MarkdownLive/`](MarkdownLive) | Markdown Live – live preview and editing helpers | C++17, CMake, WebView2 |

## Common behavior

- **Menu layout:** toggle panel, pin panel, commands, options, About – Title Case labels.
- **Panels** are titled with the plugin name, close automatically when a document of another type is activated and
  reopen for the next matching document. The **pin** (in the panel and in the menu) keeps a panel open on its document.
- **Toolbar button** for each panel, with icons for light and dark mode.
- **Settings** are stored as `<Plugin>.ini` in the Notepad++ plugin config folder
  (`Settings > Open Plugins Config Folder`); WebView2 data of the panels lives there as well.

## Build and delivery (GitHub Actions)

All plugins are built, tested and uploaded the same way on `windows-latest` (Visual Studio / MSVC or MSBuild), for
**x64**.

| Workflow | Trigger | Result |
| :-- | :-- | :-- |
| *JSON++*, *Tiff++*, *MarkdownLive* | push/PR with changes in the plugin folder, manually (*Run workflow*) | `<Plugin>-x64` |
| *Bundle* | manually (*Run workflow*), tag `v*` | all single artifacts + `NotepadPlugins-x64`; tags also create a release with all zips |
| *Format* | push/PR with C++ changes | checks the sources against `.clang-format` |

A push that only touches one plugin only builds that plugin. Changes in `.github/actions/` build all plugins.

Package conventions (`.github/actions/upload-plugin`):

- Each artifact `<Plugin>-x64.zip` contains exactly one folder `<Plugin>/` (name = DLL name) with the plugin files,
  its `README.md` and `LICENSE`. The folder is copied as-is to `…\Notepad++\plugins\`.
- The DLL exports required by Notepad++ are checked before the upload.
- `NotepadPlugins-x64.zip` contains all plugin folders side by side: extract it and copy the content to
  `…\Notepad++\plugins\`.

## Installation

1. Close Notepad++ (64-bit).
2. Extract the artifact or release zip and copy the plugin folder(s) to `…\Notepad++\plugins\`, e.g.
   `plugins\MarkdownLive\MarkdownLive.dll`.
3. Start Notepad++.

The requirements of each plugin are listed in its README.

## Repository conventions

- `.editorconfig` and `.clang-format` (C++) apply to all plugins; `.gitattributes` normalizes line endings.
- Code, comments, UI texts and documentation are in English.

## License

GPL-3.0-or-later, see [LICENSE](LICENSE). Third-party components keep their own licenses (see the plugin READMEs).
