# PathTrail – file and folder trail for Notepad++

Notepad++ plugin that records the files you open and gets you back to them – and to their folders – with one click.
Made for working across many scattered folders (experiments, data sets, quick looks) where the folder you were just in
is easily lost.

- **History** of the last 30 days, grouped by day. Each path is a **breadcrumb**: click a folder to open it, click the
  file name to open the file in Notepad++.
- **Search** over all recorded paths: every word must occur (`exp csv`), file name hits rank first, then by recency.
- **Hotspots**: the folders you are currently working in – detected automatically, no fixed folder level needed.
- Files that no longer exist are greyed out and struck through; opening one offers the nearest existing folder.

## Panel

`Plugins > PathTrail > Toggle PathTrail Panel` or the toolbar button shows/hides the panel (docked on the right).
Unlike the other plugins, the panel does not close for other documents and has no pin: the trail belongs to no
document.

| Action | How |
|---|---|
| Open a file | Click the file name (switches to the tab if it is already open) |
| Open a folder | Click a folder in the path; what happens is chosen with **Folder click** (see below) |
| All actions | Right click a folder: *Open in Explorer*, *Open terminal here*, *Open as workspace*, *Open file from here…*, *Copy path*. Right click a file: *Open*, *Show in Explorer*, *Copy path* |
| Search | Type into the search box, or `Plugins > PathTrail > Search Trail` (assign a shortcut in *Settings > Shortcut Mapper*). `↑`/`↓` select, `Enter` opens, `Esc` clears |

**Folder click** (saved in `PathTrail.ini`):

| Value | Opens |
|---|---|
| Explorer | the folder in Windows Explorer |
| Terminal | Windows Terminal in the folder (`wt -d`), `cmd` if Windows Terminal is not installed |
| Workspace | the folder in the Notepad++ *Folder as Workspace* panel |

## Hotspots

Every event is weighted – opening or saving a file counts 1, an active tab counts more the longer you stay
(saturates after 10 minutes, tab switches under 2 seconds are ignored) – and decays with a half-life of 24 hours.
The weight is added to every folder above the file.

- **Projects** are folders whose activity spreads over **at least two active subfolders** (each ≥ 10 % of the
  folder's weight, none ≥ 80 %). Working in `Exp_2026_11\run01`, `run02` and `run03` makes `Exp_2026_11` a project –
  not `D:\Data` above it (only one active child) and not `run03` (only part of the work). A common parent of several
  projects is only listed if it has activity of its own. Time span: today, 7 days or all.
- **Working folders** are the folders whose files you opened directly, ranked by weight.

## Recording

Recorded are opening, saving (including *Save As*), closing and the time a tab was active. Files Notepad++ restores
from the last session at startup and tabs without a file (`new 1`) are not recorded. On the first start, the
Notepad++ recent file list and session are imported once so the trail does not start empty.

`Plugins > PathTrail > Clear Trail...` deletes all recorded files.

## Settings and storage

`PathTrail.ini` in the Notepad++ plugin config folder (`Settings > Open Plugins Config Folder`):

| Key | Default | Meaning |
|---|---|---|
| `FolderClick` | `Explorer` | `Explorer`, `Terminal` or `Workspace` (also selectable in the panel) |
| `RetentionDays` | `30` | Older events are removed at startup |
| `HalfLifeHours` | `24` | Half-life of the hotspot and search weighting |

The trail is stored in `PathTrail\PathTrail.log` next to it: one tab separated line per event (time UTC, kind,
seconds, path). The WebView2 data of the panel lives in `PathTrail\WebView2`.

## Requirements

- Notepad++ 8.4 or newer, x64
- Microsoft Edge WebView2 Runtime (part of Windows 10/11)
- .NET Framework 4.8

## Build

```
dotnet test tests/PathTrail.Tests
msbuild src/PathTrail/PathTrail.csproj -restore -p:Configuration=Release -p:Platform=x64
```

The core logic (`src/PathTrail/Core`: log, tracker, history, search, hotspots, path segments, history import) is
platform independent and tested on .NET 8. The plugin DLL needs full-framework MSBuild on Windows for the native
exports. The resulting `PathTrail.dll` contains everything (WebView2 parts are embedded) and goes into
`…\Notepad++\plugins\PathTrail\`.
