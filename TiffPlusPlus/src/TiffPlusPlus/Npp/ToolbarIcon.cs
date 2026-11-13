using System;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.Runtime.InteropServices;

namespace TiffPlusPlus.Npp
{
    /// <summary>Draws the "image panel" toolbar button (picture frame with mountain and sun) at runtime.</summary>
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
            using (var pen = new Pen(stroke, 1.2f * s))
            using (var fill = new SolidBrush(stroke))
            using (var sun = new SolidBrush(Color.FromArgb(55, 148, 255)))
            {
                g.Clear(background);
                g.SmoothingMode = background == Color.Transparent ? SmoothingMode.AntiAlias : SmoothingMode.None;
                g.DrawRectangle(pen, 1.5f * s, 2.5f * s, 13f * s, 11f * s);
                g.FillPolygon(fill, new[]
                {
                    new PointF(2.5f * s, 12.5f * s), new PointF(6.5f * s, 7f * s), new PointF(9f * s, 10f * s),
                    new PointF(10.5f * s, 8.5f * s), new PointF(13.5f * s, 12.5f * s),
                });
                g.FillEllipse(sun, 10f * s, 4f * s, 2.6f * s, 2.6f * s);
            }
            return bmp;
        }
    }
}
