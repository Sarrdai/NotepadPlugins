using System;
using System.Collections.Generic;
using System.IO;
using System.Text;

namespace TiffPlusPlus.Core
{
    public sealed class TiffTag
    {
        public int Ifd { get; set; }
        public ushort Id { get; set; }
        public ushort Type { get; set; }
        public long Count { get; set; }
        /// <summary>Absolute file offset of the value (or of the inline value field).</summary>
        public long ValueOffset { get; set; }
        /// <summary>Short textual representation (numbers or truncated text).</summary>
        public string Preview { get; set; }
    }

    public sealed class XmlBlock
    {
        public string Source { get; set; }
        public string Xml { get; set; }
    }

    public sealed class TiffMetadata
    {
        public string Path { get; set; }
        public long FileSize { get; set; }
        public bool IsTiff { get; set; }
        public bool IsBigTiff { get; set; }
        public bool LittleEndian { get; set; }
        public string Error { get; set; }
        public List<TiffTag> Tags { get; } = new List<TiffTag>();
        public List<XmlBlock> XmlBlocks { get; } = new List<XmlBlock>();
    }

    /// <summary>
    /// Reads TIFF header, IFDs and XML payloads with random access only.
    /// Pixel data is never touched, so even huge images are processed in milliseconds.
    /// </summary>
    public static class TiffMetadataReader
    {
        private const int MaxIfds = 64;
        private const int MaxTagsPerIfd = 4096;
        private const int MaxXmlBytes = 64 * 1024 * 1024;
        private const int TailScanBytes = 4 * 1024 * 1024;

        private static readonly HashSet<string> Extensions =
            new HashSet<string>(StringComparer.OrdinalIgnoreCase) { ".tif", ".tiff" };

        public static bool IsTiffPath(string path) =>
            !string.IsNullOrEmpty(path) && Extensions.Contains(System.IO.Path.GetExtension(path));

        public static TiffMetadata Read(string path)
        {
            var result = new TiffMetadata { Path = path };
            try
            {
                using (var fs = new FileStream(path, FileMode.Open, FileAccess.Read,
                           FileShare.ReadWrite | FileShare.Delete, 4096, FileOptions.RandomAccess))
                {
                    result.FileSize = fs.Length;
                    var reader = new EndianReader(fs);
                    if (ReadHeader(reader, result, out long firstIfd))
                        ReadIfds(reader, result, firstIfd);
                    if (result.XmlBlocks.Count == 0)
                        ScanTailForXml(fs, result);
                }
            }
            catch (Exception ex) when (ex is IOException || ex is UnauthorizedAccessException)
            {
                result.Error = ex.Message;
            }
            return result;
        }

        private static bool ReadHeader(EndianReader r, TiffMetadata result, out long firstIfd)
        {
            firstIfd = 0;
            if (r.Length < 8) { result.Error = "File too small for a TIFF header."; return false; }
            byte[] head = r.ReadBytes(0, 4);
            if (head[0] == 'I' && head[1] == 'I') r.LittleEndian = true;
            else if (head[0] == 'M' && head[1] == 'M') r.LittleEndian = false;
            else { result.Error = "No TIFF byte-order mark (II/MM)."; return false; }

            ushort magic = r.UInt16(2);
            if (magic == 42)
            {
                firstIfd = r.UInt32(4);
            }
            else if (magic == 43)
            {
                result.IsBigTiff = true;
                firstIfd = (long)r.UInt64(8);
            }
            else { result.Error = $"Unknown TIFF version {magic}."; return false; }

            result.IsTiff = true;
            result.LittleEndian = r.LittleEndian;
            return true;
        }

        private static void ReadIfds(EndianReader r, TiffMetadata result, long ifdOffset)
        {
            bool big = result.IsBigTiff;
            int entrySize = big ? 20 : 12;
            var visited = new HashSet<long>();

            for (int ifd = 0; ifd < MaxIfds && ifdOffset > 0 && ifdOffset < r.Length && visited.Add(ifdOffset); ifd++)
            {
                long count = big ? (long)r.UInt64(ifdOffset) : r.UInt16(ifdOffset);
                if (count <= 0 || count > MaxTagsPerIfd) break;
                long entriesStart = ifdOffset + (big ? 8 : 2);
                byte[] entries = r.ReadBytes(entriesStart, (int)(count * entrySize));
                if (entries.Length < count * entrySize) break;

                for (int i = 0; i < count; i++)
                {
                    int e = i * entrySize;
                    ushort id = r.UInt16(entries, e);
                    ushort type = r.UInt16(entries, e + 2);
                    long n = big ? (long)r.UInt64(entries, e + 4) : r.UInt32(entries, e + 4);
                    int typeSize = TypeSize(type);
                    long byteCount = typeSize * n;
                    int inlineSize = big ? 8 : 4;
                    long fieldPos = entriesStart + e + (big ? 12 : 8);
                    long valueOffset = byteCount <= inlineSize
                        ? fieldPos
                        : (big ? (long)r.UInt64(entries, e + 12) : r.UInt32(entries, e + 8));

                    var tag = new TiffTag { Ifd = ifd, Id = id, Type = type, Count = n, ValueOffset = valueOffset };
                    result.Tags.Add(tag);
                    if (typeSize == 0 || valueOffset + byteCount > r.Length) { tag.Preview = "<invalid>"; continue; }
                    ReadTagValue(r, tag, byteCount, result);
                }

                long nextPos = entriesStart + count * entrySize;
                ifdOffset = big ? (long)r.UInt64(nextPos) : r.UInt32(nextPos);
            }
        }

        private static void ReadTagValue(EndianReader r, TiffTag tag, long byteCount, TiffMetadata result)
        {
            bool isBytes = tag.Type == 1 || tag.Type == 2 || tag.Type == 6 || tag.Type == 7;
            if (isBytes)
            {
                byte[] probe = r.ReadBytes(tag.ValueOffset, (int)Math.Min(byteCount, 64));
                if (byteCount >= 2 && LooksLikeXml(probe) && byteCount <= MaxXmlBytes)
                {
                    string xml = DecodeText(r.ReadBytes(tag.ValueOffset, (int)byteCount));
                    result.XmlBlocks.Add(new XmlBlock { Source = $"TIFF tag {tag.Id} ({TagNames.Get(tag.Id)}), IFD {tag.Ifd}", Xml = xml });
                    tag.Preview = $"<XML, {byteCount} bytes>";
                }
                else if (tag.Type == 2)
                {
                    byte[] text = r.ReadBytes(tag.ValueOffset, (int)Math.Min(byteCount, 512));
                    tag.Preview = DecodeText(text).Replace("\0", " | ").Trim(' ', '|');
                }
                else
                {
                    tag.Preview = $"<{byteCount} bytes>";
                }
                return;
            }

            int shown = (int)Math.Min(tag.Count, 16);
            int size = TypeSize(tag.Type);
            byte[] data = r.ReadBytes(tag.ValueOffset, shown * size);
            var sb = new StringBuilder();
            for (int i = 0; i < shown && (i + 1) * size <= data.Length; i++)
            {
                if (i > 0) sb.Append(", ");
                sb.Append(FormatNumber(r, data, i * size, tag.Type));
            }
            if (tag.Count > shown) sb.Append($", ... ({tag.Count} values)");
            tag.Preview = sb.ToString();
        }

        private static string FormatNumber(EndianReader r, byte[] d, int o, ushort type)
        {
            var inv = System.Globalization.CultureInfo.InvariantCulture;
            switch (type)
            {
                case 3: return r.UInt16(d, o).ToString(inv);
                case 8: return ((short)r.UInt16(d, o)).ToString(inv);
                case 4: case 13: return r.UInt32(d, o).ToString(inv);
                case 9: return ((int)r.UInt32(d, o)).ToString(inv);
                case 5: return $"{r.UInt32(d, o)}/{r.UInt32(d, o + 4)}";
                case 10: return $"{(int)r.UInt32(d, o)}/{(int)r.UInt32(d, o + 4)}";
                case 11: return BitConverter.ToSingle(BitConverter.GetBytes(r.UInt32(d, o)), 0).ToString("R", inv);
                case 12: return BitConverter.Int64BitsToDouble((long)r.UInt64(d, o)).ToString("R", inv);
                case 16: case 18: return r.UInt64(d, o).ToString(inv);
                case 17: return ((long)r.UInt64(d, o)).ToString(inv);
                default: return "?";
            }
        }

        /// <summary>Fallback for XML simply appended after the TIFF structure.</summary>
        private static void ScanTailForXml(FileStream fs, TiffMetadata result)
        {
            long start = Math.Max(0, fs.Length - TailScanBytes);
            var buffer = new byte[fs.Length - start];
            fs.Seek(start, SeekOrigin.Begin);
            int read = 0;
            while (read < buffer.Length)
            {
                int n = fs.Read(buffer, read, buffer.Length - read);
                if (n <= 0) break;
                read += n;
            }

            int xmlStart = IndexOf(buffer, read, Encoding.ASCII.GetBytes("<?xml"));
            if (xmlStart < 0) return;
            int xmlEnd = read - 1;
            while (xmlEnd > xmlStart && buffer[xmlEnd] != (byte)'>') xmlEnd--;
            if (xmlEnd <= xmlStart) return;

            string xml = DecodeText(SubArray(buffer, xmlStart, xmlEnd - xmlStart + 1));
            result.XmlBlocks.Add(new XmlBlock { Source = $"Appended data at file offset {start + xmlStart}", Xml = xml });
        }

        internal static bool LooksLikeXml(byte[] probe)
        {
            int i = 0;
            if (probe.Length >= 3 && probe[0] == 0xEF && probe[1] == 0xBB && probe[2] == 0xBF) i = 3;
            while (i < probe.Length && (probe[i] == ' ' || probe[i] == '\t' || probe[i] == '\r' || probe[i] == '\n')) i++;
            if (i + 1 >= probe.Length || probe[i] != '<') return false;
            byte c = probe[i + 1];
            return c == '?' || c == '!' || c == '_' || char.IsLetter((char)c);
        }

        private static string DecodeText(byte[] bytes)
        {
            int len = bytes.Length;
            while (len > 0 && bytes[len - 1] == 0) len--;
            int offset = len >= 3 && bytes[0] == 0xEF && bytes[1] == 0xBB && bytes[2] == 0xBF ? 3 : 0;
            return new UTF8Encoding(false, false).GetString(bytes, offset, len - offset);
        }

        private static int TypeSize(ushort type)
        {
            switch (type)
            {
                case 1: case 2: case 6: case 7: return 1;
                case 3: case 8: return 2;
                case 4: case 9: case 11: case 13: return 4;
                case 5: case 10: case 12: case 16: case 17: case 18: return 8;
                default: return 0;
            }
        }

        private static int IndexOf(byte[] data, int length, byte[] pattern)
        {
            for (int i = 0; i <= length - pattern.Length; i++)
            {
                int j = 0;
                while (j < pattern.Length && data[i + j] == pattern[j]) j++;
                if (j == pattern.Length) return i;
            }
            return -1;
        }

        private static byte[] SubArray(byte[] data, int offset, int length)
        {
            var copy = new byte[length];
            Buffer.BlockCopy(data, offset, copy, 0, length);
            return copy;
        }

        private sealed class EndianReader
        {
            private readonly FileStream _fs;
            public EndianReader(FileStream fs) => _fs = fs;
            public bool LittleEndian { get; set; } = true;
            public long Length => _fs.Length;

            public byte[] ReadBytes(long offset, int count)
            {
                if (offset < 0 || offset >= _fs.Length) return new byte[0];
                count = (int)Math.Min(count, _fs.Length - offset);
                var buf = new byte[count];
                _fs.Seek(offset, SeekOrigin.Begin);
                int read = 0;
                while (read < count)
                {
                    int n = _fs.Read(buf, read, count - read);
                    if (n <= 0) break;
                    read += n;
                }
                return buf;
            }

            public ushort UInt16(long offset) => UInt16(ReadBytes(offset, 2), 0);
            public uint UInt32(long offset) => UInt32(ReadBytes(offset, 4), 0);
            public ulong UInt64(long offset) => UInt64(ReadBytes(offset, 8), 0);

            public ushort UInt16(byte[] b, int o) => b.Length < o + 2 ? (ushort)0 : LittleEndian
                ? (ushort)(b[o] | b[o + 1] << 8)
                : (ushort)(b[o] << 8 | b[o + 1]);

            public uint UInt32(byte[] b, int o) => b.Length < o + 4 ? 0u : LittleEndian
                ? (uint)(b[o] | b[o + 1] << 8 | b[o + 2] << 16 | b[o + 3] << 24)
                : (uint)(b[o] << 24 | b[o + 1] << 16 | b[o + 2] << 8 | b[o + 3]);

            public ulong UInt64(byte[] b, int o)
            {
                if (b.Length < o + 8) return 0;
                ulong lo = UInt32(b, LittleEndian ? o : o + 4);
                ulong hi = UInt32(b, LittleEndian ? o + 4 : o);
                return hi << 32 | lo;
            }
        }
    }
}
