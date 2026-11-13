using System.Globalization;
using System.IO;
using System.Runtime.InteropServices;
using System.Text;

namespace TiffPlusPlus
{
    /// <summary>Plugin settings in TiffPlusPlus.ini inside the Notepad++ plugin config folder (like the other plugins).</summary>
    internal sealed class Settings
    {
        private const string Section = "Settings";
        private readonly string _path;

        public Settings(string configDirectory)
        {
            _path = Path.Combine(configDirectory, "TiffPlusPlus.ini");
            HighlightChanges = ReadInt("HighlightChanges", 1) != 0;
            ShowContrast = ReadInt("ShowContrast", 1) != 0;
            Clip = ReadString("Clip", "0.5");
        }

        public string ConfigDirectory => Path.GetDirectoryName(_path);

        /// <summary>Highlight lines that changed against the previous slice of a stack.</summary>
        public bool HighlightChanges { get; set; }

        /// <summary>Contrast panel of the image viewer expanded.</summary>
        public bool ShowContrast { get; set; }

        /// <summary>Auto contrast clip percentage (invariant culture, e.g. "0.5").</summary>
        public string Clip { get; set; }

        public void Save()
        {
            Directory.CreateDirectory(ConfigDirectory);
            WritePrivateProfileString(Section, "HighlightChanges", HighlightChanges ? "1" : "0", _path);
            WritePrivateProfileString(Section, "ShowContrast", ShowContrast ? "1" : "0", _path);
            WritePrivateProfileString(Section, "Clip", Clip, _path);
        }

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
