using System;

namespace PathTrail.Core
{
    /// <summary>
    /// Turns Notepad++ notifications into trail events. The active tab is timed: its "active" event is written
    /// when another tab is activated, the file is closed or Notepad++ shuts down.
    /// </summary>
    public sealed class Tracker
    {
        // Shorter activations are tab switching on the way to another tab, not visits.
        private const double MinActiveSeconds = 2;

        private readonly TrailLog _log;
        private readonly Func<DateTime> _clock;
        private string _activePath;
        private DateTime _activeSinceUtc;

        public Tracker(TrailLog log, Func<DateTime> clock = null)
        {
            _log = log;
            _clock = clock ?? (() => DateTime.UtcNow);
        }

        /// <summary>Raised after an event was written.</summary>
        public event Action Changed;

        public void Opened(string path) => Write(TrailEventKind.Open, path);

        public void Saved(string path)
        {
            // "Save As" moves the active tab to a new path.
            if (path != null && _activePath != null && !TrailPath.Comparer.Equals(path, _activePath))
                Activated(path);
            Write(TrailEventKind.Save, path);
        }

        public void Closing(string path)
        {
            if (path != null && TrailPath.Comparer.Equals(path, _activePath)) EndActivation();
            Write(TrailEventKind.Close, path);
        }

        public void Activated(string path)
        {
            if (path != null && TrailPath.Comparer.Equals(path, _activePath)) return;
            EndActivation();
            _activePath = TrailPath.IsFilePath(path) ? path : null;
            _activeSinceUtc = _clock();
        }

        /// <summary>Writes the running activation, e.g. at shutdown.</summary>
        public void EndActivation()
        {
            if (_activePath == null) return;
            DateTime now = _clock();
            double seconds = (now - _activeSinceUtc).TotalSeconds;
            string path = _activePath;
            _activePath = null;
            if (seconds >= MinActiveSeconds)
                Append(new TrailEvent(_activeSinceUtc, TrailEventKind.Active, path, seconds));
        }

        private void Write(TrailEventKind kind, string path)
        {
            if (TrailPath.IsFilePath(path)) Append(new TrailEvent(_clock(), kind, path));
        }

        private void Append(TrailEvent e)
        {
            _log.Append(e);
            Changed?.Invoke();
        }
    }
}
