using System;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Runtime.InteropServices;
using System.Threading.Tasks;
using System.Web.Script.Serialization;
using System.Windows.Forms;
using Microsoft.Web.WebView2.Core;
using Microsoft.Web.WebView2.WinForms;
using PathTrail.Core;
using PathTrail.Npp;

namespace PathTrail.Panel
{
    /// <summary>Owns the docking registration and the visible state of the trail panel.</summary>
    internal sealed class TrailPanelHost : IDisposable
    {
        private readonly NppHost _npp;
        private readonly Settings _settings;
        private readonly TrailLog _log;
        private readonly int _commandIndex;
        private TrailPanelForm _form;
        private bool _active;

        /// <param name="commandIndex">Menu index of the panel command; Notepad++ calls it to restore the panel at startup.</param>
        public TrailPanelHost(NppHost npp, Settings settings, TrailLog log, int commandIndex)
        {
            _npp = npp;
            _settings = settings;
            _log = log;
            _commandIndex = commandIndex;
        }

        public void Toggle()
        {
            if (_form == null)
            {
                _form = new TrailPanelForm(_npp, _settings, _log);
                _form.PanelClosed += () => SetActive(false);
                RegisterDockingPanel();   // Notepad++ shows the panel on registration
                SetActive(true);
                return;
            }
            SetVisible(!_active);
        }

        /// <summary>Shows the panel while Notepad++ restores its layout at startup (never hides it).</summary>
        public void ShowForRestore()
        {
            if (!_active) Toggle();
        }

        /// <summary>Shows the panel and puts the caret into the search box.</summary>
        public void FocusSearch()
        {
            if (!_active) Toggle();
            _form.FocusSearch();
        }

        /// <summary>The log changed: recompute the views (debounced) while the panel is visible.</summary>
        public void Invalidate()
        {
            if (_active) _form.ScheduleRefresh();
        }

        public void ThemeChanged() => _form?.PostTheme();

        private void SetVisible(bool visible)
        {
            Win32.SendMessage(_npp.Handle, visible ? NppMsg.NPPM_DMMSHOW : NppMsg.NPPM_DMMHIDE, IntPtr.Zero, _form.Handle);
            SetActive(visible);
        }

        private void SetActive(bool active)
        {
            _active = active;
            Plugin.SetPanelChecked(active);
            if (active) _form.OnShown();
        }

        private void RegisterDockingPanel()
        {
            var data = new TbData
            {
                HClient = _form.Handle,
                PszName = Marshal.StringToHGlobalUni(Plugin.PluginName),
                DlgId = _commandIndex,
                UMask = NppMsg.DWS_DF_CONT_RIGHT,
                PszModuleName = Marshal.StringToHGlobalUni(Path.GetFileName(typeof(Plugin).Assembly.Location)),
            };
            // Notepad++ keeps the pointer, so the memory is intentionally never freed.
            IntPtr ptr = Marshal.AllocHGlobal(Marshal.SizeOf(typeof(TbData)));
            Marshal.StructureToPtr(data, ptr, false);
            Win32.SendMessage(_npp.Handle, NppMsg.NPPM_DMMREGASDCKDLG, IntPtr.Zero, ptr);
        }

        public void Dispose() => _form?.Dispose();
    }

    /// <summary>Docked client window hosting the WebView2 based trail view (trail.html).</summary>
    internal sealed class TrailPanelForm : Form
    {
        private const string Host = "https://pathtrail.local/";
        private static readonly JavaScriptSerializer Json = new JavaScriptSerializer { MaxJsonLength = int.MaxValue };
        private static readonly TimeSpan ExistsCacheTime = TimeSpan.FromMinutes(1);

        private readonly NppHost _npp;
        private readonly Settings _settings;
        private readonly TrailLog _log;
        private readonly IPathMatcher _matcher = new SubstringMatcher();
        private readonly Timer _refreshTimer = new Timer { Interval = 400 };
        private readonly ConcurrentDictionary<string, (bool exists, DateTime checkedUtc)> _exists =
            new ConcurrentDictionary<string, (bool, DateTime)>(TrailPath.Comparer);
        private WebView2 _webView;
        private Task _initTask;
        private bool _ready;
        private bool _focusSearchPending;
        private string _query = "";
        private string _range = "week";

        public event Action PanelClosed;

        public TrailPanelForm(NppHost npp, Settings settings, TrailLog log)
        {
            _npp = npp;
            _settings = settings;
            _log = log;
            Text = Plugin.PluginName;
            FormBorderStyle = FormBorderStyle.None;
            BackColor = npp.IsDarkMode ? System.Drawing.Color.FromArgb(30, 30, 30) : System.Drawing.Color.White;
            _refreshTimer.Tick += (s, e) =>
            {
                _refreshTimer.Stop();
                PostData();
            };
        }

        /// <summary>Panel became visible: start the WebView once, afterwards just refresh.</summary>
        public void OnShown()
        {
            if (_initTask == null) _initTask = InitializeWebViewAsync();
            else if (_ready) PostData();
        }

        public void ScheduleRefresh()
        {
            if (!_ready) return;
            _refreshTimer.Stop();
            _refreshTimer.Start();
        }

        public void FocusSearch()
        {
            if (_ready)
            {
                _webView.Focus();
                Post(new { type = "focusSearch" });
            }
            else
            {
                _focusSearchPending = true;
            }
        }

        public void PostTheme()
        {
            if (_ready) Post(new { type = "theme", dark = _npp.IsDarkMode });
        }

        protected override void WndProc(ref Message m)
        {
            if (m.Msg == Win32.WM_NOTIFY)
            {
                var header = (NmHdr)Marshal.PtrToStructure(m.LParam, typeof(NmHdr));
                if (header.Code == NppMsg.DMN_CLOSE)
                    PanelClosed?.Invoke();
            }
            base.WndProc(ref m);
        }

        private async Task InitializeWebViewAsync()
        {
            try
            {
                CoreWebView2Environment.SetLoaderDllFolderPath(EmbeddedDependencies.ExtractWebView2Loader());
                // The default user data folder would be next to notepad++.exe (usually not writable).
                string userData = Path.Combine(_settings.ConfigDirectory, "PathTrail", "WebView2");
                var env = await CoreWebView2Environment.CreateAsync(null, userData);

                _webView = new WebView2 { Dock = DockStyle.Fill, DefaultBackgroundColor = BackColor };
                Controls.Add(_webView);
                await _webView.EnsureCoreWebView2Async(env);

                var core = _webView.CoreWebView2;
                core.Settings.AreDevToolsEnabled = true;
                core.Settings.IsStatusBarEnabled = false;
                core.Settings.AreDefaultContextMenusEnabled = false;
                core.AddWebResourceRequestedFilter(Host + "*", CoreWebView2WebResourceContext.All);
                core.WebResourceRequested += OnWebResourceRequested;
                core.WebMessageReceived += OnWebMessageReceived;
                core.Navigate(Host + "trail.html");
            }
            catch (Exception ex)
            {
                MessageBox.Show("The PathTrail panel needs the Microsoft Edge WebView2 Runtime.\r\n\r\n" + ex.Message,
                    Plugin.PluginName, MessageBoxButtons.OK, MessageBoxIcon.Warning);
            }
        }

        private void OnWebResourceRequested(object sender, CoreWebView2WebResourceRequestedEventArgs e)
        {
            var env = _webView.CoreWebView2.Environment;
            if (new Uri(e.Request.Uri).AbsolutePath == "/trail.html")
            {
                Stream html = typeof(TrailPanelForm).Assembly.GetManifestResourceStream("PathTrail.Panel.trail.html");
                e.Response = env.CreateWebResourceResponse(html, 200, "OK",
                    "Content-Type: text/html; charset=utf-8\r\nCache-Control: no-store");
            }
            else
            {
                e.Response = env.CreateWebResourceResponse(null, 404, "Not Found", "");
            }
        }

        private void OnWebMessageReceived(object sender, CoreWebView2WebMessageReceivedEventArgs e)
        {
            try
            {
                HandleMessage(Json.Deserialize<Dictionary<string, object>>(e.WebMessageAsJson));
            }
            catch (Exception ex)
            {
                Plugin.ReportError(ex);
            }
        }

        private void HandleMessage(Dictionary<string, object> message)
        {
            string Arg(string key) => message.TryGetValue(key, out object v) ? v as string : null;
            string path = Arg("path");

            switch (Arg("type"))
            {
                case "ready":
                    _ready = true;
                    PostTheme();
                    PostSettings();
                    PostData();
                    if (_focusSearchPending) FocusSearch();
                    _focusSearchPending = false;
                    break;
                case "openFile":
                    OpenFile(path);
                    break;
                case "folder":
                    OpenFolder(path, Arg("action"));
                    break;
                case "showInExplorer":
                    if (File.Exists(path)) ShellActions.ShowInExplorer(path);
                    else OpenFolder(TrailPath.Parent(path), "explorer");
                    break;
                case "copy":
                    Clipboard.SetText(path);
                    break;
                case "search":
                    _query = Arg("query") ?? "";
                    PostSearch();
                    break;
                case "range":
                    _range = Arg("value") ?? "week";
                    PostData();
                    break;
                case "folderClick":
                    if (Enum.TryParse(Arg("value"), true, out FolderAction action))
                    {
                        _settings.FolderClick = action;
                        _settings.Save();
                    }
                    break;
            }
        }

        private void OpenFile(string path)
        {
            if (File.Exists(path))
            {
                if (!_npp.SwitchToFile(path)) _npp.OpenFile(path);
                return;
            }
            _exists[path] = (false, DateTime.UtcNow);
            Post(new { type = "missing", paths = new[] { path } });
            string folder = ShellActions.ExistingFolder(TrailPath.Parent(path));
            if (MessageBox.Show($"The file no longer exists:\r\n{path}" +
                    (folder == null ? "" : $"\r\n\r\nOpen the nearest existing folder?\r\n{folder}"),
                    Plugin.PluginName, folder == null ? MessageBoxButtons.OK : MessageBoxButtons.YesNo,
                    MessageBoxIcon.Information) == DialogResult.Yes)
                ShellActions.OpenFolder(folder, _settings.FolderClick);
        }

        /// <summary>Runs the folder action; "open" shows a file dialog starting in the folder.</summary>
        private void OpenFolder(string folder, string action)
        {
            string existing = ShellActions.ExistingFolder(folder);
            if (existing == null)
            {
                MessageBox.Show($"The folder is not available:\r\n{folder}", Plugin.PluginName,
                    MessageBoxButtons.OK, MessageBoxIcon.Information);
                return;
            }

            if (action == "open")
            {
                using (var dlg = new OpenFileDialog { InitialDirectory = existing, Multiselect = true, Title = "Open" })
                {
                    if (dlg.ShowDialog() == DialogResult.OK)
                        foreach (string file in dlg.FileNames) _npp.OpenFile(file);
                }
                return;
            }

            FolderAction folderAction = Enum.TryParse(action, true, out FolderAction parsed) ? parsed : _settings.FolderClick;
            ShellActions.OpenFolder(existing, folderAction);
        }

        private void PostSettings() => Post(new { type = "settings", folderClick = _settings.FolderClick.ToString().ToLowerInvariant() });

        private void PostData()
        {
            var events = _log.Events;
            DateTime now = DateTime.UtcNow;
            DateTime since = _range == "today" ? DateTime.Now.Date.ToUniversalTime()
                : _range == "week" ? now.AddDays(-7)
                : DateTime.MinValue;
            Hotspots spots = TrailIndex.FindHotspots(events.Where(e => e.TimeUtc >= since), now,
                new HotspotOptions { HalfLifeHours = _settings.HalfLifeHours });

            var history = TrailIndex.History(events).Select(day => new
            {
                day = day.Day.ToString("yyyy-MM-dd"),
                files = day.Files.Select(FileDto).ToList(),
            }).ToList();

            Post(new
            {
                type = "data",
                range = _range,
                history,
                projects = spots.Projects.Select(FolderDto).ToList(),
                working = spots.WorkingFolders.Select(FolderDto).ToList(),
            });
            if (_query.Length > 0) PostSearch();
            CheckExistence(history.SelectMany(d => d.files.Select(f => f.path)));
        }

        private void PostSearch()
        {
            var results = _query.Trim().Length == 0 ? new List<FileEntry>()
                : TrailIndex.Search(_log.Events, _query, _matcher, DateTime.UtcNow, _settings.HalfLifeHours);
            Post(new { type = "search", query = _query, results = results.Select(FileDto).ToList() });
            CheckExistence(results.Select(r => r.Path));
        }

        private static FileDto FileDto(FileEntry f) => new FileDto
        {
            path = f.Path,
            segments = Segments(f.Path),
            last = f.LastUtc.ToString("o"),
            opens = f.Opens,
            seconds = Math.Round(f.Seconds),
        };

        private static object FolderDto(FolderEntry f) => new
        {
            path = f.Path,
            segments = Segments(f.Path),
            last = f.LastUtc.ToString("o"),
            files = f.Files,
            subfolders = f.ActiveSubfolders,
            score = Math.Round(f.Score, 2),
        };

        private static List<object> Segments(string path) =>
            TrailPath.Split(path).Select(s => (object)new { label = s.Label, path = s.FullPath }).ToList();

        /// <summary>
        /// Marks files that no longer exist. Runs in the background (network drives can block for seconds),
        /// checks each root once and caches results for a minute.
        /// </summary>
        private async void CheckExistence(IEnumerable<string> paths)
        {
            DateTime now = DateTime.UtcNow;
            var pending = paths.Distinct(TrailPath.Comparer)
                .Where(p => !_exists.TryGetValue(p, out var c) || now - c.checkedUtc > ExistsCacheTime)
                .ToList();
            if (pending.Count > 0)
            {
                await Task.Run(() =>
                {
                    foreach (var root in pending.GroupBy(p => TrailPath.Split(p)[0].FullPath, TrailPath.Comparer))
                    {
                        bool rootOk = Directory.Exists(root.Key);
                        foreach (string p in root) _exists[p] = (rootOk && File.Exists(p), DateTime.UtcNow);
                    }
                });
            }
            Post(new { type = "missing", paths = _exists.Where(kv => !kv.Value.exists).Select(kv => kv.Key).ToList() });
        }

        private void Post(object message)
        {
            if (IsDisposed) return;
            _webView?.CoreWebView2?.PostWebMessageAsJson(Json.Serialize(message));
        }

        protected override void Dispose(bool disposing)
        {
            if (disposing)
            {
                _refreshTimer.Dispose();
                _webView?.Dispose();
            }
            base.Dispose(disposing);
        }
    }

    internal sealed class FileDto
    {
        public string path { get; set; }
        public List<object> segments { get; set; }
        public string last { get; set; }
        public int opens { get; set; }
        public double seconds { get; set; }
    }
}
