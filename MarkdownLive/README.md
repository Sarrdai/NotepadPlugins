# Markdown Live – Markdown-Plugin für Notepad++

Live-Vorschau und Komfortfunktionen für Markdown in Notepad++ (64 Bit).

Die Vorschau läuft in einem andockbaren Fenster auf Basis von **WebView2** (Chromium) und wird
beim Tippen inkrementell aktualisiert. Alle Bibliotheken sind im Plugin enthalten – es wird
keine Internetverbindung benötigt.

## Funktionen

**Live-Vorschau**

- GitHub Flavored Markdown: Tabellen, Task-Listen, Durchstreichen, Autolinks, Fußnoten,
  Definitionslisten, `==Markierung==`, Hoch-/Tiefstellung, Emoji-Kürzel (`:rocket:`),
  GitHub-Alerts (`> [!NOTE]`), YAML-Front-Matter, eingebettetes HTML (bereinigt)
- Syntax-Highlighting für Codeblöcke (highlight.js) inkl. „Copy“-Schaltfläche
- Mathematik mit KaTeX (`$…$`, `$$…$$`) und Diagramme mit Mermaid (` ```mermaid `)
- Synchrones Scrollen in beide Richtungen, Markierung des Absatzes an der Schreibmarke
- Doppelklick in der Vorschau springt zur Quellzeile, Checkboxen sind in der Vorschau anklickbar
- Lokale Bilder (relativ zum Dokument), Links öffnen im Browser bzw. `.md`-Dateien in Notepad++
- Helles/dunkles Design folgt automatisch dem Dark Mode von Notepad++
- Export als eigenständiges **HTML** oder als **PDF**
- **Anpinnen**: Die Pin-Schaltfläche oben rechts in der Vorschau (oder *Pin Live Preview to Current
  File*) hält die Vorschau beim aktuellen Dokument, während im Editor andere Dateien bearbeitet
  werden – z. B. eine Markdown-Anleitung neben dem Quelltext. Ist das angepinnte Dokument sichtbar
  (auch in der zweiten Ansicht), wird es weiterhin live aktualisiert und synchron gescrollt; ein
  Doppelklick oder Abhaken in der Vorschau holt es nach vorn. Der Titel des Panels zeigt das
  angepinnte Dokument. Erneutes Klicken, Schließen der Vorschau oder des Dokuments hebt den Pin auf.
- **Automatisches Schließen**: Beim Wechsel auf eine Nicht-Markdown-Datei schließt sich die
  Vorschau und öffnet sich bei der nächsten Markdown-Datei wieder (wie das Bild-Panel von Tiff++).
  Ungespeicherte Dokumente ohne Endung lassen sie offen; nur der Pin hält sie für andere Dateien fest.

**Code-Highlighting im Editor**

Fenced Code Blocks (` ```python `, `~~~cs` …) werden im Editor mit dem Lexer der angegebenen Sprache
eingefärbt – so, als wäre die ganze Datei in dieser Sprache, nur eben auf den Codeblock beschränkt.
Farben und Schlüsselwörter stammen aus der Notepad++-Konfiguration: aktives Theme (`stylers.xml`
bzw. `themes\*.xml`, inklusive eigener Schlüsselwörter aus dem Stil-Konfigurator) und `langs.xml`.
Ein Wechsel des Themes, des Dark Mode oder eine Änderung im Stil-Konfigurator wird sofort übernommen.

- Sprachangabe per Name, Kürzel oder Dateiendung: `py`, `cs`/`c#`, `js`, `ts`, `sh`, `ps1`, `bat`,
  `sql`, `xml`, `html`, `json`, `yaml`, `ini`, `rs`, `go`, `lua` … – alles, was Notepad++ kennt
- Blöcke ohne oder mit unbekannter Sprache (z. B. `mermaid`) bleiben unverändert
- Nur Schriftfarben werden übernommen (keine Fett-/Kursivschrift und keine Hintergrundfarben), da
  die Einfärbung über einen Scintilla-Indikator über dem Markdown-Lexer liegt
- Benötigt Notepad++ 8.4 oder neuer; abschaltbar über *Highlight Code Blocks in Editor*

**Komfortfunktionen** (Menü *Erweiterungen → Markdown Live*)

| Befehl | Tastenkürzel | Beschreibung |
| :-- | :-- | :-- |
| Toggle Live Preview | `Strg+Umschalt+M` | Vorschau ein-/ausblenden (auch per Symbolleiste) |
| Bold / Italic | `Strg+Umschalt+E` / `Strg+Umschalt+I` | Auswahl oder Wort unter der Schreibmarke umschalten |
| Strikethrough, Inline Code, Code Block | – | Auswahl umschließen bzw. wieder entfernen |
| Heading: Add # / Remove # | – | Überschriftenebene ändern (auch mehrzeilig) |
| Toggle Bullet / Numbered List, Blockquote | – | Zeilenpräfixe für die markierten Zeilen umschalten |
| Task: Add / Check / Uncheck | – | Aufgabe anlegen bzw. abhaken |
| Insert Link | – | Nutzt eine URL aus der Zwischenablage automatisch als Ziel |
| Insert Image… / Paste Image from Clipboard | – | Bild verlinken bzw. Screenshot als PNG in `images\` ablegen |
| Insert Table / Format Table | – | Tabelle einfügen bzw. Spalten ausrichten |
| Table: Insert Row Above / Below, Delete Row | – | Zeile relativ zur Schreibmarke einfügen bzw. löschen (Kopf- und Trennzeile bleiben geschützt) |
| Table: Insert Column Left / Right, Delete Column | – | Spalte einfügen bzw. löschen; die Tabelle wird dabei neu ausgerichtet, die Schreibmarke springt in die neue Zelle |
| Insert / Update Table of Contents | – | Inhaltsverzeichnis zwischen `<!-- TOC -->`-Markern |
| Export as HTML… / PDF… | – | Export der aktuellen Vorschau |
| Pin Live Preview to Current File | – | Vorschau an das aktuelle Dokument anheften bzw. lösen |

Zusätzlich werden Listen, Aufgaben und Zitate bei **Enter** automatisch fortgesetzt
(nummerierte Listen zählen weiter; Enter auf einem leeren Listenpunkt beendet die Liste).

Die Optionen am Ende des Menüs werden gespeichert:

| Option | Standard | Beschreibung |
| :-- | :-- | :-- |
| Synchronize Scrolling | an | Editor und Vorschau scrollen gemeinsam |
| Continue Lists on Enter | an | Listenfortsetzung (siehe oben) |
| Open Preview Automatically for Markdown Files | aus | Vorschau beim Wechsel auf eine Markdown-Datei öffnen (und für andere Dateien wieder schließen) |
| Highlight Code Blocks in Editor | an | Code-Highlighting in Codeblöcken (siehe oben) |

Weitere Tastenkürzel lassen sich unter *Einstellungen → Tastatur → Plugin-Befehle* vergeben.

**Tool Panel** (optional)

Eine schmale Leiste mit Schaltflächen für alle oben genannten Befehle und Optionen – ohne Umweg
über das Menü. Sie wird über *Tool Panel: Top / Bottom / Hidden* im Plugin-Menü oder per
Rechtsklick auf die Leiste **oben** (unter der Notepad++-Symbolleiste), **unten** (über der
Statusleiste) oder gar nicht angezeigt; Standard ist ausgeblendet, die Wahl wird gespeichert.

- Umschalter (Vorschau, Anpinnen und alle Optionen) zeigen ihren aktuellen Zustand an
- Tooltips nennen den Befehl samt aktuell zugewiesenem Tastenkürzel
- Ist das Fenster zu schmal, sind die übrigen Befehle über `»` am rechten Rand erreichbar
- Folgt dem Dark Mode von Notepad++ und skaliert mit der Bildschirmauflösung (DPI)

## Installation

1. Artefakt `MarkdownLive-x64` (Workflow *MarkdownLive*) bzw. das Bundle `NotepadPlugins-x64`
   (Workflow *Bundle*) unter *Actions* oder als Anhang eines Releases herunterladen.
2. Notepad++ beenden und den enthaltenen Ordner `MarkdownLive` nach
   `…\Notepad++\plugins\` kopieren, sodass `plugins\MarkdownLive\MarkdownLive.dll` existiert.
   Der Ordner muss vollständig kopiert werden (`WebView2Loader.dll` und `assets\` gehören dazu).
3. Notepad++ starten, eine `.md`-Datei öffnen und `Strg+Umschalt+M` drücken.

Voraussetzungen: Notepad++ 8.x (64 Bit) und die *Microsoft Edge WebView2 Runtime*. Diese ist
unter Windows 11 und aktuellem Windows 10 bereits vorhanden; fehlt sie, zeigt das
Vorschaufenster einen Hinweis mit Download-Adresse.

## Aufbau

| Pfad | Inhalt |
| :-- | :-- |
| `src/` | Plugin-DLL (C++17): Notepad++-Schnittstelle, WebView2-Panel, Tool Panel, Editor-Befehle |
| `src/md_text.*` | Plattformunabhängige Textlogik (Listen, Tabellen, TOC, Slugs) |
| `src/code_syntax.*` | Plattformunabhängig: Codeblöcke finden, Sprache → Lexer, Notepad++-XML (Themes, `langs.xml`, `config.xml`) lesen |
| `src/code_highlight.*` | Einfärben der Codeblöcke im Editor (Lexer per `NPPM_CREATELEXER`, Indikator mit Textfarbe) |
| `web/` | Vorschau-Seite (markdown-it, highlight.js, KaTeX, Mermaid, DOMPurify, morphdom) |
| `tests/`, `web/test/` | Unit-Tests der Textlogik, Browser-Tests der Vorschau |

## Build

Windows mit Visual Studio 2022 und Node.js >= 20, im Ordner `MarkdownLive/`
(CI: `.github/workflows/markdownlive.yml` im Repository-Root):

```bash
cd web && npm ci && npm run build && cd ..   # Vorschau-Seite -> web/dist
cmake -S . -B build -A x64
cmake --build build --config Release           # -> build/package/MarkdownLive
ctest --test-dir build -C Release
```

Das WebView2-SDK wird beim Konfigurieren automatisch von NuGet geladen (Prüfsumme hinterlegt).
Unter Linux baut derselbe CMake-Aufruf nur die Unit-Tests der Textlogik.

## Teststand

- Textlogik: Unit-Tests (`ctest`), Vorschau-Seite: End-to-End-Tests in Headless-Chromium (`npm test`)
- Plugin-DLL (damals MinGW-Build): in Notepad++ 8.7.1 unter Wine geprüft (Laden, Menü, Andocken, alle Editor-Befehle,
  Listenfortsetzung, Einstellungen)
- Die Einbettung der WebView2-Laufzeit selbst lässt sich unter Wine nicht ausführen und ist
  daher nur kompiliert, aber noch nicht auf einem echten Windows-System gelaufen.
- Anpinnen, automatisches Schließen und Code-Highlighting im Editor: Erkennung der
  Codeblöcke, Sprachzuordnung und das Lesen der Notepad++-Konfiguration sind per Unit-Test
  abgedeckt, die Pin-Schaltfläche per Browser-Test; das Zusammenspiel mit Notepad++ ist bisher nur
  kompiliert und noch nicht in Notepad++ selbst gelaufen.
- Tool Panel: unter Wine in einem Testfenster mit dem Aufbau des Notepad++-Hauptfensters geprüft
  (zwei Rebars, Symbolleiste, Statusleiste): oben/unten/ausblenden, Klicks, Umschalter, Tooltips,
  Überlauf-Menü, Dark Mode, 200 % DPI, Beenden. In Notepad++ selbst ist es noch nicht gelaufen.
  Notepad++ bietet für eine solche Leiste keine Plugin-Schnittstelle; sie wird deshalb als
  zusätzliches Band in die Rebar-Leisten des Hauptfensters eingehängt (siehe `src/tool_panel.h`).

## Lizenzen der enthaltenen Komponenten

Das Paket enthält `WebView2Loader.dll` (Microsoft, BSD-artige Lizenz, siehe
`WebView2-LICENSE.txt`) sowie gebündelte Open-Source-Bibliotheken (überwiegend MIT/BSD/Apache-2.0;
die Lizenzhinweise stehen jeweils am Ende der Dateien in `assets\`).
