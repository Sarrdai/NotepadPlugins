using System;
using System.IO;
using System.Linq;
using System.Windows.Forms;
using PathTrail.Core;
using PathTrail.Npp;
using PathTrail.Panel;

namespace PathTrail
{
    internal static class Plugin
    {
        public const string PluginName = "PathTrail";

        private static NppHost _host;
        private static Settings _settings;
        private static TrailLog _log;
        private static Tracker _tracker;
        private static TrailPanelHost _panelHost;
        private static int _panelCmdIndex;
        private static bool _ready;   // NPPN_READY received; files opened before belong to the restored session

        public static FuncItems Commands { get; } = new FuncItems();

        public static void Initialize(NppData data)
        {
            _host = new NppHost(data);
            _settings = new Settings(_host.PluginsConfigDirectory);
            _log = new TrailLog(Path.Combine(_settings.ConfigDirectory, "PathTrail", "PathTrail.log"));
            _tracker = new Tracker(_log);
            _tracker.Changed += () => _panelHost?.Invalidate();

            // Same layout as the other plugins: panel, commands, About (no pin: the trail is not bound to a document).
            _panelCmdIndex = Commands.Add("Toggle PathTrail Panel", Safe(TogglePanel));
            Commands.Add("-", null);
            Commands.Add("Search Trail", Safe(() => PanelHost.FocusSearch()));
            Commands.Add("Clear Trail...", Safe(ClearTrail));
            Commands.Add("-", null);
            Commands.Add("About", Safe(ShowAbout));
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
                        OnReady();
                        break;
                    case NppMsg.NPPN_FILEOPENED when _ready:
                        _tracker.Opened(PathOf(header.IdFrom));
                        break;
                    case NppMsg.NPPN_BUFFERACTIVATED when _ready:
                        _tracker.Activated(PathOf(header.IdFrom));
                        break;
                    case NppMsg.NPPN_FILESAVED when _ready:
                        _tracker.Saved(PathOf(header.IdFrom));
                        break;
                    case NppMsg.NPPN_FILEBEFORECLOSE when _ready:
                        _tracker.Closing(PathOf(header.IdFrom));
                        break;
                    case NppMsg.NPPN_DARKMODECHANGED:
                        _panelHost?.ThemeChanged();
                        break;
                    case NppMsg.NPPN_BEFORESHUTDOWN:
                        _ready = false;   // closing the session at exit is no user action
                        _tracker.EndActivation();
                        break;
                    case NppMsg.NPPN_CANCELSHUTDOWN:
                        _ready = true;
                        _tracker.Activated(PathOf(_host.CurrentBufferId));
                        break;
                    case NppMsg.NPPN_SHUTDOWN:
                        _ready = false;
                        _tracker.EndActivation();
                        ShutdownPanel();
                        break;
                }
            }
            catch (Exception ex)
            {
                ReportError(ex);
            }
        }

        private static void OnReady()
        {
            _log.Load(DateTime.UtcNow, _settings.RetentionDays);
            if (!_settings.Imported)
            {
                ImportNppHistory();
                _settings.Imported = true;
            }
            _settings.Save();   // writes the defaults, so they can be found and edited in PathTrail.ini

            _ready = true;
            _tracker.Activated(PathOf(_host.CurrentBufferId));
            _panelHost?.Invalidate();
        }

        /// <summary>Seeds the trail with the Notepad++ recent file list and session (config.xml lives two levels above plugins\config).</summary>
        private static void ImportNppHistory()
        {
            string settingsDir = Path.GetDirectoryName(Path.GetDirectoryName(_settings.ConfigDirectory.TrimEnd('\\')));
            if (settingsDir == null) return;
            var paths = NppHistoryImport.ReadPaths(Path.Combine(settingsDir, "config.xml"), Path.Combine(settingsDir, "session.xml"));
            foreach (TrailEvent e in NppHistoryImport.ToEvents(paths, DateTime.UtcNow).AsEnumerable().Reverse())
                _log.Append(e);
        }

        private static string PathOf(IntPtr bufferId) => _host.GetPathFromBufferId(bufferId);

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

        public static void ReportError(Exception ex) =>
            MessageBox.Show(ex.ToString(), PluginName, MessageBoxButtons.OK, MessageBoxIcon.Error);

        private static TrailPanelHost PanelHost =>
            _panelHost ?? (_panelHost = new TrailPanelHost(_host, _settings, _log, _panelCmdIndex));

        private static void TogglePanel()
        {
            // Before NPPN_READY, Notepad++ restores the docked panel: show it, never toggle it off.
            if (!_ready) PanelHost.ShowForRestore();
            else PanelHost.Toggle();
        }

        private static void ClearTrail()
        {
            if (MessageBox.Show("Delete the whole trail (all recorded files)?", PluginName,
                    MessageBoxButtons.OKCancel, MessageBoxIcon.Question) != DialogResult.OK) return;
            _tracker.EndActivation();
            _log.Clear();
            _tracker.Activated(PathOf(_host.CurrentBufferId));
            _panelHost?.Invalidate();
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

        private static void ShowAbout() =>
            MessageBox.Show(
                "Records the files you open in Notepad++ and lets you jump back to them and their folders.\r\n\r\n" +
                "History: every file per day. Click a folder in the path to open it (Explorer, terminal or\r\n" +
                "workspace, see 'Folder click'), click the file name to open the file. Right click shows all actions.\r\n" +
                "Hotspots: folders you are currently working in, e.g. an experiment with several active subfolders.\r\n\r\n" +
                $"The trail keeps {_settings.RetentionDays} days (RetentionDays in PathTrail.ini) and is stored in\r\n" +
                Path.Combine(_settings.ConfigDirectory, "PathTrail"),
                PluginName, MessageBoxButtons.OK, MessageBoxIcon.Information);
    }
}
