using System.ComponentModel;
using System.Diagnostics;
using System.IO;
using PathTrail.Core;

namespace PathTrail
{
    /// <summary>Starts Explorer, a terminal or Notepad++ "Folder as Workspace" for a folder.</summary>
    internal static class ShellActions
    {
        public static void OpenFolder(string folder, FolderAction action)
        {
            switch (action)
            {
                case FolderAction.Terminal:
                    OpenTerminal(folder);
                    break;
                case FolderAction.Workspace:
                    OpenWorkspace(folder);
                    break;
                default:
                    Start("explorer.exe", Quote(folder));
                    break;
            }
        }

        /// <summary>Opens Explorer on the folder of the file with the file selected.</summary>
        public static void ShowInExplorer(string file) => Start("explorer.exe", "/select," + Quote(file));

        /// <summary>Windows Terminal if installed (an app execution alias), otherwise cmd.</summary>
        public static void OpenTerminal(string folder)
        {
            try
            {
                Process.Start(new ProcessStartInfo("wt.exe", "-d " + Quote(folder)) { UseShellExecute = true });
            }
            catch (Win32Exception)
            {
                Process.Start(new ProcessStartInfo("cmd.exe") { WorkingDirectory = folder, UseShellExecute = true });
            }
        }

        /// <summary>
        /// Notepad++ forwards the command line of a second start to the running instance, so the folder opens
        /// in its "Folder as Workspace" panel without a new window.
        /// </summary>
        public static void OpenWorkspace(string folder)
        {
            string exe = Process.GetCurrentProcess().MainModule.FileName;
            Start(exe, "-openFoldersAsWorkspace " + Quote(folder));
        }

        /// <summary>The folder itself or its nearest existing parent (moved or deleted folders), or null.</summary>
        public static string ExistingFolder(string folder)
        {
            for (string current = folder; current != null; current = TrailPath.Parent(current))
                if (Directory.Exists(current)) return current;
            return null;
        }

        private static void Start(string file, string arguments) =>
            Process.Start(new ProcessStartInfo(file, arguments) { UseShellExecute = false });

        /// <summary>Quotes a path; a trailing backslash ("C:\") would escape the closing quote.</summary>
        private static string Quote(string path) => "\"" + (path.EndsWith("\\") ? path + "." : path) + "\"";
    }
}
