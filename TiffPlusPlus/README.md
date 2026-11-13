# Tiff++ – TIFF XML viewer for Notepad++

Notepad++ plugin for TIFF images (e.g. X-ray images) with embedded XML:

- **Drop a file into Notepad++ → only the XML part is shown**, indented and with XML syntax highlighting.
  Only the TIFF header, the image file directory (IFD) and the XML tag are read – pixel data is never loaded.
- **No XML?** The tab shows `NO XML FOUND` and lists the TIFF tags instead.
- **Image panel (optional)** docked on the right, based on WebView2. The image is only loaded while the panel is visible.
- Extensions: `.tiff`, `.tif` – case insensitive.

## Image panel

`Plugins > Tiff++ > Toggle Image Panel` or the toolbar button shows/hides the panel. It always shows the image of the
active tab.

Switching to a document without a TIFF closes the panel; switching back to a TIFF reopens it (unsaved documents without
extension keep it open). The pin at the top right (or `Pin Image Panel to Current Image`) keeps the panel open on the
current image or stack until it is released or the panel is closed.

| Action | How |
|---|---|
| Zoom into a region | Left mouse button: drag a box |
| Reset zoom | **Reset**, double click, `F` or `Esc` |
| Zoom at mouse position | Mouse wheel, `+` / `-`, **1:1** for actual pixels |
| Resize panel / switch image | The visible section is scaled to stay completely visible and is kept when switching images (if it lies on the new image) |
| Pan | Middle mouse button, Shift+drag or Space+drag |
| Auto contrast | Contrast panel: **Full** (whole image, `A`), **View** (visible region only, `V`), selectable clip percentage; **Full range** uses min..max and sets the clip to 0 % |
| Manual contrast | Min/max sliders and number fields, drag the markers in the histogram, gamma |
| Windowing like an X-ray viewer | Drag with the right mouse button: ↔ window width, ↕ level |
| More | **Invert** (`I`), **Lock** (keep contrast when switching images), **PNG** (export with the current contrast), **Copy** (`Ctrl+C`, image with the current contrast to the clipboard), pixel value under the mouse in the status line |

New images start with auto contrast on the whole image.

## Image stacks

Dropping a **folder** (top level only) or **several TIFFs** into Notepad++ opens a single stack tab
(`<name> [stack].xml`) instead of many tabs – similar to a virtual stack in ImageJ. Alternatively:
`Open TIFF Stack (Folder)...` or a multiple selection in `Open TIFF...`. Files are sorted naturally by name (`img2`
before `img10`).

| Feature | How |
|---|---|
| Switch slices (XML tab) | `Alt+→` / `Alt+←` (customizable in *Settings > Shortcut Mapper*) |
| Highlight changes | Lines that differ from the previous slice are highlighted (can be switched off in the menu) |
| Values across the stack | Caret on a value line (e.g. `<Voltage>`), then `Ctrl+Alt+V`: chart + table across all slices. More values are added as further charts. Clicking a point/row jumps to that slice |
| CSV export | In the values window: semicolon separated, UTF-8, values unchanged from the XML |
| Image panel | Slider, `Page Up`/`Page Down`, `Home`/`End`, play at 2–20 fps. Contrast is kept while browsing; optional *Auto per slice*; *Auto stack* computes the contrast from samples (12 slices) of the whole stack |

While browsing, only the XML of the current slice is read (header/IFD). The image panel only decodes the current slice
and preloads its neighbours in the background (LRU cache, max. 768 MB on x64). Scroll and caret position in the XML tab
are kept when switching, so the same spot can be followed across all slices.

All commands are in one menu: `Plugins > Tiff++`.

## Settings

Stored in `TiffPlusPlus.ini` in the Notepad++ plugin config folder (`Settings > Open Plugins Config Folder`):
highlighting of changed lines, clip percentage and whether the contrast panel is expanded.

## Installation

1. Download the artifact `TiffPlusPlus-x64` (workflow *Tiff++*) or the bundle `NotepadPlugins-x64`
   (workflow *Bundle*) from GitHub Actions, or build it yourself (see below).
2. Copy the folder `TiffPlusPlus` (`TiffPlusPlus.dll`, README, LICENSE) to `%ProgramFiles%\Notepad++\plugins\`.
   Delete the folder `plugins\NppTiffXml` of earlier versions.
3. Restart Notepad++.

Requirements: Windows 10/11 with .NET Framework 4.8 and the Microsoft Edge WebView2 Runtime (both preinstalled on
Windows 11).

## Building

In the folder `TiffPlusPlus/` (CI: `.github/workflows/tiffplusplus.yml` in the repository root):

```bash
msbuild src/TiffPlusPlus/TiffPlusPlus.csproj -restore -p:Configuration=Release -p:Platform=x64
```

Use `msbuild` from Visual Studio or the Build Tools (not `dotnet build`): only it creates the native exports for
Notepad++. `dotnet build` only compiles (e.g. on Linux).

Tests of the TIFF/XML logic (also run on Linux):

```bash
dotnet test tests/TiffPlusPlus.Tests
```

Optionally against a real file: set the environment variable `TIFFPP_SAMPLE` to its path.

## How it works

- **Drag & drop**: the plugin intercepts `WM_DROPFILES` in the Notepad++ main window before Notepad++ loads the binary
  file.
- **File > Open, command line, session restore**: Notepad++ loads the file itself; the plugin then immediately replaces
  the binary tab with the XML view (works, but is slower for large files). `Plugins > Tiff++ > Open TIFF...` is fast as
  well.
- **XML search**: all TIFF tags of type BYTE/ASCII/SBYTE/UNDEFINED whose content starts with `<` (e.g. private tag
  50635, XMP 700, ImageDescription 270). Fallback: `<?xml` in the last 4 MB of the file. Classic TIFF and BigTIFF, II
  and MM.
- **Display**: the XML is written to a read-only file `%TEMP%\TiffPlusPlus\<hash>\<image name>.xml` and opened (tab
  name = image name, highlighting by extension). A `.source` file next to it remembers the original TIFF for the image
  panel. Entries older than 14 days are removed at startup.
- **Single file**: the WebView2 wrappers (`Microsoft.Web.WebView2.Core/WinForms.dll`) are embedded in the plugin DLL
  and loaded from memory. The native `WebView2Loader.dll` is extracted to `%LOCALAPPDATA%\TiffPlusPlus\loader\<x64|x86>`
  when the panel is opened for the first time. The browser engine itself is the WebView2 Runtime installed in Windows;
  its data folder is `TiffPlusPlus\WebView2` in the plugin config folder.
- **Image decoding**: Windows Imaging Component (uncompressed, LZW, Deflate, PackBits, JPEG). 16-bit grayscale is passed
  to the viewer unchanged; contrast/gamma are applied with a lookup table in the WebView. Multi-page TIFFs show the
  first page.

## License

GPL-3.0-or-later, see `LICENSE`.
