using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Runtime.InteropServices;
using System.Text;
using System.Windows.Forms;
using TiffPlusPlus.Core;
using TiffPlusPlus.Npp;
using TiffPlusPlus.Stacks;
using TiffPlusPlus.Viewer;

namespace TiffPlusPlus
{
    internal static class Plugin
    {
        public const string PluginName = "Tiff++";

        private static NppHost _host;
        private static Control _invoker;
        private static ImagePanelHost _panelHost;
        private static StackController _stacks;
        private static int _panelCmdIndex;
        private static int _pinCmdIndex;
        private static Settings _settings;
        private static int _highlightCmdIndex;
        private static bool _ready;   // NPPN_READY received; commands run before only restore docked panels

        private static IntPtr _originalWndProc;
        private static Win32.WndProc _wndProcDelegate; // kept alive for the process lifetime

        public static FuncItems Commands { get; } = new FuncItems();

        public static void Initialize(NppData data)
        {
            _host = new NppHost(data);
            _invoker = new Control();
            _invoker.CreateControl();

            _settings = new Settings(_host.PluginsConfigDirectory);
            _stacks = new StackController(_host) { HighlightChanges = _settings.HighlightChanges };
            _stacks.SliceChanged += () => _panelHost?.Refresh();

            // Same layout as the other plugins: panel, pin, commands, options, About.
            const byte VK_LEFT = 0x25, VK_RIGHT = 0x27;
            _panelCmdIndex = Commands.Add("Toggle Image Panel", Safe(ToggleImagePanel));
            _pinCmdIndex = Commands.Add("Pin Image Panel to Current Image", Safe(PinImagePanel));
            Commands.Add("-", null);
            Commands.Add("Open TIFF...", Safe(OpenTiffFromDialog));
            Commands.Add("Open TIFF Stack (Folder)...", Safe(OpenStackFromFolderDialog));
            Commands.Add("-", null);
            Commands.Add("Next Slice", Safe(() => _stacks.Step(+1)), false, new ShortcutKey(false, true, false, VK_RIGHT));
            Commands.Add("Previous Slice", Safe(() => _stacks.Step(-1)), false, new ShortcutKey(false, true, false, VK_LEFT));
            Commands.Add("Values Across Stack (Element at Caret)", Safe(() => _stacks.ShowValuesAtCaret()), false,
                new ShortcutKey(true, true, false, (byte)'V'));
            Commands.Add("-", null);
            _highlightCmdIndex = Commands.Add("Highlight Changes to Previous Slice", Safe(ToggleHighlight), _settings.HighlightChanges);
            Commands.Add("-", null);
            Commands.Add("About", Safe(ShowAbout));

            HookDropFiles();
        }

        public static void OnNotification(NmHdr header)
        {
            try
            {
                switch (header.Code)
                {
                    case NppMsg.NPPN_TBMODIFICATION:
                        ToolbarIcon.Register(_host, Commands.GetCmdId(_panelCmdIndex));
                        break;
                    case NppMsg.NPPN_READY:
                        _ready = true;
                        CleanupOldViewFiles();
                        _panelHost?.OnDocumentActivated();   // a restored panel closes if no TIFF is active
                        break;
                    case NppMsg.NPPN_FILEOPENED:
                        OnFileOpened(header.IdFrom);
                        break;
                    case NppMsg.NPPN_BUFFERACTIVATED:
                        _stacks.TryGetActive(out _, out _);   // rebuilds restored stacks and their highlighting
                        if (_ready) _panelHost?.OnDocumentActivated();
                        else _panelHost?.Refresh();
                        break;
                    case NppMsg.NPPN_SHUTDOWN:
                        _stacks.CloseAll();
                        ShutdownPanel();
                        break;
                }
            }
            catch (Exception ex)
            {
                ReportError(ex);
            }
        }

        /// <summary>Shows the XML of a TIFF through a temp view file (reads header/IFD/tag only).</summary>
        public static string OpenTiff(string tiffPath)
        {
            string viewPath = XmlViewFiles.GetViewPath(tiffPath);
            if (_host.SwitchToFile(viewPath))
                return viewPath;

            var meta = TiffMetadataReader.Read(tiffPath);
            XmlViewFiles.Write(tiffPath, XmlDocumentBuilder.Build(meta));
            if (_host.OpenFile(viewPath))
                _host.SetBufferLang(_host.CurrentBufferId, NppMsg.L_XML);
            return viewPath;
        }

        public static string CurrentImagePath() =>
            XmlViewFiles.ResolveImagePath(_host.GetPathFromBufferId(_host.CurrentBufferId));

        /// <summary>Image (or stack slice) belonging to the active tab, or null.</summary>
        private static ImageContext CurrentImageContext()
        {
            if (_stacks.TryGetActive(out var stack, out string viewPath))
                return new ImageContext { ImagePath = stack.Files[stack.Index], StackKey = viewPath, Index = stack.Index, StackFiles = stack.Files };
            string path = CurrentImagePath();
            return path == null ? null : new ImageContext { ImagePath = path };
        }

        private static NppFuncItemDelegate Safe(Action action) => () =>
        {
            try
            {
                action();
            }
            catch (Exception ex)
            {
                ReportError(ex);
            }
        };

        public static void SetPanelChecked(bool isChecked) =>
            _host.SetMenuItemCheck(Commands.GetCmdId(_panelCmdIndex), isChecked);

        public static void SetPinChecked(bool isChecked) =>
            _host.SetMenuItemCheck(Commands.GetCmdId(_pinCmdIndex), isChecked);

        public static void ReportError(Exception ex) =>
            MessageBox.Show(ex.ToString(), PluginName, MessageBoxButtons.OK, MessageBoxIcon.Error);

        // Fallback for File > Open, command line and session restore: Notepad++ already opened the
        // binary, so replace that buffer by the XML view once the current notification has finished.
        private static void OnFileOpened(IntPtr bufferId)
        {
            string path = _host.GetPathFromBufferId(bufferId);
            if (!TiffMetadataReader.IsTiffPath(path)) return;

            _invoker.BeginInvoke((Action)(() =>
            {
                try
                {
                    string viewPath = OpenTiff(path);
                    _host.CloseBuffer(bufferId);
                    _host.SwitchToFile(viewPath);
                }
                catch (Exception ex)
                {
                    ReportError(ex);
                }
            }));
        }

        private static void ShutdownPanel()
        {
            try
            {
                _panelHost?.Dispose();
            }
            catch (Exception)
            {
                // Never bother the user with errors while Notepad++ is closing.
            }
        }

        private static void OpenTiffFromDialog()
        {
            using (var dlg = new OpenFileDialog
            {
                Filter = "TIFF images (*.tiff;*.tif)|*.tiff;*.tif|All files (*.*)|*.*",
                Multiselect = true,
                Title = "Open TIFF XML",
            })
            {
                if (dlg.ShowDialog() != DialogResult.OK) return;
                if (dlg.FileNames.Length == 1) OpenTiff(dlg.FileName);
                else _stacks.Open(TiffStack.NameForFiles(dlg.FileNames), dlg.FileNames);
            }
        }

        private static void OpenStackFromFolderDialog()
        {
            if (!_ready) return;   // never show the dialog while Notepad++ restores its window layout
            using (var dlg = new FolderBrowserDialog { Description = "Folder with TIFF images (top level only)" })
            {
                if (dlg.ShowDialog() == DialogResult.OK)
                    _stacks.Open(FolderName(dlg.SelectedPath), TiffStack.FilesInFolder(dlg.SelectedPath));
            }
        }

        private static ImagePanelHost PanelHost =>
            _panelHost ?? (_panelHost = new ImagePanelHost(_host, _settings, _panelCmdIndex, CurrentImageContext,
                (key, index) => _stacks.GoTo(key, index)));

        private static void ToggleImagePanel()
        {
            // Before NPPN_READY, Notepad++ restores the docked panel: show it, never toggle it off.
            if (!_ready) PanelHost.ShowForRestore();
            else PanelHost.Toggle();
        }

        private static void PinImagePanel()
        {
            // Older versions registered the panel with id 1 (this command): restore the panel instead.
            if (!_ready) PanelHost.ShowForRestore();
            else PanelHost.TogglePinFromMenu();
        }

        private static void ToggleHighlight()
        {
            bool enabled = !_stacks.HighlightChanges;
            _stacks.SetHighlightChanges(enabled);
            _host.SetMenuItemCheck(Commands.GetCmdId(_highlightCmdIndex), enabled);
            _settings.HighlightChanges = enabled;
            _settings.Save();
        }

        private static string FolderName(string folder) =>
            Path.GetFileName(folder.TrimEnd(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar)) is string name && name.Length > 0 ? name : folder;

        private static void ShowAbout() =>
            MessageBox.Show(
                "Shows the XML embedded in TIFF images (*.tiff, *.tif) with XML highlighting.\r\n" +
                "Only the TIFF header and directory are read, never the pixel data.\r\n\r\n" +
                "Plugins > " + PluginName + " > Toggle Image Panel (or the toolbar button) opens a viewer\r\n" +
                "for the image of the active tab.\r\n\r\n" +
                "Stacks: drop a folder or several TIFF files. Alt+Left/Right switches slices,\r\n" +
                "changed lines are highlighted, Ctrl+Alt+V charts the value at the caret across all slices.",
                PluginName, MessageBoxButtons.OK, MessageBoxIcon.Information);

        // --- Drag & drop interception -------------------------------------------------------------

        private static void HookDropFiles()
        {
            _wndProcDelegate = MainWndProc;
            _originalWndProc = Win32.SetWindowLongPtr(_host.Handle, Win32.GWLP_WNDPROC,
                Marshal.GetFunctionPointerForDelegate(_wndProcDelegate));
        }

        private static IntPtr MainWndProc(IntPtr hWnd, uint msg, IntPtr wParam, IntPtr lParam)
        {
            if (msg == Win32.WM_DROPFILES)
            {
                try
                {
                    if (HandleDrop(wParam))
                        return IntPtr.Zero;
                }
                catch (Exception ex)
                {
                    ReportError(ex);
                }
            }
            return Win32.CallWindowProc(_originalWndProc, hWnd, msg, wParam, lParam);
        }

        /// <summary>Opens dropped TIFFs as XML before Notepad++ reads the whole binary file.</summary>
        private static bool HandleDrop(IntPtr hDrop)
        {
            uint count = Win32.DragQueryFile(hDrop, 0xFFFFFFFF, null, 0);
            var files = new List<string>();
            for (uint i = 0; i < count; i++)
            {
                var sb = new StringBuilder(32768);
                Win32.DragQueryFile(hDrop, i, sb, (uint)sb.Capacity);
                files.Add(sb.ToString());
            }

            var tiffs = files.Where(TiffMetadataReader.IsTiffPath).ToList();
            var tiffFolders = files.Where(f => Directory.Exists(f) && TiffStack.FilesInFolder(f).Any()).ToList();

            // Let Notepad++ handle drops without TIFFs (including folder-as-workspace drops).
            if (tiffs.Count == 0 && tiffFolders.Count == 0)
                return false;

            Win32.DragFinish(hDrop);
            foreach (string file in files.Where(f => !tiffs.Contains(f) && !Directory.Exists(f)))
                _host.OpenFile(file);
            foreach (string folder in tiffFolders)
                _stacks.Open(FolderName(folder), TiffStack.FilesInFolder(folder));
            if (tiffs.Count == 1) OpenTiff(tiffs[0]);
            else if (tiffs.Count > 1) _stacks.Open(TiffStack.NameForFiles(tiffs), tiffs);
            return true;
        }

        private static void CleanupOldViewFiles()
        {
            try
            {
                if (!Directory.Exists(XmlViewFiles.RootDirectory)) return;
                foreach (string dir in Directory.GetDirectories(XmlViewFiles.RootDirectory))
                {
                    if (Directory.GetLastWriteTimeUtc(dir) > DateTime.UtcNow.AddDays(-14)) continue;
                    foreach (string file in Directory.GetFiles(dir))
                        File.SetAttributes(file, FileAttributes.Normal);
                    Directory.Delete(dir, true);
                }
            }
            catch (Exception ex) when (ex is IOException || ex is UnauthorizedAccessException)
            {
                // Files may still be open in another Notepad++ instance; try again next start.
            }
        }
    }
}
