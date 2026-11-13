using System.IO;
using System.Windows.Media;
using System.Windows.Media.Imaging;

namespace TiffPlusPlus.Viewer
{
    public sealed class PixelImage
    {
        public int Width { get; set; }
        public int Height { get; set; }
        /// <summary>"gray16" (little-endian uint16) or "bgra8".</summary>
        public string Format { get; set; }
        public int SourceBitsPerPixel { get; set; }
        public int FrameCount { get; set; }
        public byte[] Data { get; set; }
    }

    /// <summary>Decodes the first frame through WIC (uncompressed, LZW, Deflate, PackBits, JPEG, ...).</summary>
    public static class PixelLoader
    {
        public static PixelImage Load(string path)
        {
            using (var fs = new FileStream(path, FileMode.Open, FileAccess.Read, FileShare.ReadWrite | FileShare.Delete))
            {
                var decoder = BitmapDecoder.Create(fs,
                    BitmapCreateOptions.PreservePixelFormat | BitmapCreateOptions.IgnoreColorProfile,
                    BitmapCacheOption.OnLoad);
                BitmapSource frame = decoder.Frames[0];
                PixelFormat source = frame.Format;

                bool isGray = source == PixelFormats.Gray16 || source == PixelFormats.Gray8 ||
                              source == PixelFormats.Gray4 || source == PixelFormats.Gray2 ||
                              source == PixelFormats.BlackWhite || source == PixelFormats.Gray32Float;
                PixelFormat target = isGray ? PixelFormats.Gray16 : PixelFormats.Bgra32;
                if (source != target)
                    frame = new FormatConvertedBitmap(frame, target, null, 0);

                int stride = (frame.PixelWidth * target.BitsPerPixel + 7) / 8;
                var data = new byte[stride * frame.PixelHeight];
                frame.CopyPixels(data, stride, 0);

                return new PixelImage
                {
                    Width = frame.PixelWidth,
                    Height = frame.PixelHeight,
                    Format = isGray ? "gray16" : "bgra8",
                    SourceBitsPerPixel = source.BitsPerPixel,
                    FrameCount = decoder.Frames.Count,
                    Data = data,
                };
            }
        }
    }
}
