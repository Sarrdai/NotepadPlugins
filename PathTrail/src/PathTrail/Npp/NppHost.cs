using System;
using System.Text;

namespace PathTrail.Npp
{
    /// <summary>Thin typed wrapper around the Notepad++ message API.</summary>
    public sealed class NppHost
    {
        public NppHost(NppData data) => Data = data;

        public NppData Data { get; }
        public IntPtr Handle => Data.NppHandle;

        private IntPtr Send(int msg, IntPtr w, IntPtr l) => Win32.SendMessage(Handle, msg, w, l);

        public IntPtr CurrentBufferId => Send(NppMsg.NPPM_GETCURRENTBUFFERID, IntPtr.Zero, IntPtr.Zero);

        public string GetPathFromBufferId(IntPtr bufferId)
        {
            int len = Win32.SendMessage(Handle, NppMsg.NPPM_GETFULLPATHFROMBUFFERID, bufferId, (StringBuilder)null).ToInt32();
            if (len <= 0) return null;
            var sb = new StringBuilder(len + 1);
            Win32.SendMessage(Handle, NppMsg.NPPM_GETFULLPATHFROMBUFFERID, bufferId, sb);
            return sb.ToString();
        }

        public bool OpenFile(string path) =>
            Win32.SendMessage(Handle, NppMsg.NPPM_DOOPEN, IntPtr.Zero, path) != IntPtr.Zero;

        /// <summary>Activates an already opened file; returns false if it is not open.</summary>
        public bool SwitchToFile(string path) =>
            Win32.SendMessage(Handle, NppMsg.NPPM_SWITCHTOFILE, IntPtr.Zero, path) != IntPtr.Zero;

        /// <summary>Notepad++ plugin config folder (e.g. %AppData%\Notepad++\plugins\config).</summary>
        public string PluginsConfigDirectory
        {
            get
            {
                var sb = new StringBuilder(260);
                Win32.SendMessage(Handle, NppMsg.NPPM_GETPLUGINSCONFIGDIR, new IntPtr(sb.Capacity), sb);
                return sb.ToString();
            }
        }

        public bool IsDarkMode => Send(NppMsg.NPPM_ISDARKMODEENABLED, IntPtr.Zero, IntPtr.Zero) != IntPtr.Zero;

        public void SetMenuItemCheck(int cmdId, bool isChecked) =>
            Send(NppMsg.NPPM_SETMENUITEMCHECK, new IntPtr(cmdId), new IntPtr(isChecked ? 1 : 0));
    }
}
