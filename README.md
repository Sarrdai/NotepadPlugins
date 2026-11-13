# Notepad++ Plugins

Sammel-Repository für eigene Notepad++-Plugins. Jedes Plugin liegt in einem eigenen Ordner
und hat dort seine eigene README.

| Ordner | Plugin | Technik |
| :-- | :-- | :-- |
| [`JSONPlusPlus/`](JSONPlusPlus) | JSON++ – synchronisierte Pretty-Ansicht für JSON | C++20, CMake |
| [`TiffPlusPlus/`](TiffPlusPlus) | Tiff++ – XML-Metadaten und Bildansicht für TIFF | C# / .NET Framework 4.8 |
| [`MarkdownLive/`](MarkdownLive) | Markdown Live – Live-Vorschau und Editor-Befehle | C++17, CMake, WebView2 |

## Build und Auslieferung (GitHub Actions)

Alle Plugins werden einheitlich auf `windows-latest` (Visual Studio 2022 / MSVC bzw. MSBuild)
für **x64** gebaut, getestet und als Artefakt hochgeladen.

| Workflow | Auslöser | Ergebnis |
| :-- | :-- | :-- |
| *JSON++*, *Tiff++*, *MarkdownLive* | Push/PR mit Änderungen im jeweiligen Ordner, manuell (*Run workflow*) | `<Plugin>-x64` |
| *Bundle* | manuell (*Run workflow*), Tag `v*` | alle Einzel-Artefakte + `NotepadPlugins-x64`; bei Tags zusätzlich ein Release mit allen ZIPs |

Ein Push, der nur ein Plugin betrifft, baut nur dieses Plugin. Änderungen an
`.github/actions/` bauen alle Plugins.

Gemeinsame Konventionen (`.github/actions/upload-plugin`):

- Jedes Artefakt `<Plugin>-x64.zip` enthält genau einen Ordner `<Plugin>/` (Name = DLL-Name),
  der direkt nach `…\Notepad++\plugins\` kopiert wird.
- Vor dem Upload werden die von Notepad++ benötigten DLL-Exporte geprüft.
- `NotepadPlugins-x64.zip` enthält alle Plugin-Ordner nebeneinander: entpacken und den Inhalt
  nach `…\Notepad++\plugins\` kopieren.

## Installation

1. Notepad++ (64 Bit) beenden.
2. Artefakt bzw. Release-ZIP entpacken und den/die Plugin-Ordner nach `…\Notepad++\plugins\`
   kopieren, z. B. `plugins\MarkdownLive\MarkdownLive.dll`.
3. Notepad++ starten.

Voraussetzungen der einzelnen Plugins stehen in deren README.
