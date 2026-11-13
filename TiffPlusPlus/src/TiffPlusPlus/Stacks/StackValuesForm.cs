using System;
using System.Collections.Generic;
using System.Drawing;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using System.Windows.Forms;
using System.Windows.Forms.DataVisualization.Charting;
using TiffPlusPlus.Core;

namespace TiffPlusPlus.Stacks
{
    /// <summary>
    /// Values of selected XML elements across all slices: one chart per element plus a table.
    /// Clicking a point or a row jumps to that slice.
    /// </summary>
    internal sealed class StackValuesForm : Form
    {
        private const int FixedColumns = 2;   // Slice, File

        private readonly TiffStack _stack;
        private readonly Action<int> _goToSlice;
        private readonly Chart _chart = new Chart { Dock = DockStyle.Fill, BackColor = Color.White };
        private readonly DataGridView _grid = new DataGridView
        {
            Dock = DockStyle.Fill,
            ReadOnly = true,
            AllowUserToAddRows = false,
            AllowUserToDeleteRows = false,
            AllowUserToResizeRows = false,
            RowHeadersVisible = false,
            SelectionMode = DataGridViewSelectionMode.FullRowSelect,
            MultiSelect = false,
            BackgroundColor = SystemColors.Window,
            AutoSizeColumnsMode = DataGridViewAutoSizeColumnsMode.DisplayedCells,
        };
        private readonly ToolStripStatusLabel _status = new ToolStripStatusLabel { Spring = true, TextAlign = ContentAlignment.MiddleLeft };
        private readonly List<XmlValuePath> _paths = new List<XmlValuePath>();
        private readonly List<string[]> _values = new List<string[]>();
        private bool _syncing;

        public StackValuesForm(TiffStack stack, Action<int> goToSlice)
        {
            _stack = stack;
            _goToSlice = goToSlice;
            Text = $"{Plugin.PluginName} values - {stack.Name}";
            Size = new Size(900, 650);
            StartPosition = FormStartPosition.CenterParent;
            ShowInTaskbar = false;

            var toolbar = new ToolStrip { GripStyle = ToolStripGripStyle.Hidden };
            toolbar.Items.Add(new ToolStripButton("Export CSV...", null, (s, e) => ExportCsv()));
            toolbar.Items.Add(new ToolStripButton("Clear", null, (s, e) => ClearSeries()));
            toolbar.Items.Add(new ToolStripSeparator());
            toolbar.Items.Add(new ToolStripLabel("Add more: caret on a value line, then Plugins > Tiff++ > Values across stack"));

            var split = new SplitContainer { Dock = DockStyle.Fill, Orientation = Orientation.Horizontal };
            // SplitterDistance must be set once the control has its real size.
            Load += (s, e) => split.SplitterDistance = (int)(split.Height * 0.6);
            split.Panel1.Controls.Add(_chart);
            split.Panel2.Controls.Add(_grid);

            var statusBar = new StatusStrip();
            statusBar.Items.Add(_status);

            Controls.Add(split);
            Controls.Add(toolbar);
            Controls.Add(statusBar);

            _grid.Columns.Add("slice", "Slice");
            _grid.Columns.Add("file", "File");
            for (int i = 0; i < stack.Count; i++)
                _grid.Rows.Add(i + 1, Path.GetFileName(stack.Files[i]));
            _grid.SelectionChanged += OnGridSelectionChanged;
            _chart.MouseClick += OnChartClick;
        }

        public async void AddSeries(XmlValuePath path)
        {
            if (_paths.Any(p => p.FullName == path.FullName))
            {
                _status.Text = $"{path.FullName} is already shown.";
                return;
            }

            try
            {
                _status.Text = $"Reading {path.FullName} from {_stack.Count} slices...";
                UseWaitCursor = true;
                string[] values = await Task.Run(() =>
                    Enumerable.Range(0, _stack.Count).Select(i => path.Evaluate(_stack.GetSliceText(i))).ToArray());
                if (IsDisposed) return;

                _paths.Add(path);
                _values.Add(values);
                AddColumn(path, values);
                int numeric = AddChart(path, values);

                int distinct = values.Where(v => v != null).Distinct().Count();
                int missing = values.Count(v => v == null);
                _status.Text = $"{path.FullName}: {distinct} distinct value(s)" +
                               (numeric == 0 ? ", non-numeric (see table)" : "") +
                               (missing > 0 ? $", missing in {missing} slice(s)" : "");
                SetCurrentSlice(_stack.Index);
            }
            catch (Exception ex)
            {
                _status.Text = "Error: " + ex.Message;
            }
            finally
            {
                UseWaitCursor = false;
            }
        }

        public void SetCurrentSlice(int index)
        {
            foreach (var area in _chart.ChartAreas)
            {
                area.AxisX.StripLines.Clear();
                area.AxisX.StripLines.Add(new StripLine { IntervalOffset = index + 1, BorderColor = Color.OrangeRed, BorderWidth = 2 });
            }

            if (index < 0 || index >= _grid.Rows.Count) return;
            _syncing = true;
            try
            {
                _grid.ClearSelection();
                _grid.Rows[index].Selected = true;
                _grid.CurrentCell = _grid.Rows[index].Cells[0];
            }
            finally
            {
                _syncing = false;
            }
        }

        private void AddColumn(XmlValuePath path, string[] values)
        {
            int col = _grid.Columns.Add("v" + _paths.Count, path.ShortName);
            _grid.Columns[col].ToolTipText = path.FullName;
            for (int i = 0; i < values.Length; i++)
                _grid.Rows[i].Cells[col].Value = values[i] ?? "";
        }

        /// <summary>Adds one chart area per element (scales differ, e.g. kV vs. mm); returns the numeric point count.</summary>
        private int AddChart(XmlValuePath path, string[] values)
        {
            var area = new ChartArea(path.FullName);
            area.AxisX.Minimum = 1;
            area.AxisX.Maximum = Math.Max(2, _stack.Count);
            area.AxisX.MajorGrid.LineColor = Color.Gainsboro;
            area.AxisY.MajorGrid.LineColor = Color.Gainsboro;
            area.AxisY.IsStartedFromZero = false;
            area.AxisY.LabelStyle.Format = "G6";
            area.CursorX.IsUserEnabled = false;
            if (_chart.ChartAreas.Count > 0) area.AlignWithChartArea = _chart.ChartAreas[0].Name;
            _chart.ChartAreas.Add(area);

            _chart.Titles.Add(new Title(path.ShortName, Docking.Top, new Font("Segoe UI", 9f, FontStyle.Bold), Color.DimGray)
            {
                DockedToChartArea = area.Name,
                IsDockedInsideChartArea = false,
                ToolTip = path.FullName,
            });

            var series = new Series(path.FullName)
            {
                ChartArea = area.Name,
                ChartType = SeriesChartType.Line,
                MarkerStyle = MarkerStyle.Circle,
                MarkerSize = _stack.Count > 300 ? 0 : 5,
                Color = Color.FromArgb(55, 120, 220),
                ToolTip = "Slice #VALX: #VALY{G6}",
            };
            int numeric = 0;
            for (int i = 0; i < values.Length; i++)
            {
                if (values[i] != null && double.TryParse(values[i], NumberStyles.Float, CultureInfo.InvariantCulture, out double v))
                {
                    series.Points.AddXY(i + 1, v);
                    numeric++;
                }
            }
            _chart.Series.Add(series);
            return numeric;
        }

        private void OnChartClick(object sender, MouseEventArgs e)
        {
            HitTestResult hit = _chart.HitTest(e.X, e.Y);
            if (hit.ChartElementType == ChartElementType.DataPoint && hit.Object is DataPoint point)
                _goToSlice((int)point.XValue - 1);
            else if (hit.ChartArea != null && hit.ChartElementType == ChartElementType.PlottingArea)
            {
                double x = hit.ChartArea.AxisX.PixelPositionToValue(e.X);
                _goToSlice((int)Math.Round(x) - 1);
            }
        }

        private void OnGridSelectionChanged(object sender, EventArgs e)
        {
            if (_syncing || _grid.CurrentRow == null) return;
            _goToSlice(_grid.CurrentRow.Index);
        }

        private void ClearSeries()
        {
            _paths.Clear();
            _values.Clear();
            _chart.Series.Clear();
            _chart.ChartAreas.Clear();
            _chart.Titles.Clear();
            while (_grid.Columns.Count > FixedColumns) _grid.Columns.RemoveAt(_grid.Columns.Count - 1);
            _status.Text = "";
        }

        /// <summary>Semicolon separated (Excel default for German locales), UTF-8 with BOM, values as stored in the XML.</summary>
        private void ExportCsv()
        {
            if (_paths.Count == 0) return;
            using (var dlg = new SaveFileDialog
            {
                Filter = "CSV (*.csv)|*.csv",
                FileName = string.Concat(_stack.Name.Split(Path.GetInvalidFileNameChars())) + " values.csv",
            })
            {
                if (dlg.ShowDialog(this) != DialogResult.OK) return;
                var sb = new StringBuilder();
                sb.AppendLine(string.Join(";", new[] { "Slice", "File" }.Concat(_paths.Select(p => p.FullName)).Select(Csv)));
                for (int i = 0; i < _stack.Count; i++)
                {
                    var cells = new List<string> { (i + 1).ToString(CultureInfo.InvariantCulture), _stack.Files[i] };
                    cells.AddRange(_values.Select(v => v[i] ?? ""));
                    sb.AppendLine(string.Join(";", cells.Select(Csv)));
                }
                File.WriteAllText(dlg.FileName, sb.ToString(), new UTF8Encoding(true));
                _status.Text = "Exported to " + dlg.FileName;
            }
        }

        private static string Csv(string value) =>
            value.IndexOfAny(new[] { ';', '"', '\n', '\r' }) >= 0 ? "\"" + value.Replace("\"", "\"\"") + "\"" : value;
    }
}
