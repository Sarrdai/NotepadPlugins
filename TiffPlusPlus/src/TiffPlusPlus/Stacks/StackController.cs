using System;
using System.Collections.Generic;
using System.Text;
using System.Windows.Forms;
using TiffPlusPlus.Core;
using TiffPlusPlus.Npp;

namespace TiffPlusPlus.Stacks
{
    /// <summary>
    /// Owns the open TIFF stacks. A stack is shown in a single read-only tab; switching slices replaces
    /// the tab text in place (no file is rewritten, so Notepad++ shows no reload prompts).
    /// </summary>
    internal sealed class StackController
    {
        private const int FallbackMarker = 20;

        private readonly NppHost _npp;
        private readonly Dictionary<string, TiffStack> _stacks = new Dictionary<string, TiffStack>(StringComparer.OrdinalIgnoreCase);
        private readonly Dictionary<string, StackValuesForm> _valueForms = new Dictionary<string, StackValuesForm>(StringComparer.OrdinalIgnoreCase);
        private int _marker = -1;

        public StackController(NppHost npp) => _npp = npp;

        public bool HighlightChanges { get; set; } = true;

        /// <summary>Raised after the active stack switched to another slice.</summary>
        public event Action SliceChanged;

        public void Open(string name, IEnumerable<string> files)
        {
            var stack = TiffStack.Create(name, files);
            if (stack.Count == 0)
            {
                MessageBox.Show("No TIFF files (*.tif, *.tiff) found.", Plugin.PluginName, MessageBoxButtons.OK, MessageBoxIcon.Information);
                return;
            }
            if (stack.Count == 1)
            {
                Plugin.OpenTiff(stack.Files[0]);
                return;
            }

            string viewPath = XmlViewFiles.GetStackViewPath(stack);
            if (_npp.SwitchToFile(viewPath))
                return;   // already open: keep its current slice

            XmlViewFiles.WriteStack(stack, stack.BuildDisplayText(stack.Index));
            _stacks[viewPath] = stack;
            if (_npp.OpenFile(viewPath))
            {
                _npp.SetBufferLang(_npp.CurrentBufferId, NppMsg.L_XML);
                ApplyHighlight(stack);
            }
        }

        /// <summary>The stack shown in the active tab (rebuilt from its sidecar after a session restore).</summary>
        public bool TryGetActive(out TiffStack stack, out string viewPath)
        {
            stack = null;
            viewPath = _npp.GetPathFromBufferId(_npp.CurrentBufferId);
            if (viewPath == null) return false;
            if (_stacks.TryGetValue(viewPath, out stack)) return true;
            if (!XmlViewFiles.IsStackView(viewPath)) return false;

            stack = XmlViewFiles.LoadStack(viewPath);
            if (stack == null || stack.Count == 0) return false;
            _stacks[viewPath] = stack;
            ApplyHighlight(stack);
            return true;
        }

        public void Step(int delta)
        {
            if (TryGetActive(out var stack, out string viewPath))
                GoTo(viewPath, stack.Index + delta);
        }

        public void GoTo(string viewPath, int index)
        {
            if (!_stacks.TryGetValue(viewPath, out var stack)) return;
            if (!string.Equals(_npp.GetPathFromBufferId(_npp.CurrentBufferId), viewPath, StringComparison.OrdinalIgnoreCase)
                && !_npp.SwitchToFile(viewPath))
                return;

            int previous = stack.Index;
            stack.Index = index;
            if (stack.Index == previous) return;

            Render(stack);
            if (_valueForms.TryGetValue(viewPath, out var form)) form.SetCurrentSlice(stack.Index);
            SliceChanged?.Invoke();
        }

        public void SetHighlightChanges(bool enabled)
        {
            HighlightChanges = enabled;
            if (TryGetActive(out var stack, out _)) ApplyHighlight(stack);
        }

        /// <summary>Collects the value under the caret from every slice into the values window.</summary>
        public void ShowValuesAtCaret()
        {
            if (!TryGetActive(out var stack, out string viewPath))
            {
                MessageBox.Show("Open a TIFF stack first (drop a folder or several TIFF files).",
                    Plugin.PluginName, MessageBoxButtons.OK, MessageBoxIcon.Information);
                return;
            }

            IntPtr sci = _npp.CurrentScintilla;
            IntPtr pos = Win32.SendMessage(sci, Sci.SCI_GETCURRENTPOS, IntPtr.Zero, IntPtr.Zero);
            int line = Win32.SendMessage(sci, Sci.SCI_LINEFROMPOSITION, pos, IntPtr.Zero).ToInt32() - TiffStack.HeaderLineCount;
            var path = XmlValuePath.FromLine(stack.GetSliceText(stack.Index), line);
            if (path == null)
            {
                MessageBox.Show("Place the caret on a line with a value, e.g. <Voltage>120</Voltage>.",
                    Plugin.PluginName, MessageBoxButtons.OK, MessageBoxIcon.Information);
                return;
            }

            if (!_valueForms.TryGetValue(viewPath, out var form) || form.IsDisposed)
            {
                form = new StackValuesForm(stack, index => GoTo(viewPath, index));
                form.FormClosed += (s, e) => _valueForms.Remove(viewPath);
                _valueForms[viewPath] = form;
                form.Show(new NppWindow(_npp.Handle));
            }
            form.Activate();
            form.AddSeries(path);
            form.SetCurrentSlice(stack.Index);
        }

        public void CloseAll()
        {
            foreach (var form in new List<StackValuesForm>(_valueForms.Values)) form.Close();
        }

        private void Render(TiffStack stack)
        {
            IntPtr sci = _npp.CurrentScintilla;
            IntPtr firstLine = Win32.SendMessage(sci, Sci.SCI_GETFIRSTVISIBLELINE, IntPtr.Zero, IntPtr.Zero);
            long caret = Win32.SendMessage(sci, Sci.SCI_GETCURRENTPOS, IntPtr.Zero, IntPtr.Zero).ToInt64();

            byte[] text = Encoding.UTF8.GetBytes(stack.BuildDisplayText(stack.Index) + "\0");
            Win32.SendMessage(sci, Sci.SCI_SETREADONLY, IntPtr.Zero, IntPtr.Zero);
            Win32.SendMessage(sci, Sci.SCI_SETTEXT, IntPtr.Zero, text);
            Win32.SendMessage(sci, Sci.SCI_SETSAVEPOINT, IntPtr.Zero, IntPtr.Zero);
            Win32.SendMessage(sci, Sci.SCI_EMPTYUNDOBUFFER, IntPtr.Zero, IntPtr.Zero);
            Win32.SendMessage(sci, Sci.SCI_SETREADONLY, new IntPtr(1), IntPtr.Zero);

            // Keep caret and scroll position, so the same XML element stays in view while browsing.
            long length = Win32.SendMessage(sci, Sci.SCI_GETTEXTLENGTH, IntPtr.Zero, IntPtr.Zero).ToInt64();
            Win32.SendMessage(sci, Sci.SCI_GOTOPOS, new IntPtr(Math.Min(caret, length)), IntPtr.Zero);
            Win32.SendMessage(sci, Sci.SCI_SETFIRSTVISIBLELINE, firstLine, IntPtr.Zero);

            ApplyHighlight(stack);
        }

        /// <summary>Highlights the lines that differ from the previous slice.</summary>
        private void ApplyHighlight(TiffStack stack)
        {
            IntPtr sci = _npp.CurrentScintilla;
            if (_marker < 0) _marker = _npp.AllocateMarker(FallbackMarker);
            var marker = new IntPtr(_marker);
            Win32.SendMessage(sci, Sci.SCI_MARKERDELETEALL, marker, IntPtr.Zero);
            if (!HighlightChanges) return;

            int color = _npp.IsDarkMode ? Bgr(84, 66, 18) : Bgr(255, 236, 150);
            Win32.SendMessage(sci, Sci.SCI_MARKERDEFINE, marker, new IntPtr(Sci.SC_MARK_BACKGROUND));
            Win32.SendMessage(sci, Sci.SCI_MARKERSETBACK, marker, new IntPtr(color));

            bool[] changed = stack.ChangedLinesComparedToPrevious(stack.Index);
            for (int i = 0; i < changed.Length; i++)
                if (changed[i])
                    Win32.SendMessage(sci, Sci.SCI_MARKERADD, new IntPtr(i + TiffStack.HeaderLineCount), marker);
        }

        private static int Bgr(int r, int g, int b) => r | g << 8 | b << 16;

        private sealed class NppWindow : IWin32Window
        {
            public NppWindow(IntPtr handle) => Handle = handle;
            public IntPtr Handle { get; }
        }
    }
}
