using System;
using System.IO;
using System.Security.Cryptography;
using System.Text;

namespace TiffPlusPlus.Core
{
    /// <summary>
    /// The XML is shown through a read-only temp file "&lt;image name&gt;.xml" so Notepad++ gets a proper tab name
    /// and XML highlighting. A ".source" sidecar remembers the original TIFF (survives session restore).
    /// </summary>
    public static class XmlViewFiles
    {
        private const string SourceSuffix = ".source";

        public static string RootDirectory { get; } =
            Path.Combine(Path.GetTempPath(), "TiffPlusPlus");

        private const string StackSuffix = ".stack";

        public static string GetViewPath(string tiffPath)
        {
            string full = Path.GetFullPath(tiffPath);
            return Path.Combine(RootDirectory, Hash(full), Path.GetFileName(full) + ".xml");
        }

        public static string Write(string tiffPath, string content)
        {
            string viewPath = GetViewPath(tiffPath);
            WriteReadOnly(viewPath, content);
            File.WriteAllText(viewPath + SourceSuffix, Path.GetFullPath(tiffPath), Encoding.UTF8);
            return viewPath;
        }

        /// <summary>One view file per set of files; the tab shows "&lt;name&gt; [stack].xml".</summary>
        public static string GetStackViewPath(TiffStack stack)
        {
            string fileName = string.Concat(stack.Name.Split(Path.GetInvalidFileNameChars())) + " [stack].xml";
            return Path.Combine(RootDirectory, Hash(string.Join("|", stack.Files)), fileName);
        }

        public static string WriteStack(TiffStack stack, string content)
        {
            string viewPath = GetStackViewPath(stack);
            WriteReadOnly(viewPath, content);
            SaveStack(viewPath, stack);
            return viewPath;
        }

        /// <summary>Persists name, current slice and file list so a restored session can rebuild the stack.</summary>
        public static void SaveStack(string viewPath, TiffStack stack) =>
            File.WriteAllText(viewPath + StackSuffix, stack.Serialize(), Encoding.UTF8);

        public static TiffStack LoadStack(string viewPath)
        {
            try
            {
                string sidecar = viewPath + StackSuffix;
                return File.Exists(sidecar) ? TiffStack.Deserialize(File.ReadAllText(sidecar, Encoding.UTF8)) : null;
            }
            catch (IOException)
            {
                return null;
            }
        }

        public static bool IsStackView(string documentPath) =>
            !string.IsNullOrEmpty(documentPath) &&
            documentPath.StartsWith(RootDirectory, StringComparison.OrdinalIgnoreCase) &&
            File.Exists(documentPath + StackSuffix);

        private static void WriteReadOnly(string path, string content)
        {
            Directory.CreateDirectory(Path.GetDirectoryName(path));
            if (File.Exists(path))
                File.SetAttributes(path, FileAttributes.Normal);
            File.WriteAllText(path, content, new UTF8Encoding(true));
            // Read-only file: Notepad++ opens it locked, which protects against accidental edits.
            File.SetAttributes(path, FileAttributes.ReadOnly);
        }

        private static string Hash(string text)
        {
            using (var sha = SHA1.Create())
                return BitConverter.ToString(sha.ComputeHash(Encoding.UTF8.GetBytes(text.ToUpperInvariant())), 0, 6).Replace("-", "");
        }

        /// <summary>Returns the TIFF behind a view file, or the path itself if it is a TIFF.</summary>
        public static string ResolveImagePath(string documentPath)
        {
            if (string.IsNullOrEmpty(documentPath)) return null;
            if (TiffMetadataReader.IsTiffPath(documentPath)) return documentPath;
            if (!documentPath.StartsWith(RootDirectory, StringComparison.OrdinalIgnoreCase)) return null;
            try
            {
                string sidecar = documentPath + SourceSuffix;
                return File.Exists(sidecar) ? File.ReadAllText(sidecar, Encoding.UTF8).Trim() : null;
            }
            catch (IOException)
            {
                return null;
            }
        }
    }
}
