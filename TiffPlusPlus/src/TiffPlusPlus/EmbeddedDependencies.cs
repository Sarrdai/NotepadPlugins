using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Reflection;

namespace TiffPlusPlus
{
    /// <summary>
    /// The WebView2 wrapper assemblies and the native WebView2Loader.dll are embedded in the plugin dll,
    /// so the plugin is deployed as a single file (like Tauri, which links the loader statically).
    /// </summary>
    internal static class EmbeddedDependencies
    {
        private const string Prefix = "TiffPlusPlus.Embedded.";
        private static readonly Dictionary<string, Assembly> Loaded = new Dictionary<string, Assembly>(StringComparer.OrdinalIgnoreCase);

        /// <summary>Loads an embedded managed assembly from memory (also immune to the "Mark of the Web").</summary>
        public static Assembly ResolveAssembly(string assemblyName)
        {
            lock (Loaded)
            {
                if (Loaded.TryGetValue(assemblyName, out var cached)) return cached;
                byte[] bytes = ReadResource(Prefix + assemblyName + ".dll");
                if (bytes == null) return null;
                var assembly = Assembly.Load(bytes);
                Loaded[assemblyName] = assembly;
                return assembly;
            }
        }

        /// <summary>Extracts WebView2Loader.dll (native) once and returns its folder.</summary>
        public static string ExtractWebView2Loader()
        {
            byte[] bytes = ReadResource(Prefix + "WebView2Loader.dll")
                ?? throw new InvalidOperationException("WebView2Loader.dll is not embedded in the plugin.");

            string arch = IntPtr.Size == 8 ? "x64" : "x86";
            string dir = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
                "TiffPlusPlus", "loader", arch);
            string file = Path.Combine(dir, "WebView2Loader.dll");

            if (File.Exists(file) && File.ReadAllBytes(file).SequenceEqual(bytes))
                return dir;

            Directory.CreateDirectory(dir);
            string temp = file + "." + Guid.NewGuid().ToString("N") + ".tmp";
            File.WriteAllBytes(temp, bytes);
            try
            {
                if (File.Exists(file)) File.Delete(file);
                File.Move(temp, file);
            }
            catch (IOException) when (File.Exists(file))
            {
                // An older loader is in use by another Notepad++ instance; it stays compatible.
                File.Delete(temp);
            }
            return dir;
        }

        private static byte[] ReadResource(string name)
        {
            using (var stream = typeof(EmbeddedDependencies).Assembly.GetManifestResourceStream(name))
            {
                if (stream == null) return null;
                var buffer = new MemoryStream();
                stream.CopyTo(buffer);
                return buffer.ToArray();
            }
        }
    }
}
