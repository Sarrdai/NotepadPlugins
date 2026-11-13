# Tiff++ – TIFF-XML-Viewer für Notepad++

Notepad++-Plugin für TIFF-Bilder (z. B. Röntgenbilder) mit eingebettetem XML:

- **Datei in Notepad++ ziehen → nur der XML-Teil wird angezeigt**, eingerückt und mit XML-Syntax-Highlighting.
  Gelesen werden nur TIFF-Header, Verzeichnis (IFD) und der XML-Tag – die Pixeldaten werden nie geladen.
- **Kein XML vorhanden?** Der Tab zeigt den Hinweis `NO XML FOUND` und listet stattdessen die TIFF-Tags auf.
- **Bild-Panel (optional)** rechts andockbar, basierend auf WebView2. Das Bild wird nur geladen, solange das Panel sichtbar ist.
- Endungen: `.tiff`, `.tif` – Groß-/Kleinschreibung egal.

## Bild-Panel

`Plugins > Tiff++ > Image panel` oder der Bild-Button in der Toolbar blendet das Panel ein/aus. Es zeigt immer das Bild des aktiven Tabs.

| Aktion | Bedienung |
|---|---|
| In Bereich zoomen | Linke Maustaste: Kasten aufziehen |
| Zoom zurücksetzen | **Reset**, Doppelklick, `F` oder `Esc` |
| Zoomen an Mausposition | Mausrad, `+` / `-`, **1:1** für Originalpixel |
| Verschieben | Mittlere Maustaste, Shift+Ziehen oder Leertaste+Ziehen |
| Autokontrast | **Auto** (ganzes Bild, `A`), **Auto view** (nur sichtbarer Ausschnitt, `V`), Clip-Prozent wählbar |
| Kontrast manuell | Min/Max-Regler und Zahlenfelder, Marker im Histogramm ziehen, Gamma |
| Fensterung wie im Röntgen-Viewer | Rechte Maustaste ziehen: ↔ Fensterbreite, ↕ Level |
| Weitere | **Invert** (`I`), **Lock** (Kontrast/Zoom beim Bildwechsel behalten), **PNG** (Export mit aktuellem Kontrast), Pixelwert unter der Maus in der Statuszeile |

## Image Stacks

Einen **Ordner** (nur oberste Ebene) oder **mehrere TIFFs** in Notepad++ ziehen öffnet einen einzigen Stack-Tab
(`<Name> [stack].xml`) statt vieler Tabs – vergleichbar mit einem virtuellen Stack in ImageJ. Alternativ:
`Open TIFF stack (folder)...` bzw. Mehrfachauswahl in `Open TIFF XML...`. Sortierung natürlich nach Dateiname (`img2` vor `img10`).

| Funktion | Bedienung |
|---|---|
| Schicht wechseln (XML-Tab) | `Alt+→` / `Alt+←` (anpassbar über *Einstellungen > Tastenkürzel*) |
| Änderungen hervorheben | Zeilen, die sich zur vorherigen Schicht unterscheiden, sind farbig hinterlegt (abschaltbar im Menü) |
| Werte über den Stack | Cursor auf eine Wertzeile (z. B. `<Voltage>`), dann `Ctrl+Alt+V`: Diagramm + Tabelle über alle Schichten. Weitere Werte kommen als zusätzliche Diagramme hinzu. Klick auf Punkt/Zeile springt zur Schicht |
| CSV-Export | Im Werte-Fenster: Semikolon-getrennt, UTF-8, Werte unverändert aus dem XML |
| Bild-Panel | Slider, `Bild↑`/`Bild↓`, `Pos1`/`Ende`, Play mit 2–20 fps. Kontrast und Zoom bleiben beim Blättern erhalten; optional *Auto per slice*; *Auto stack* berechnet den Kontrast aus Stichproben (12 Schichten) des ganzen Stacks |

Beim Blättern wird nur das XML der jeweiligen Schicht gelesen (Header/IFD). Das Bild-Panel dekodiert nur die aktuelle
Schicht und lädt die Nachbarn im Hintergrund vor (LRU-Cache, max. 768 MB bei x64). Scroll- und Cursorposition im
XML-Tab bleiben beim Wechsel erhalten, sodass man dieselbe Stelle über alle Schichten verfolgen kann.

Alle Funktionen liegen unter einem Menüeintrag: `Plugins > Tiff++`.

## Installation

1. Build-Artefakt `TiffPlusPlus-x64` (Workflow *Tiff++*) bzw. das Bundle `NotepadPlugins-x64`
   (Workflow *Bundle*) aus GitHub Actions herunterladen oder selbst bauen (siehe unten).
2. Den Ordner `TiffPlusPlus` (enthält nur `TiffPlusPlus.dll`) nach `%ProgramFiles%\Notepad++\plugins\` kopieren.
   Den alten Ordner `plugins\NppTiffXml` aus früheren Versionen löschen.
3. Notepad++ neu starten.

Voraussetzungen: Windows 10/11 mit .NET Framework 4.8 und Microsoft Edge WebView2 Runtime (beides bei Windows 11 vorinstalliert).

## Bauen

Im Ordner `TiffPlusPlus/` (CI: `.github/workflows/tiffplusplus.yml` im Repository-Root):

```bash
msbuild src/TiffPlusPlus/TiffPlusPlus.csproj -restore -p:Configuration=Release -p:Platform=x64
```

Wichtig ist `msbuild` aus Visual Studio bzw. den Build Tools (nicht `dotnet build`): Nur damit werden die nativen
Exporte für Notepad++ erzeugt. `dotnet build` kompiliert nur (z. B. unter Linux).

Tests der TIFF-/XML-Logik (laufen auch unter Linux):

```bash
dotnet test tests/TiffPlusPlus.Tests
```

Optional gegen eine echte Datei: Umgebungsvariable `TIFFPP_SAMPLE` auf den Pfad setzen.

## Funktionsweise

- **Drag & Drop**: Das Plugin fängt `WM_DROPFILES` im Notepad++-Hauptfenster ab, bevor Notepad++ die Binärdatei lädt.
- **Datei > Öffnen, Kommandozeile, Sitzungswiederherstellung**: Notepad++ lädt die Datei hier selbst; das Plugin
  ersetzt den Binär-Tab danach sofort durch die XML-Ansicht (funktioniert, ist aber bei großen Dateien langsamer).
  Schnell geht es auch über `Plugins > Tiff++ > Open TIFF XML...`.
- **XML-Suche**: Alle TIFF-Tags vom Typ BYTE/ASCII/SBYTE/UNDEFINED, deren Inhalt mit `<` beginnt (z. B. privater Tag 50635,
  XMP 700, ImageDescription 270). Fallback: `<?xml` in den letzten 4 MB der Datei. Classic TIFF und BigTIFF, II und MM.
- **Anzeige**: Das XML wird in eine schreibgeschützte Datei `%TEMP%\TiffPlusPlus\<hash>\<Bildname>.xml` geschrieben und geöffnet
  (Tab-Name = Bildname, Highlighting über die Endung). Eine `.source`-Datei daneben merkt sich das Original-TIFF
  für das Bild-Panel. Einträge älter als 14 Tage werden beim Start entfernt.
- **Eine Datei**: Die WebView2-Wrapper (`Microsoft.Web.WebView2.Core/WinForms.dll`) sind in die Plugin-DLL eingebettet und
  werden aus dem Speicher geladen. Der native `WebView2Loader.dll` wird beim ersten Öffnen des Panels nach
  `%LOCALAPPDATA%\TiffPlusPlus\loader\<x64|x86>` entpackt. Die Browser-Engine selbst ist die in Windows installierte WebView2-Runtime.
- **Bilddekodierung**: Windows Imaging Component (unkomprimiert, LZW, Deflate, PackBits, JPEG). 16-Bit-Graustufen
  werden unverändert an den Viewer übergeben; Kontrast/Gamma werden per Lookup-Table im WebView berechnet.
  Bei mehrseitigen TIFFs wird die erste Seite angezeigt.
