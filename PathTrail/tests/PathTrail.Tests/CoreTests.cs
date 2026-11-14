using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using PathTrail.Core;
using Xunit;

namespace PathTrail.Tests
{
    public class TrailPathTests
    {
        [Fact]
        public void SplitsDrivePathIntoClickableSegments()
        {
            var segments = TrailPath.Split(@"D:\Data\Exp\run1\result.csv");
            Assert.Equal(new[] { "D:", "Data", "Exp", "run1", "result.csv" }, segments.Select(s => s.Label));
            Assert.Equal(@"D:\", segments[0].FullPath);
            Assert.Equal(@"D:\Data\Exp", segments[2].FullPath);
            Assert.Equal(@"D:\Data\Exp\run1\result.csv", segments[4].FullPath);
        }

        [Fact]
        public void SplitsUncPathWithShareAsRoot()
        {
            var segments = TrailPath.Split(@"\\server\share\a\b.txt");
            Assert.Equal(new[] { @"\\server\share", "a", "b.txt" }, segments.Select(s => s.Label));
            Assert.Equal(@"\\server\share\a", segments[1].FullPath);
        }

        [Fact]
        public void AncestorsAndParent()
        {
            Assert.Equal(new[] { @"C:\", @"C:\a", @"C:\a\b" }, TrailPath.Ancestors(@"C:\a\b\c.txt"));
            Assert.Equal(@"C:\a\b", TrailPath.Parent(@"C:\a\b\c.txt"));
            Assert.Null(TrailPath.Parent(@"C:\"));
        }

        [Theory]
        [InlineData(@"C:\x.txt", true)]
        [InlineData(@"\\srv\share\x.txt", true)]
        [InlineData("new 1", false)]
        [InlineData("", false)]
        public void DetectsRealFilePaths(string path, bool expected) => Assert.Equal(expected, TrailPath.IsFilePath(path));
    }

    public class TrailLogTests : IDisposable
    {
        private readonly string _dir = Path.Combine(Path.GetTempPath(), "PathTrail_" + Guid.NewGuid().ToString("N"));

        public TrailLogTests() => Directory.CreateDirectory(_dir);
        public void Dispose() => Directory.Delete(_dir, true);

        [Fact]
        public void FormatAndParseRoundTrip()
        {
            var e = new TrailEvent(new DateTime(2026, 11, 14, 9, 30, 5, DateTimeKind.Utc), TrailEventKind.Active, @"C:\a b\ä.txt", 12.34);
            TrailEvent parsed = TrailLog.Parse(TrailLog.Format(e));
            Assert.Equal(e.TimeUtc, parsed.TimeUtc);
            Assert.Equal(DateTimeKind.Utc, parsed.TimeUtc.Kind);
            Assert.Equal(TrailEventKind.Active, parsed.Kind);
            Assert.Equal(12.3, parsed.Seconds);
            Assert.Equal(e.Path, parsed.Path);
        }

        [Fact]
        public void LoadDropsExpiredAndBrokenLinesAndRewritesFile()
        {
            string file = Path.Combine(_dir, "trail.log");
            var now = new DateTime(2026, 11, 14, 12, 0, 0, DateTimeKind.Utc);
            var log = new TrailLog(file);
            log.Append(new TrailEvent(now.AddDays(-40), TrailEventKind.Open, @"C:\old.txt"));
            log.Append(new TrailEvent(now.AddDays(-1), TrailEventKind.Open, @"C:\new.txt"));
            File.AppendAllText(file, "garbage\n");

            var reloaded = new TrailLog(file);
            reloaded.Load(now, 30);

            Assert.Equal(new[] { @"C:\new.txt" }, reloaded.Events.Select(e => e.Path));
            Assert.Single(File.ReadAllLines(file));
        }
    }

    public class TrailIndexTests
    {
        private static readonly DateTime Now = new DateTime(2026, 11, 14, 12, 0, 0, DateTimeKind.Utc);

        private static TrailEvent Open(string path, double hoursAgo = 1) =>
            new TrailEvent(Now.AddHours(-hoursAgo), TrailEventKind.Open, path);

        [Fact]
        public void HistoryGroupsByDayAndListsEachFileOncePerDay()
        {
            var events = new[]
            {
                Open(@"C:\a.txt", 50), Open(@"C:\a.txt", 2), Open(@"C:\b.txt", 1), Open(@"c:\A.TXT", 0.5),
                new TrailEvent(Now, TrailEventKind.Open, "new 1"),
            };
            List<DayGroup> days = TrailIndex.History(events, t => t);

            Assert.Equal(new[] { Now.Date, Now.Date.AddDays(-2) }, days.Select(d => d.Day));   // 50 h before 14.11. 12:00 = 12.11.
            Assert.Equal(new[] { @"c:\A.TXT", @"C:\b.txt" }, days[0].Files.Select(f => f.Path));
            Assert.Equal(2, days[0].Files[0].Opens);
        }

        [Fact]
        public void SearchRequiresAllTermsAndPrefersFileNameHits()
        {
            var events = new[] { Open(@"D:\results\x\data.csv"), Open(@"D:\other\results.csv"), Open(@"D:\misc\notes.txt") };
            var hits = TrailIndex.Search(events, "RESULTS csv", new SubstringMatcher(), Now, 24);
            Assert.Equal(new[] { @"D:\other\results.csv", @"D:\results\x\data.csv" }, hits.Select(h => h.Path));
        }

        [Fact]
        public void ExperimentFolderWithSeveralActiveRunsIsAProject()
        {
            var events = new List<TrailEvent>();
            foreach (string run in new[] { "run01", "run02", "run03" })
                for (int i = 0; i < 3; i++)
                    events.Add(Open($@"D:\Data\Exp_2026_11\{run}\raw\file{i}.csv"));
            events.Add(Open(@"C:\Users\me\notes.txt"));

            Hotspots spots = TrailIndex.FindHotspots(events, Now, new HotspotOptions());

            Assert.Equal(@"D:\Data\Exp_2026_11", spots.Projects.First().Path);
            Assert.Equal(3, spots.Projects.First().ActiveSubfolders);
            Assert.Equal(9, spots.Projects.First().Files);
            Assert.DoesNotContain(spots.Projects, p => p.Path == @"D:\Data" || p.Path == @"D:\");
            Assert.Contains(spots.WorkingFolders, w => w.Path == @"D:\Data\Exp_2026_11\run02\raw");
        }

        [Fact]
        public void CommonParentOfTwoProjectsIsSuppressed()
        {
            var events = new List<TrailEvent>();
            foreach (string exp in new[] { "ExpA", "ExpB" })
                foreach (string run in new[] { "r1", "r2" })
                    events.Add(Open($@"D:\Data\{exp}\{run}\x.csv"));

            var projects = TrailIndex.FindHotspots(events, Now, new HotspotOptions()).Projects.Select(p => p.Path).ToList();

            Assert.Equal(new[] { @"D:\Data\ExpA", @"D:\Data\ExpB" }, projects.OrderBy(p => p));
        }

        [Fact]
        public void RecentActivityOutweighsOldActivity()
        {
            var events = new List<TrailEvent>();
            foreach (string run in new[] { "a", "b" })
            {
                events.Add(Open($@"D:\Old\{run}\x.csv", 24 * 7));
                events.Add(Open($@"D:\Old\{run}\y.csv", 24 * 7));
                events.Add(Open($@"D:\New\{run}\x.csv", 1));
            }

            Assert.Equal(@"D:\New", TrailIndex.FindHotspots(events, Now, new HotspotOptions()).Projects.First().Path);
        }

        [Fact]
        public void LongActivationsWeighMoreThanShortOnes()
        {
            double shortGlance = TrailIndex.Weight(new TrailEvent(Now, TrailEventKind.Active, @"C:\a", 5));
            double longWork = TrailIndex.Weight(new TrailEvent(Now, TrailEventKind.Active, @"C:\a", 1200));
            Assert.True(shortGlance < 0.3);
            Assert.Equal(1.0, longWork, 6);
            Assert.Equal(0, TrailIndex.Weight(new TrailEvent(Now, TrailEventKind.Close, @"C:\a")));
        }
    }

    public class NppHistoryImportTests : IDisposable
    {
        private readonly string _dir = Path.Combine(Path.GetTempPath(), "PathTrailImport_" + Guid.NewGuid().ToString("N"));

        public NppHistoryImportTests() => Directory.CreateDirectory(_dir);
        public void Dispose() => Directory.Delete(_dir, true);

        [Fact]
        public void ReadsRecentFilesThenSessionWithoutDuplicates()
        {
            string config = Path.Combine(_dir, "config.xml");
            string session = Path.Combine(_dir, "session.xml");
            File.WriteAllText(config, @"<NotepadPlus><History nbMaxFile=""10""><File filename=""C:\a.txt"" /><File filename=""C:\b.txt"" /></History>
                <FindHistory><File filename=""C:\ignored.txt"" /></FindHistory></NotepadPlus>");
            File.WriteAllText(session, @"<NotepadPlus><Session activeView=""0""><mainView><File filename=""c:\B.TXT"" /><File filename=""new 1"" />
                <File filename=""D:\c.csv"" /></mainView></Session></NotepadPlus>");

            var paths = NppHistoryImport.ReadPaths(config, session);
            Assert.Equal(new[] { @"C:\a.txt", @"C:\b.txt", @"D:\c.csv" }, paths);

            var events = NppHistoryImport.ToEvents(paths, new DateTime(2026, 1, 1, 0, 0, 0, DateTimeKind.Utc));
            Assert.True(events[0].TimeUtc > events[1].TimeUtc);
            Assert.All(events, e => Assert.Equal(TrailEventKind.Import, e.Kind));
        }

        [Fact]
        public void MissingOrBrokenFilesYieldNothing()
        {
            string broken = Path.Combine(_dir, "broken.xml");
            File.WriteAllText(broken, "<NotepadPlus><History>");
            Assert.Empty(NppHistoryImport.ReadPaths(broken, Path.Combine(_dir, "missing.xml")));
        }
    }
}

namespace PathTrail.Tests
{
    public class TrackerTests : IDisposable
    {
        private readonly string _dir = Path.Combine(Path.GetTempPath(), "PathTrailTracker_" + Guid.NewGuid().ToString("N"));
        private DateTime _now = new DateTime(2026, 11, 14, 8, 0, 0, DateTimeKind.Utc);

        public TrackerTests() => Directory.CreateDirectory(_dir);
        public void Dispose() => Directory.Delete(_dir, true);

        [Fact]
        public void TimesActivationsAndIgnoresQuickTabSwitchesAndUnsavedTabs()
        {
            var log = new TrailLog(Path.Combine(_dir, "t.log"));
            var tracker = new Tracker(log, () => _now);

            tracker.Opened(@"C:\a.txt");
            tracker.Activated(@"C:\a.txt");
            _now = _now.AddSeconds(90);
            tracker.Activated(@"C:\b.txt");     // passed through quickly
            _now = _now.AddSeconds(1);
            tracker.Activated("new 1");
            _now = _now.AddSeconds(30);
            tracker.Activated(@"C:\a.txt");
            _now = _now.AddSeconds(10);
            tracker.Closing(@"C:\a.txt");

            var active = log.Events.Where(e => e.Kind == TrailEventKind.Active).ToList();
            Assert.Equal(new[] { 90.0, 10.0 }, active.Select(e => e.Seconds));
            Assert.All(active, e => Assert.Equal(@"C:\a.txt", e.Path));
            Assert.Equal(TrailEventKind.Close, log.Events.Last().Kind);
        }

        [Fact]
        public void SaveAsSwitchesTheActivePath()
        {
            var log = new TrailLog(Path.Combine(_dir, "t.log"));
            var tracker = new Tracker(log, () => _now);
            tracker.Activated(@"C:\old.txt");
            _now = _now.AddSeconds(20);
            tracker.Saved(@"D:\new.txt");
            _now = _now.AddSeconds(40);
            tracker.EndActivation();

            Assert.Equal(new[] { "active C:\\old.txt", "save D:\\new.txt", "active D:\\new.txt" },
                log.Events.Select(e => e.Kind.ToString().ToLowerInvariant() + " " + e.Path));
        }
    }
}
