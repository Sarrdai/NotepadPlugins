using System;
using System.Globalization;
using System.IO;
using System.Runtime.InteropServices;
using System.Text;

namespace PathTrail
{
    /// <summary>What a click on a folder segment does.</summary>
    internal enum FolderAction
    {
        Explorer,
        Terminal,
        Workspace,
    }

    /// <summary>Plugin settings in PathTrail.ini inside the Notepad++ plugin config folder (like the other plugins).</summary>
    internal sealed class Settings
    {
        private const string Section = "Settings";
        private readonly string _path;

        public Settings(string configDirectory)
        {
            _path = Path.Combine(configDirectory, "PathTrail.ini");
            FolderClick = Enum.TryParse(ReadString("FolderClick", null), true, out FolderAction action) ? action : FolderAction.Explorer;
            RetentionDays = Math.Max(1, ReadInt("RetentionDays", 30));
            HalfLifeHours = Math.Max(1, ReadInt("HalfLifeHours", 24));
            Imported = ReadInt("Imported", 0) != 0;
        }

        public string ConfigDirectory => Path.GetDirectoryName(_path);

        /// <summary>Folder segment click: Explorer, Terminal (Windows Terminal, cmd as fallback) or Workspace.</summary>
        public FolderAction FolderClick { get; set; }

        /// <summary>Events older than this are removed from the log at startup.</summary>
        public int RetentionDays { get; set; }

        /// <summary>Half-life of the frecency weight used for hotspots and search ranking.</summary>
        public int HalfLifeHours { get; set; }

        /// <summary>The Notepad++ recent file list was imported once.</summary>
        public bool Imported { get; set; }

        public void Save()
        {
            Directory.CreateDirectory(ConfigDirectory);
            Write("FolderClick", FolderClick.ToString());
            Write("RetentionDays", RetentionDays.ToString(CultureInfo.InvariantCulture));
            Write("HalfLifeHours", HalfLifeHours.ToString(CultureInfo.InvariantCulture));
            Write("Imported", Imported ? "1" : "0");
        }

        private void Write(string key, string value) => WritePrivateProfileString(Section, key, value, _path);

        private int ReadInt(string key, int fallback) =>
            int.TryParse(ReadString(key, null), NumberStyles.Integer, CultureInfo.InvariantCulture, out int value) ? value : fallback;

        private string ReadString(string key, string fallback)
        {
            var sb = new StringBuilder(64);
            GetPrivateProfileString(Section, key, "", sb, sb.Capacity, _path);
            return sb.Length > 0 ? sb.ToString() : fallback;
        }

        [DllImport("kernel32.dll", CharSet = CharSet.Unicode)]
        private static extern int GetPrivateProfileString(string section, string key, string defaultValue,
            StringBuilder result, int size, string filePath);

        [DllImport("kernel32.dll", CharSet = CharSet.Unicode)]
        private static extern bool WritePrivateProfileString(string section, string key, string value, string filePath);
    }
}
