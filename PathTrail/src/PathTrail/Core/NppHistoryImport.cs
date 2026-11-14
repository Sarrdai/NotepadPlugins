using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Xml;
using System.Xml.Linq;

namespace PathTrail.Core
{
    /// <summary>Seeds an empty trail from the Notepad++ recent file list (config.xml) and session (session.xml).</summary>
    public static class NppHistoryImport
    {
        /// <summary>Recent files first (newest first), then session files not already listed.</summary>
        public static List<string> ReadPaths(string configXml, string sessionXml)
        {
            var paths = new List<string>();
            var seen = new HashSet<string>(TrailPath.Comparer);
            void Add(string path)
            {
                if (TrailPath.IsFilePath(path) && seen.Add(path)) paths.Add(path);
            }

            foreach (string path in FileNames(configXml, "History")) Add(path);
            foreach (string path in FileNames(sessionXml, "Session")) Add(path);
            return paths;
        }

        /// <summary>Import events with descending times, so the order of the recent file list is kept.</summary>
        public static List<TrailEvent> ToEvents(IEnumerable<string> paths, DateTime nowUtc) =>
            paths.Select((p, i) => new TrailEvent(nowUtc.AddMinutes(-i), TrailEventKind.Import, p)).ToList();

        private static IEnumerable<string> FileNames(string xmlFile, string container)
        {
            if (string.IsNullOrEmpty(xmlFile) || !File.Exists(xmlFile)) return Enumerable.Empty<string>();
            try
            {
                XDocument doc = XDocument.Load(xmlFile);
                return doc.Descendants(container)
                    .SelectMany(c => c.Descendants("File"))
                    .Select(f => (string)f.Attribute("filename"))
                    .Where(f => f != null)
                    .ToList();
            }
            catch (Exception ex) when (ex is XmlException || ex is IOException || ex is UnauthorizedAccessException)
            {
                return Enumerable.Empty<string>();   // locked or broken files are simply skipped
            }
        }
    }
}
