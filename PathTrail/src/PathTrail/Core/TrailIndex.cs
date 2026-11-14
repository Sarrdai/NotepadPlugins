using System;
using System.Collections.Generic;
using System.Linq;

namespace PathTrail.Core
{
    /// <summary>A file as shown in the history or in search results.</summary>
    public sealed class FileEntry
    {
        public string Path { get; set; }
        public DateTime LastUtc { get; set; }
        public int Opens { get; set; }
        public double Seconds { get; set; }
        public double Score { get; set; }
    }

    public sealed class DayGroup
    {
        public DateTime Day { get; set; }
        public List<FileEntry> Files { get; } = new List<FileEntry>();
    }

    public sealed class FolderEntry
    {
        public string Path { get; set; }
        public double Score { get; set; }
        public DateTime LastUtc { get; set; }
        /// <summary>Distinct files touched below the folder.</summary>
        public int Files { get; set; }
        /// <summary>Subfolders with a significant share of the activity.</summary>
        public int ActiveSubfolders { get; set; }
    }

    public sealed class HotspotOptions
    {
        public double HalfLifeHours { get; set; } = 24;
        /// <summary>Minimum share of a folder's score for a child to count as active.</summary>
        public double SignificantShare { get; set; } = 0.1;
        /// <summary>A folder is no project folder if one child (or its direct files) carries this share alone.</summary>
        public double DominantShare { get; set; } = 0.8;
        /// <summary>Path depth (drive root = 1) below which folders never count as project folders.</summary>
        public int MinDepth { get; set; } = 2;
        public int MaxResults { get; set; } = 15;
    }

    public sealed class Hotspots
    {
        /// <summary>Folders whose activity spreads over several active subfolders (e.g. an experiment root).</summary>
        public List<FolderEntry> Projects { get; } = new List<FolderEntry>();
        /// <summary>Folders with the most activity on the files directly inside them.</summary>
        public List<FolderEntry> WorkingFolders { get; } = new List<FolderEntry>();
    }

    /// <summary>Matches a search query against a path; null means no match, higher is better.</summary>
    public interface IPathMatcher
    {
        double? Match(string path, string query);
    }

    /// <summary>Case-insensitive substring search: every whitespace separated term must occur in the path.</summary>
    public sealed class SubstringMatcher : IPathMatcher
    {
        public double? Match(string path, string query)
        {
            string[] terms = query.Split((char[])null, StringSplitOptions.RemoveEmptyEntries);
            if (terms.Length == 0) return null;
            string name = TrailPath.Name(path);
            double score = 0;
            foreach (string term in terms)
            {
                if (path.IndexOf(term, StringComparison.OrdinalIgnoreCase) < 0) return null;
                if (name.IndexOf(term, StringComparison.OrdinalIgnoreCase) >= 0) score += 1;   // prefer file name hits
            }
            return score;
        }
    }

    /// <summary>Read models (history, search, hotspots) computed from the event log.</summary>
    public static class TrailIndex
    {
        /// <summary>Relevance of a single event before time decay.</summary>
        public static double Weight(TrailEvent e)
        {
            switch (e.Kind)
            {
                case TrailEventKind.Open:
                case TrailEventKind.Save:
                    return 1;
                case TrailEventKind.Import:
                    return 0.5;
                case TrailEventKind.Active:
                    return 0.2 + 0.8 * Math.Min(e.Seconds, 600) / 600;   // saturates after 10 minutes
                default:
                    return 0;
            }
        }

        /// <summary>Weight with exponential time decay ("frecency").</summary>
        public static double Frecency(TrailEvent e, DateTime nowUtc, double halfLifeHours)
        {
            double ageHours = Math.Max(0, (nowUtc - e.TimeUtc).TotalHours);
            return Weight(e) * Math.Pow(0.5, ageHours / Math.Max(0.1, halfLifeHours));
        }

        /// <summary>Files grouped by local day, newest day and newest file first. A file appears once per day.</summary>
        public static List<DayGroup> History(IEnumerable<TrailEvent> events, Func<DateTime, DateTime> toLocal = null)
        {
            toLocal = toLocal ?? (t => t.ToLocalTime());
            return events
                .Where(e => TrailPath.IsFilePath(e.Path))
                .GroupBy(e => toLocal(e.TimeUtc).Date)
                .OrderByDescending(g => g.Key)
                .Select(day =>
                {
                    var group = new DayGroup { Day = day.Key };
                    group.Files.AddRange(Summarize(day, DateTime.MinValue, 1).OrderByDescending(f => f.LastUtc));
                    return group;
                })
                .ToList();
        }

        /// <summary>Distinct files matching the query, best match first, then by frecency.</summary>
        public static List<FileEntry> Search(IEnumerable<TrailEvent> events, string query, IPathMatcher matcher,
            DateTime nowUtc, double halfLifeHours, int maxResults = 200)
        {
            return Summarize(events.Where(e => TrailPath.IsFilePath(e.Path)), nowUtc, halfLifeHours)
                .Select(f => (file: f, match: matcher.Match(f.Path, query)))
                .Where(x => x.match.HasValue)
                .OrderByDescending(x => x.match.Value)
                .ThenByDescending(x => x.file.Score)
                .ThenByDescending(x => x.file.LastUtc)
                .Take(maxResults)
                .Select(x => x.file)
                .ToList();
        }

        public static Hotspots FindHotspots(IEnumerable<TrailEvent> events, DateTime nowUtc, HotspotOptions options)
        {
            var root = new FolderNode(null, 0);
            var nodes = new Dictionary<string, FolderNode>(TrailPath.Comparer);

            foreach (TrailEvent e in events)
            {
                if (!TrailPath.IsFilePath(e.Path)) continue;
                double w = Frecency(e, nowUtc, options.HalfLifeHours);
                FolderNode parent = root;
                var segments = TrailPath.Split(e.Path);
                for (int i = 0; i < segments.Count - 1; i++)
                {
                    string folder = segments[i].FullPath;
                    if (!nodes.TryGetValue(folder, out FolderNode node))
                    {
                        node = new FolderNode(folder, i + 1);
                        nodes.Add(folder, node);
                        parent.Children.Add(node);
                    }
                    node.Score += w;
                    node.Files.Add(e.Path);
                    if (e.TimeUtc > node.LastUtc) node.LastUtc = e.TimeUtc;
                    if (i == segments.Count - 2) node.DirectScore += w;
                    parent = node;
                }
            }

            var projects = nodes.Values.Where(n => IsProject(n, options)).ToList();
            var projectSet = new HashSet<FolderNode>(projects);
            var result = new Hotspots();

            // A project whose activity is (almost) fully explained by sub-projects is just their common parent.
            result.Projects.AddRange(projects
                .Where(p => TopmostDescendants(p, projectSet).Sum(d => d.Score) < options.DominantShare * p.Score)
                .OrderByDescending(p => p.Score)
                .Take(options.MaxResults)
                .Select(p => ToEntry(p, options)));

            result.WorkingFolders.AddRange(nodes.Values
                .Where(n => n.DirectScore > 0)
                .OrderByDescending(n => n.DirectScore)
                .Take(options.MaxResults)
                .Select(n => ToEntry(n, options, n.DirectScore)));
            return result;
        }

        private static bool IsProject(FolderNode node, HotspotOptions options)
        {
            if (node.Depth < options.MinDepth || node.Score <= 0) return false;
            if (node.DirectScore >= options.DominantShare * node.Score) return false;
            if (node.Children.Any(c => c.Score >= options.DominantShare * node.Score)) return false;
            return ActiveSubfolders(node, options) >= 2;
        }

        private static int ActiveSubfolders(FolderNode node, HotspotOptions options) =>
            node.Children.Count(c => c.Score >= options.SignificantShare * node.Score);

        private static IEnumerable<FolderNode> TopmostDescendants(FolderNode node, HashSet<FolderNode> set)
        {
            foreach (FolderNode child in node.Children)
            {
                if (set.Contains(child)) yield return child;
                else foreach (FolderNode d in TopmostDescendants(child, set)) yield return d;
            }
        }

        private static FolderEntry ToEntry(FolderNode n, HotspotOptions options, double? score = null) => new FolderEntry
        {
            Path = n.Path,
            Score = score ?? n.Score,
            LastUtc = n.LastUtc,
            Files = n.Files.Count,
            ActiveSubfolders = ActiveSubfolders(n, options),
        };

        private static IEnumerable<FileEntry> Summarize(IEnumerable<TrailEvent> events, DateTime nowUtc, double halfLifeHours) =>
            events.GroupBy(e => e.Path, TrailPath.Comparer).Select(g =>
            {
                TrailEvent last = g.OrderBy(e => e.TimeUtc).Last();
                return new FileEntry
                {
                    Path = last.Path,   // latest spelling of the path
                    LastUtc = last.TimeUtc,
                    Opens = g.Count(e => e.Kind == TrailEventKind.Open),
                    Seconds = g.Where(e => e.Kind == TrailEventKind.Active).Sum(e => e.Seconds),
                    Score = nowUtc == DateTime.MinValue ? 0 : g.Sum(e => Frecency(e, nowUtc, halfLifeHours)),
                };
            });

        private sealed class FolderNode
        {
            public FolderNode(string path, int depth)
            {
                Path = path;
                Depth = depth;
            }

            public string Path { get; }
            public int Depth { get; }
            public double Score { get; set; }
            public double DirectScore { get; set; }
            public DateTime LastUtc { get; set; }
            public HashSet<string> Files { get; } = new HashSet<string>(TrailPath.Comparer);
            public List<FolderNode> Children { get; } = new List<FolderNode>();
        }
    }
}
