using System;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.Runtime.InteropServices;

namespace PathTrail.Npp
{
    /// <summary>Draws the "trail" toolbar button (dotted path from bottom left to a folder at the top right) at runtime.</summary>
    internal static class ToolbarIcon
    {
        // Notepad++ treats this color as transparent in the classic 16x16 toolbar bitmap.
        private static readonly Color BitmapBackground = Color.FromArgb(192, 192, 192);

        public static void Register(NppHost npp, int cmdId)
        {
            var icons = new ToolbarIconsWithDarkMode
            {
                HToolbarBmp = Create(16, Color.FromArgb(64, 64, 64), BitmapBackground).GetHbitmap(BitmapBackground),
                HToolbarIcon = Create(32, Color.FromArgb(64, 64, 64), Color.Transparent).GetHicon(),
                HToolbarIconDarkMode = Create(32, Color.FromArgb(224, 224, 224), Color.Transparent).GetHicon(),
            };
            // Notepad++ keeps the handles for the process lifetime, so neither they nor the struct are freed.
            IntPtr ptr = Marshal.AllocHGlobal(Marshal.SizeOf(typeof(ToolbarIconsWithDarkMode)));
            Marshal.StructureToPtr(icons, ptr, false);
            Win32.SendMessage(npp.Handle, NppMsg.NPPM_ADDTOOLBARICON_FORDARKMODE, new IntPtr(cmdId), ptr);
        }

        private static Bitmap Create(int size, Color stroke, Color background)
        {
            var bmp = new Bitmap(size, size, System.Drawing.Imaging.PixelFormat.Format32bppArgb);
            float s = size / 16f;
            using (var g = Graphics.FromImage(bmp))
            using (var pen = new Pen(stroke, 1.2f * s) { LineJoin = LineJoin.Round })
            using (var dots = new SolidBrush(stroke))
            using (var accent = new SolidBrush(Color.FromArgb(55, 148, 255)))
            {
                g.Clear(background);
                g.SmoothingMode = background == Color.Transparent ? SmoothingMode.AntiAlias : SmoothingMode.None;
                // Folder at the top right.
                g.DrawPolygon(pen, new[]
                {
                    new PointF(8.5f * s, 2.5f * s), new PointF(11f * s, 2.5f * s), new PointF(12f * s, 3.5f * s),
                    new PointF(14.5f * s, 3.5f * s), new PointF(14.5f * s, 8.5f * s), new PointF(8.5f * s, 8.5f * s),
                });
                // Trail of footsteps leading to it, the newest one highlighted.
                g.FillEllipse(dots, 1f * s, 12.5f * s, 2.5f * s, 2.5f * s);
                g.FillEllipse(dots, 3.5f * s, 9f * s, 2.5f * s, 2.5f * s);
                g.FillEllipse(accent, 6.5f * s, 11.5f * s, 3f * s, 3f * s);
            }
            return bmp;
        }
    }
}
