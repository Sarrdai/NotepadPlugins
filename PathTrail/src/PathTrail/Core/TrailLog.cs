using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Text;

namespace PathTrail.Core
{
    /// <summary>
    /// Append-only, tab separated event log (time, kind, seconds, path). Windows paths never contain tabs or
    /// line breaks, so no escaping is needed. Events older than the retention are dropped on load.
    /// </summary>
    public sealed class TrailLog
    {
        private static readonly Encoding Utf8 = new UTF8Encoding(false);
        private readonly string _file;
        private readonly List<TrailEvent> _events = new List<TrailEvent>();

        public TrailLog(string file) => _file = file;

        public IReadOnlyList<TrailEvent> Events => _events;

        /// <summary>Loads the log, drops expired and unreadable lines and rewrites the file if anything was dropped.</summary>
        public void Load(DateTime nowUtc, int retentionDays)
        {
            _events.Clear();
            if (!File.Exists(_file)) return;

            DateTime cutoff = nowUtc.AddDays(-Math.Max(1, retentionDays));
            int lines = 0;
            foreach (string line in File.ReadLines(_file, Utf8))
            {
                lines++;
                TrailEvent e = Parse(line);
                if (e != null && e.TimeUtc >= cutoff) _events.Add(e);
            }
            if (_events.Count != lines) Rewrite();
        }

        public void Append(TrailEvent e)
        {
            _events.Add(e);
            Directory.CreateDirectory(Path.GetDirectoryName(_file));
            File.AppendAllText(_file, Format(e) + "\n", Utf8);
        }

        public void Clear()
        {
            _events.Clear();
            if (File.Exists(_file)) File.Delete(_file);
        }

        private void Rewrite()
        {
            string temp = _file + ".tmp";
            File.WriteAllText(temp, string.Concat(_events.Select(e => Format(e) + "\n")), Utf8);
            File.Copy(temp, _file, true);
            File.Delete(temp);
        }

        public static string Format(TrailEvent e) => string.Join("\t",
            e.TimeUtc.ToString("yyyy-MM-ddTHH:mm:ssZ", CultureInfo.InvariantCulture),
            e.Kind.ToString().ToLowerInvariant(),
            Math.Round(e.Seconds, 1).ToString(CultureInfo.InvariantCulture),
            e.Path);

        public static TrailEvent Parse(string line)
        {
            string[] parts = line.Split(new[] { '\t' }, 4);
            if (parts.Length != 4 || parts[3].Length == 0) return null;
            if (!DateTime.TryParseExact(parts[0], "yyyy-MM-ddTHH:mm:ssZ", CultureInfo.InvariantCulture,
                    DateTimeStyles.AdjustToUniversal | DateTimeStyles.AssumeUniversal, out DateTime time)) return null;
            if (!Enum.TryParse(parts[1], true, out TrailEventKind kind)) return null;
            if (!double.TryParse(parts[2], NumberStyles.Float, CultureInfo.InvariantCulture, out double seconds)) return null;
            return new TrailEvent(time, kind, parts[3], seconds);
        }
    }
}
