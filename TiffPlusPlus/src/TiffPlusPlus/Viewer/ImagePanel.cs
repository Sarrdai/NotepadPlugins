using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Runtime.InteropServices;
using System.Threading.Tasks;
using System.Web.Script.Serialization;
using System.Windows.Forms;
using Microsoft.Web.WebView2.Core;
using Microsoft.Web.WebView2.WinForms;
using TiffPlusPlus.Npp;

namespace TiffPlusPlus.Viewer
{
    /// <summary>What the panel should show for the active document: a single image or a slice of a stack.</summary>
    internal sealed class ImageContext
    {
        public string ImagePath { get; set; }
        public string StackKey { get; set; }
        public int Index { get; set; }
        public IReadOnlyList<string> StackFiles { get; set; }
        public bool IsStack => StackKey != null;
    }

    /// <summary>Owns the docking registration and the active/inactive state of the image panel.</summary>
    internal sealed class ImagePanelHost : IDisposable
    {
        private readonly NppHost _npp;
        private readonly Func<ImageContext> _currentContext;
        private readonly Action<string, int> _sliceRequested;
        private readonly int _commandIndex;
        private ImagePanelForm _form;
        private IntPtr _tbDataPtr;
        private bool _active;

        /// <param name="commandIndex">Menu index of the panel command; Notepad++ calls it to restore the panel at startup.</param>
        public ImagePanelHost(NppHost npp, int commandIndex, Func<ImageContext> currentContext, Action<string, int> sliceRequested)
        {
            _npp = npp;
            _commandIndex = commandIndex;
            _currentContext = currentContext;
            _sliceRequested = sliceRequested;
        }

        public void Toggle()
        {
            if (_form == null)
            {
                _form = new ImagePanelForm();
                _form.PanelClosed += () => SetActive(false);
                _form.SliceRequested += (key, index) => _sliceRequested(key, index);
                RegisterDockingPanel();   // Notepad++ shows the panel on registration
                SetActive(true);
                return;
            }

            if (_active)
            {
                Win32.SendMessage(_npp.Handle, NppMsg.NPPM_DMMHIDE, IntPtr.Zero, _form.Handle);
                SetActive(false);
            }
            else
            {
                Win32.SendMessage(_npp.Handle, NppMsg.NPPM_DMMSHOW, IntPtr.Zero, _form.Handle);
                SetActive(true);
            }
        }

        /// <summary>Called when the active tab or the active slice changed.</summary>
        public void Refresh()
        {
            if (_active) _form.ShowImage(_currentContext());
        }

        private void SetActive(bool active)
        {
            _active = active;
            Plugin.SetPanelChecked(active);
            if (active) _form.ShowImage(_currentContext());
            else _form.Unload();   // free pixel memory while the panel is hidden
        }

        private void RegisterDockingPanel()
        {
            var data = new TbData
            {
                HClient = _form.Handle,
                PszName = Marshal.StringToHGlobalUni("TIFF Image"),
                DlgId = _commandIndex,
                UMask = NppMsg.DWS_DF_CONT_RIGHT,
                PszModuleName = Marshal.StringToHGlobalUni(Path.GetFileName(typeof(Plugin).Assembly.Location)),
            };
            // Notepad++ keeps the pointer, so the memory is intentionally never freed.
            _tbDataPtr = Marshal.AllocHGlobal(Marshal.SizeOf(typeof(TbData)));
            Marshal.StructureToPtr(data, _tbDataPtr, false);
            Win32.SendMessage(_npp.Handle, NppMsg.NPPM_DMMREGASDCKDLG, IntPtr.Zero, _tbDataPtr);
        }

        public void Dispose() => _form?.Dispose();
    }

    /// <summary>Docked client window hosting the WebView2 based viewer (viewer.html).</summary>
    internal sealed class ImagePanelForm : Form
    {
        private const string Host = "https://npptiff.local/";
        private const int AutoStackSamples = 12;
        private static readonly JavaScriptSerializer Json = new JavaScriptSerializer();

        private readonly PixelCache _cache = new PixelCache(IntPtr.Size == 8 ? 768L << 20 : 192L << 20);
        private readonly HashSet<string> _prefetching = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        private WebView2 _webView;
        private Task _initTask;
        private bool _viewerReady;
        private ImageContext _context;
        private string _loadedPath;
        private PixelImage _image;
        private int _loadSequence;

        public event Action PanelClosed;
        public event Action<string, int> SliceRequested;

        public ImagePanelForm()
        {
            Text = "TIFF Image";
            FormBorderStyle = FormBorderStyle.None;
            BackColor = System.Drawing.Color.FromArgb(30, 30, 30);
        }

        public void ShowImage(ImageContext context)
        {
            _context = context;
            if (_initTask == null) _initTask = InitializeWebViewAsync();
            if (_viewerReady) LoadRequestedImage();
        }

        public void Unload()
        {
            _context = null;
            _loadedPath = null;
            _image = null;
            _cache.Clear();
            _loadSequence++;
            if (_viewerReady) Post(new { type = "clear" });
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
                string loaderDir = EmbeddedDependencies.ExtractWebView2Loader();
                CoreWebView2Environment.SetLoaderDllFolderPath(loaderDir);

                // The default user data folder would be next to notepad++.exe (usually not writable).
                string userData = Path.Combine(
                    Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "TiffPlusPlus", "WebView2");
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
                core.Navigate(Host + "viewer.html");
            }
            catch (Exception ex)
            {
                MessageBox.Show(
                    "The image panel needs the Microsoft Edge WebView2 Runtime.\r\n\r\n" + ex.Message,
                    Plugin.PluginName, MessageBoxButtons.OK, MessageBoxIcon.Warning);
            }
        }

        private void OnWebMessageReceived(object sender, CoreWebView2WebMessageReceivedEventArgs e)
        {
            string json = e.WebMessageAsJson;
            if (json == "\"ready\"")
            {
                _viewerReady = true;
                _loadedPath = null;
                LoadRequestedImage();
                return;
            }

            var message = Json.Deserialize<Dictionary<string, object>>(json);
            if (message == null || !message.TryGetValue("type", out var type)) return;
            switch (type as string)
            {
                case "slice" when _context?.IsStack == true:
                    SliceRequested?.Invoke(_context.StackKey, Convert.ToInt32(message["index"]));
                    break;
                case "autoStack" when _context?.IsStack == true:
                    AutoContrastStack(_context, Convert.ToDouble(message["clip"], System.Globalization.CultureInfo.InvariantCulture));
                    break;
            }
        }

        private async void LoadRequestedImage()
        {
            ImageContext context = _context;
            string path = context?.ImagePath;
            if (path == null)
            {
                Post(new { type = "none" });
                return;
            }
            if (string.Equals(path, _loadedPath, StringComparison.OrdinalIgnoreCase)) return;

            int sequence = ++_loadSequence;
            _loadedPath = path;
            try
            {
                PixelImage image = _cache.Get(path);
                if (image == null)
                {
                    Post(new { type = "loading", name = Path.GetFileName(path), stackKey = context.StackKey });
                    image = await Task.Run(() => PixelLoader.Load(path));
                    _cache.Add(path, image);
                }
                if (sequence != _loadSequence) return;   // a newer request superseded this one

                _image = image;
                Post(new
                {
                    type = "image",
                    url = Host + "pixels/" + sequence,
                    name = Path.GetFileName(path),
                    path,
                    width = image.Width,
                    height = image.Height,
                    format = image.Format,
                    sourceBits = image.SourceBitsPerPixel,
                    frames = image.FrameCount,
                    stack = context.IsStack
                        ? new { key = context.StackKey, index = context.Index, count = context.StackFiles.Count }
                        : null,
                });
                if (context.IsStack) Prefetch(context);
            }
            catch (Exception ex)
            {
                if (sequence != _loadSequence) return;
                _loadedPath = null;
                Post(new { type = "error", name = Path.GetFileName(path), message = ex.Message });
            }
        }

        /// <summary>Decodes the neighbouring slices in the background, so browsing feels instant.</summary>
        private async void Prefetch(ImageContext context)
        {
            foreach (int i in new[] { context.Index + 1, context.Index - 1 })
            {
                if (i < 0 || i >= context.StackFiles.Count) continue;
                string file = context.StackFiles[i];
                if (_cache.Get(file) != null || !_prefetching.Add(file)) continue;
                try
                {
                    _cache.Add(file, await Task.Run(() => PixelLoader.Load(file)));
                }
                catch (Exception)
                {
                    // Errors are reported when the slice is actually shown.
                }
                finally
                {
                    _prefetching.Remove(file);
                }
            }
        }

        /// <summary>Window from the joint histogram of evenly sampled slices.</summary>
        private async void AutoContrastStack(ImageContext context, double clipPercent)
        {
            Post(new { type = "status", text = "Computing stack contrast..." });
            try
            {
                var files = context.StackFiles;
                int samples = Math.Min(AutoStackSamples, files.Count);
                var picked = Enumerable.Range(0, samples).Select(i => files[(int)((long)i * (files.Count - 1) / Math.Max(1, samples - 1))]).Distinct().ToList();
                var cached = picked.ToDictionary(f => f, f => _cache.Get(f), StringComparer.OrdinalIgnoreCase);

                var (lo, hi) = await Task.Run(() =>
                {
                    var bins = new long[65536];
                    long total = 0;
                    foreach (string file in picked)
                    {
                        PixelImage img = cached[file] ?? PixelLoader.Load(file);
                        total += Histogram.Accumulate(img, bins);
                    }
                    return Histogram.Percentiles(bins, total, clipPercent);
                });
                Post(new { type = "window", lo, hi, text = $"Stack contrast from {picked.Count} slices" });
            }
            catch (Exception ex)
            {
                Post(new { type = "status", text = "Stack contrast failed: " + ex.Message });
            }
        }

        private void OnWebResourceRequested(object sender, CoreWebView2WebResourceRequestedEventArgs e)
        {
            var env = _webView.CoreWebView2.Environment;
            string path = new Uri(e.Request.Uri).AbsolutePath;

            if (path == "/viewer.html")
            {
                Stream html = typeof(ImagePanelForm).Assembly.GetManifestResourceStream("TiffPlusPlus.Viewer.viewer.html");
                e.Response = env.CreateWebResourceResponse(html, 200, "OK",
                    "Content-Type: text/html; charset=utf-8\r\nCache-Control: no-store");
            }
            else if (path.StartsWith("/pixels/", StringComparison.Ordinal) && _image != null)
            {
                e.Response = env.CreateWebResourceResponse(new MemoryStream(_image.Data, false), 200, "OK",
                    "Content-Type: application/octet-stream\r\nCache-Control: no-store");
            }
            else
            {
                e.Response = env.CreateWebResourceResponse(null, 404, "Not Found", "");
            }
        }

        private void Post(object message)
        {
            _webView?.CoreWebView2?.PostWebMessageAsJson(Json.Serialize(message));
        }

        protected override void Dispose(bool disposing)
        {
            if (disposing) _webView?.Dispose();
            base.Dispose(disposing);
        }
    }
}
