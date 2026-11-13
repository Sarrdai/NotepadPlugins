using System;
using System.Text;

namespace TiffPlusPlus.Npp
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

        public void SetBufferLang(IntPtr bufferId, int lang) =>
            Send(NppMsg.NPPM_SETBUFFERLANGTYPE, bufferId, new IntPtr(lang));

        public void CloseBuffer(IntPtr bufferId)
        {
            long pos = Send(NppMsg.NPPM_GETPOSFROMBUFFERID, bufferId, IntPtr.Zero).ToInt64();
            if (pos == -1) return;
            int view = (int)((pos >> 30) & 0x3);
            int index = (int)(pos & 0x3FFFFFFF);
            Send(NppMsg.NPPM_ACTIVATEDOC, new IntPtr(view), new IntPtr(index));
            Send(NppMsg.NPPM_MENUCOMMAND, IntPtr.Zero, new IntPtr(NppMsg.IDM_FILE_CLOSE));
        }

        public IntPtr CurrentScintilla
        {
            get
            {
                Win32.SendMessage(Handle, NppMsg.NPPM_GETCURRENTSCINTILLA, IntPtr.Zero, out int which);
                return which == 1 ? Data.ScintillaSecondHandle : Data.ScintillaMainHandle;
            }
        }

        public bool IsDarkMode => Send(NppMsg.NPPM_ISDARKMODEENABLED, IntPtr.Zero, IntPtr.Zero) != IntPtr.Zero;

        /// <summary>Reserves a Scintilla marker number for this plugin (falls back to a fixed one).</summary>
        public int AllocateMarker(int fallback)
        {
            bool ok = Win32.SendMessage(Handle, NppMsg.NPPM_ALLOCATEMARKER, new IntPtr(1), out int start) != IntPtr.Zero;
            return ok ? start : fallback;
        }

        public void SetMenuItemCheck(int cmdId, bool isChecked) =>
            Send(NppMsg.NPPM_SETMENUITEMCHECK, new IntPtr(cmdId), new IntPtr(isChecked ? 1 : 0));
    }
}
