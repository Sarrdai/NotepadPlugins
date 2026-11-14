using System;
using System.Collections.Generic;

namespace PathTrail.Core
{
    /// <summary>One clickable part of a breadcrumb: its label and the full path up to and including it.</summary>
    public sealed class PathSegment
    {
        public PathSegment(string label, string fullPath)
        {
            Label = label;
            FullPath = fullPath;
        }

        public string Label { get; }
        public string FullPath { get; }
    }

    /// <summary>String based Windows path helpers (independent of the OS the tests run on).</summary>
    public static class TrailPath
    {
        public static readonly StringComparer Comparer = StringComparer.OrdinalIgnoreCase;

        /// <summary>Splits "C:\a\b.txt" into C: | a | b.txt and "\\server\share\a" into \\server\share | a.</summary>
        public static IReadOnlyList<PathSegment> Split(string path)
        {
            var result = new List<PathSegment>();
            if (string.IsNullOrEmpty(path)) return result;

            string normalized = path.Replace('/', '\\');
            int start;
            string root;
            if (normalized.StartsWith(@"\\", StringComparison.Ordinal))
            {
                // UNC: the share is the root, \\server alone cannot be opened.
                int server = normalized.IndexOf('\\', 2);
                int share = server < 0 ? -1 : normalized.IndexOf('\\', server + 1);
                root = share < 0 ? normalized.TrimEnd('\\') : normalized.Substring(0, share);
                start = share < 0 ? normalized.Length : share + 1;
                result.Add(new PathSegment(root, root));
            }
            else
            {
                int first = normalized.IndexOf('\\');
                root = first < 0 ? normalized : normalized.Substring(0, first);
                start = first < 0 ? normalized.Length : first + 1;
                // A drive root is only a valid folder with its backslash ("C:" is the current dir of C).
                result.Add(new PathSegment(root, root.EndsWith(":", StringComparison.Ordinal) ? root + "\\" : root));
            }

            string current = root;
            foreach (string part in normalized.Substring(start).Split(new[] { '\\' }, StringSplitOptions.RemoveEmptyEntries))
            {
                current += "\\" + part;
                result.Add(new PathSegment(part, current));
            }
            return result;
        }

        /// <summary>Parent folder or null for a root.</summary>
        public static string Parent(string path)
        {
            var segments = Split(path);
            return segments.Count < 2 ? null : segments[segments.Count - 2].FullPath;
        }

        /// <summary>All folders containing the file, from the root down to its directory.</summary>
        public static IEnumerable<string> Ancestors(string filePath)
        {
            var segments = Split(filePath);
            for (int i = 0; i < segments.Count - 1; i++)
                yield return segments[i].FullPath;
        }

        public static string Name(string path)
        {
            var segments = Split(path);
            return segments.Count == 0 ? path : segments[segments.Count - 1].Label;
        }

        /// <summary>Tabs without a file on disk ("new 1") have no folder.</summary>
        public static bool IsFilePath(string path) =>
            !string.IsNullOrEmpty(path) && (path.StartsWith(@"\\", StringComparison.Ordinal) ||
                                            (path.Length > 2 && path[1] == ':' && (path[2] == '\\' || path[2] == '/')));
    }
}
