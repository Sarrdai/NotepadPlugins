using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Text;

namespace TiffPlusPlus.Npp
{
    [StructLayout(LayoutKind.Sequential)]
    public struct NppData
    {
        public IntPtr NppHandle;
        public IntPtr ScintillaMainHandle;
        public IntPtr ScintillaSecondHandle;
    }

    [StructLayout(LayoutKind.Sequential)]
    public struct NmHdr
    {
        public IntPtr HwndFrom;
        public IntPtr IdFrom;
        public uint Code;
    }

    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    public delegate void NppFuncItemDelegate();

    /// <summary>Mirror of the native FuncItem struct (Notepad++ PluginInterface.h).</summary>
    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
    public struct FuncItem
    {
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 64)]
        public string ItemName;
        public IntPtr PFunc;
        public int CmdId;
        [MarshalAs(UnmanagedType.I1)]
        public bool Init2Check;
        public IntPtr PShKey;
    }

    /// <summary>Mirror of the native tTbData struct (Docking.h).</summary>
    [StructLayout(LayoutKind.Sequential)]
    public struct TbData
    {
        public IntPtr HClient;
        public IntPtr PszName;
        public int DlgId;
        public uint UMask;
        public IntPtr HIconTab;
        public IntPtr PszAddInfo;
        public Rect RcFloat;
        public int IPrevCont;
        public IntPtr PszModuleName;
    }

    /// <summary>Mirror of toolbarIconsWithDarkMode (Notepad++ 8.0+).</summary>
    [StructLayout(LayoutKind.Sequential)]
    public struct ToolbarIconsWithDarkMode
    {
        public IntPtr HToolbarBmp;
        public IntPtr HToolbarIcon;
        public IntPtr HToolbarIconDarkMode;
    }

    [StructLayout(LayoutKind.Sequential)]
    public struct Rect
    {
        public int Left, Top, Right, Bottom;
    }

    /// <summary>Mirror of the native ShortcutKey struct.</summary>
    [StructLayout(LayoutKind.Sequential)]
    public struct ShortcutKey
    {
        [MarshalAs(UnmanagedType.I1)] public bool IsCtrl;
        [MarshalAs(UnmanagedType.I1)] public bool IsAlt;
        [MarshalAs(UnmanagedType.I1)] public bool IsShift;
        public byte Key;

        public ShortcutKey(bool ctrl, bool alt, bool shift, byte key)
        {
            IsCtrl = ctrl; IsAlt = alt; IsShift = shift; Key = key;
        }
    }

    /// <summary>Keeps the unmanaged FuncItem array (and its delegates) alive for the plugin lifetime.</summary>
    public sealed class FuncItems
    {
        private readonly List<FuncItem> _items = new List<FuncItem>();
        private readonly List<NppFuncItemDelegate> _keepAlive = new List<NppFuncItemDelegate>();
        private IntPtr _native = IntPtr.Zero;

        public int Count => _items.Count;

        public int Add(string name, NppFuncItemDelegate func, bool checkedInit = false, ShortcutKey? shortcut = null)
        {
            _keepAlive.Add(func);
            IntPtr shortcutPtr = IntPtr.Zero;
            if (shortcut.HasValue)
            {
                // Notepad++ keeps the pointer; allocated once per command for the process lifetime.
                shortcutPtr = Marshal.AllocHGlobal(Marshal.SizeOf(typeof(ShortcutKey)));
                Marshal.StructureToPtr(shortcut.Value, shortcutPtr, false);
            }
            _items.Add(new FuncItem
            {
                ItemName = name,
                PFunc = func == null ? IntPtr.Zero : Marshal.GetFunctionPointerForDelegate(func),
                CmdId = 0,
                Init2Check = checkedInit,
                PShKey = shortcutPtr,
            });
            return _items.Count - 1;
        }

        public IntPtr ToNative()
        {
            int size = Marshal.SizeOf(typeof(FuncItem));
            _native = Marshal.AllocHGlobal(size * _items.Count);
            for (int i = 0; i < _items.Count; i++)
                Marshal.StructureToPtr(_items[i], _native + i * size, false);
            return _native;
        }

        /// <summary>Command ids are assigned by Notepad++ after getFuncsArray; read them back.</summary>
        public int GetCmdId(int index)
        {
            int size = Marshal.SizeOf(typeof(FuncItem));
            var item = (FuncItem)Marshal.PtrToStructure(_native + index * size, typeof(FuncItem));
            return item.CmdId;
        }
    }

    public static class NppMsg
    {
        private const int WM_USER = 0x400;
        private const int NPPMSG = WM_USER + 1000;

        public const int NPPM_GETCURRENTSCINTILLA = NPPMSG + 4;
        public const int NPPM_ACTIVATEDOC = NPPMSG + 28;
        public const int NPPM_DMMSHOW = NPPMSG + 30;
        public const int NPPM_DMMHIDE = NPPMSG + 31;
        public const int NPPM_DMMREGASDCKDLG = NPPMSG + 33;
        public const int NPPM_SWITCHTOFILE = NPPMSG + 37;
        public const int NPPM_SETMENUITEMCHECK = NPPMSG + 40;
        public const int NPPM_MENUCOMMAND = NPPMSG + 48;
        public const int NPPM_GETPOSFROMBUFFERID = NPPMSG + 57;
        public const int NPPM_GETFULLPATHFROMBUFFERID = NPPMSG + 58;
        public const int NPPM_GETCURRENTBUFFERID = NPPMSG + 60;
        public const int NPPM_SETBUFFERLANGTYPE = NPPMSG + 65;
        public const int NPPM_DOOPEN = NPPMSG + 77;
        public const int NPPM_ALLOCATEMARKER = NPPMSG + 82;
        public const int NPPM_ADDTOOLBARICON_FORDARKMODE = NPPMSG + 101;
        public const int NPPM_ISDARKMODEENABLED = NPPMSG + 107;

        public const int NPPN_FIRST = 1000;
        public const uint NPPN_READY = NPPN_FIRST + 1;
        public const uint NPPN_TBMODIFICATION = NPPN_FIRST + 2;
        public const uint NPPN_FILEOPENED = NPPN_FIRST + 4;
        public const uint NPPN_SHUTDOWN = NPPN_FIRST + 9;
        public const uint NPPN_BUFFERACTIVATED = NPPN_FIRST + 10;

        public const int IDM_FILE_CLOSE = 41000 + 3;
        public const int L_XML = 9;

        // Docking manager
        public const uint DWS_ICONTAB = 0x00000001;
        public const uint DWS_DF_CONT_RIGHT = 1u << 28;
        public const uint DMN_CLOSE = 1050 + 1;
    }

    public static class Sci
    {
        public const int SCI_GETCURRENTPOS = 2008;
        public const int SCI_SETSAVEPOINT = 2014;
        public const int SCI_GOTOPOS = 2025;
        public const int SCI_MARKERDEFINE = 2040;
        public const int SCI_MARKERSETBACK = 2042;
        public const int SCI_MARKERADD = 2043;
        public const int SCI_MARKERDELETEALL = 2045;
        public const int SCI_GETFIRSTVISIBLELINE = 2152;
        public const int SCI_LINEFROMPOSITION = 2166;
        public const int SCI_SETREADONLY = 2171;
        public const int SCI_EMPTYUNDOBUFFER = 2175;
        public const int SCI_SETTEXT = 2181;
        public const int SCI_GETTEXTLENGTH = 2183;
        public const int SCI_SETFIRSTVISIBLELINE = 2613;
        public const int SC_MARK_BACKGROUND = 22;
    }

    public static class Win32
    {
        public const int WM_DROPFILES = 0x0233;
        public const int WM_NOTIFY = 0x004E;
        public const int GWLP_WNDPROC = -4;

        public delegate IntPtr WndProc(IntPtr hWnd, uint msg, IntPtr wParam, IntPtr lParam);

        [DllImport("user32.dll", CharSet = CharSet.Unicode)]
        public static extern IntPtr SendMessage(IntPtr hWnd, int msg, IntPtr wParam, IntPtr lParam);

        [DllImport("user32.dll", CharSet = CharSet.Unicode)]
        public static extern IntPtr SendMessage(IntPtr hWnd, int msg, IntPtr wParam, string lParam);

        [DllImport("user32.dll", CharSet = CharSet.Unicode)]
        public static extern IntPtr SendMessage(IntPtr hWnd, int msg, IntPtr wParam, StringBuilder lParam);

        [DllImport("user32.dll")]
        public static extern IntPtr SendMessage(IntPtr hWnd, int msg, IntPtr wParam, byte[] lParam);

        [DllImport("user32.dll")]
        public static extern IntPtr SendMessage(IntPtr hWnd, int msg, IntPtr wParam, out int lParam);

        [DllImport("user32.dll", EntryPoint = "SetWindowLongPtrW")]
        private static extern IntPtr SetWindowLongPtr64(IntPtr hWnd, int index, IntPtr value);

        [DllImport("user32.dll", EntryPoint = "SetWindowLongW")]
        private static extern IntPtr SetWindowLong32(IntPtr hWnd, int index, IntPtr value);

        public static IntPtr SetWindowLongPtr(IntPtr hWnd, int index, IntPtr value) =>
            IntPtr.Size == 8 ? SetWindowLongPtr64(hWnd, index, value) : SetWindowLong32(hWnd, index, value);

        [DllImport("user32.dll", EntryPoint = "CallWindowProcW")]
        public static extern IntPtr CallWindowProc(IntPtr prev, IntPtr hWnd, uint msg, IntPtr wParam, IntPtr lParam);

        [DllImport("shell32.dll", CharSet = CharSet.Unicode)]
        public static extern uint DragQueryFile(IntPtr hDrop, uint index, StringBuilder file, uint size);

        [DllImport("shell32.dll")]
        public static extern void DragFinish(IntPtr hDrop);
    }
}
