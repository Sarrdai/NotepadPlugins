using System;
using System.IO;
using System.Reflection;
using System.Runtime.InteropServices;
using RGiesecke.DllExport;

namespace PathTrail.Npp
{
    /// <summary>Native entry points expected by Notepad++ (see PluginInterface.h).</summary>
    internal static class UnmanagedExports
    {
        private static readonly IntPtr PluginNamePtr = Marshal.StringToHGlobalUni(Plugin.PluginName);

        static UnmanagedExports()
        {
            // Notepad++ probes assemblies relative to notepad++.exe, not the plugin folder.
            AppDomain.CurrentDomain.AssemblyResolve += ResolveFromPluginFolder;
        }

        private static Assembly ResolveFromPluginFolder(object sender, ResolveEventArgs args)
        {
            string name = new AssemblyName(args.Name).Name;
            Assembly embedded = EmbeddedDependencies.ResolveAssembly(name);
            if (embedded != null) return embedded;

            string file = Path.Combine(Path.GetDirectoryName(typeof(UnmanagedExports).Assembly.Location), name + ".dll");
            // UnsafeLoadFrom: files extracted from a downloaded zip carry the "Mark of the Web",
            // which makes LoadFrom fail with HRESULT 0x80131515.
            return File.Exists(file) ? Assembly.UnsafeLoadFrom(file) : null;
        }

        [DllExport(CallingConvention = CallingConvention.Cdecl)]
        private static bool isUnicode() => true;

        [DllExport(CallingConvention = CallingConvention.Cdecl)]
        private static void setInfo(NppData data) => Plugin.Initialize(data);

        [DllExport(CallingConvention = CallingConvention.Cdecl)]
        private static IntPtr getFuncsArray(ref int count)
        {
            count = Plugin.Commands.Count;
            return Plugin.Commands.ToNative();
        }

        [DllExport(CallingConvention = CallingConvention.Cdecl)]
        private static uint messageProc(uint message, IntPtr wParam, IntPtr lParam) => 1;

        [DllExport(CallingConvention = CallingConvention.Cdecl)]
        private static IntPtr getName() => PluginNamePtr;

        [DllExport(CallingConvention = CallingConvention.Cdecl)]
        private static void beNotified(IntPtr notifyCode)
        {
            var header = (NmHdr)Marshal.PtrToStructure(notifyCode, typeof(NmHdr));
            Plugin.OnNotification(header);
        }
    }
}
