using System;

namespace PathTrail.Core
{
    public enum TrailEventKind
    {
        /// <summary>File opened in Notepad++.</summary>
        Open,
        /// <summary>Tab was active for <see cref="TrailEvent.Seconds"/> seconds (written when the activation ends).</summary>
        Active,
        /// <summary>File saved (also covers "Save As" to a new location).</summary>
        Save,
        /// <summary>File closed.</summary>
        Close,
        /// <summary>Seeded once from the Notepad++ recent file list and session.</summary>
        Import,
    }

    /// <summary>One line of the trail log.</summary>
    public sealed class TrailEvent
    {
        public TrailEvent(DateTime timeUtc, TrailEventKind kind, string path, double seconds = 0)
        {
            TimeUtc = timeUtc;
            Kind = kind;
            Path = path;
            Seconds = seconds;
        }

        public DateTime TimeUtc { get; }
        public TrailEventKind Kind { get; }
        public string Path { get; }
        public double Seconds { get; }
    }
}
