using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text;

namespace TiffPlusPlus.Core
{
    /// <summary>
    /// A virtual stack of TIFF files. Only the XML of requested slices is read (header/IFD/tag) and cached;
    /// pixel data is never touched here.
    /// </summary>
    public sealed class TiffStack
    {
        private readonly string[] _texts;
        private int _index;

        private TiffStack(string name, IReadOnlyList<string> files)
        {
            Name = name;
            Files = files;
            _texts = new string[files.Count];
        }

        public string Name { get; }
        public IReadOnlyList<string> Files { get; }
        public int Count => Files.Count;

        public int Index
        {
            get => _index;
            set => _index = Math.Max(0, Math.Min(Count - 1, value));
        }

        public static TiffStack Create(string name, IEnumerable<string> files, int index = 0)
        {
            var sorted = files
                .Where(TiffMetadataReader.IsTiffPath)
                .Select(Path.GetFullPath)
                .Distinct(StringComparer.OrdinalIgnoreCase)
                .OrderBy(f => Path.GetFileName(f), NaturalStringComparer.Instance)
                .ThenBy(f => f, NaturalStringComparer.Instance)
                .ToList();
            return new TiffStack(name, sorted) { Index = index };
        }

        /// <summary>TIFF files directly inside the folder (no recursion).</summary>
        public static IEnumerable<string> FilesInFolder(string folder) =>
            Directory.EnumerateFiles(folder).Where(TiffMetadataReader.IsTiffPath);

        /// <summary>Stack name for a set of dropped files: their common folder name.</summary>
        public static string NameForFiles(IReadOnlyCollection<string> files)
        {
            var folders = files.Select(f => Path.GetDirectoryName(Path.GetFullPath(f))).Distinct(StringComparer.OrdinalIgnoreCase).ToList();
            string folder = folders.Count == 1 ? Path.GetFileName(folders[0]) : "Mixed folders";
            return $"{folder} ({files.Count} files)";
        }

        /// <summary>The formatted XML of one slice (thread-safe, cached).</summary>
        public string GetSliceText(int index)
        {
            string text = _texts[index];
            if (text != null) return text;
            text = XmlDocumentBuilder.Build(TiffMetadataReader.Read(Files[index]));
            _texts[index] = text;
            return text;
        }

        public const int HeaderLineCount = 1;

        /// <summary>Header comment + XML of the slice, as shown in the Notepad++ tab.</summary>
        public string BuildDisplayText(int index)
        {
            string info = $"Tiff++ stack \"{Name}\": slice {index + 1} / {Count} - {Path.GetFileName(Files[index])}";
            return "<!-- " + info.Replace("--", "- -") + " -->\r\n" + GetSliceText(index);
        }

        /// <summary>0-based body lines (without header) that differ from the previous slice.</summary>
        public bool[] ChangedLinesComparedToPrevious(int index)
        {
            if (index <= 0) return new bool[0];
            return LineDiff.ChangedLines(LineDiff.SplitLines(GetSliceText(index - 1)), LineDiff.SplitLines(GetSliceText(index)));
        }

        // --- persistence of the stack definition next to its view file (session restore) ----------

        public string Serialize()
        {
            var sb = new StringBuilder();
            sb.Append(Name).Append('\n').Append(Index).Append('\n');
            foreach (string f in Files) sb.Append(f).Append('\n');
            return sb.ToString();
        }

        public static TiffStack Deserialize(string text)
        {
            string[] lines = text.Replace("\r\n", "\n").Split(new[] { '\n' }, StringSplitOptions.RemoveEmptyEntries);
            if (lines.Length < 3 || !int.TryParse(lines[1], out int index)) return null;
            return Create(lines[0], lines.Skip(2), index);
        }
    }
}
