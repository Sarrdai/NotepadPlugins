using System;
using System.Collections.Generic;
using System.IO;
using System.Text;
using TiffPlusPlus.Core;
using Xunit;

namespace TiffPlusPlus.Tests
{
    public class TiffMetadataReaderTests : IDisposable
    {
        private const string Xml = "<ImageRemarks><Info><Voltage>120</Voltage></Info></ImageRemarks>";
        private readonly string _dir = Path.Combine(Path.GetTempPath(), "TiffPlusPlusTests_" + Guid.NewGuid().ToString("N"));

        public TiffMetadataReaderTests() => Directory.CreateDirectory(_dir);
        public void Dispose() => Directory.Delete(_dir, true);

        [Theory]
        [InlineData(true, (ushort)6)]   // SBYTE, as written by the X-ray software
        [InlineData(false, (ushort)2)]  // ASCII, big endian
        [InlineData(true, (ushort)7)]   // UNDEFINED
        public void FindsXmlInTag(bool littleEndian, ushort type)
        {
            string path = Write(TiffBuilder.Classic(littleEndian, xmlTag: 50635, xmlType: type, xml: Xml + "\0"));

            var meta = TiffMetadataReader.Read(path);

            Assert.True(meta.IsTiff);
            var block = Assert.Single(meta.XmlBlocks);
            Assert.Equal(Xml, block.Xml);
            Assert.Contains("50635", block.Source);
        }

        [Fact]
        public void FindsXmlInBigTiff()
        {
            string path = Write(TiffBuilder.Big(xmlTag: 700, xml: Xml));

            var meta = TiffMetadataReader.Read(path);

            Assert.True(meta.IsBigTiff);
            Assert.Equal(Xml, Assert.Single(meta.XmlBlocks).Xml);
        }

        [Fact]
        public void FallsBackToXmlAppendedAfterImage()
        {
            var bytes = new List<byte>(TiffBuilder.Classic(true, xmlTag: 0, xmlType: 2, xml: null));
            bytes.AddRange(Encoding.UTF8.GetBytes("<?xml version=\"1.0\"?>" + Xml + "\0\0"));
            string path = Write(bytes.ToArray());

            var meta = TiffMetadataReader.Read(path);

            Assert.StartsWith("Appended", Assert.Single(meta.XmlBlocks).Source);
        }

        [Fact]
        public void ReportsMissingXml()
        {
            string path = Write(TiffBuilder.Classic(true, xmlTag: 0, xmlType: 2, xml: null));

            var meta = TiffMetadataReader.Read(path);
            string text = XmlDocumentBuilder.Build(meta);

            Assert.Empty(meta.XmlBlocks);
            Assert.Contains("NO XML FOUND", text);
            Assert.Contains("name=\"ImageWidth\">4<", text);
        }

        [Fact]
        public void ReportsNonTiff()
        {
            string path = Write(Encoding.ASCII.GetBytes("not a tiff file"));

            string text = XmlDocumentBuilder.Build(TiffMetadataReader.Read(path));

            Assert.Contains("NO XML FOUND", text);
            Assert.Contains("byte-order", text);
        }

        [Fact]
        public void PrettyPrintIndentsAndKeepsMalformedXml()
        {
            Assert.Equal("<a>\r\n  <b>1</b>\r\n</a>", XmlDocumentBuilder.PrettyPrint("<a><b>1</b></a>"));
            Assert.Contains("<a><b>", XmlDocumentBuilder.PrettyPrint("<a><b>"));
        }

        [Theory]
        [InlineData("x.tiff", true)]
        [InlineData("x.TIFF", true)]
        [InlineData("x.tif", true)]
        [InlineData("x.Tif", true)]
        [InlineData("x.xml", false)]
        public void RecognisesExtensions(string path, bool expected) =>
            Assert.Equal(expected, TiffMetadataReader.IsTiffPath(path));

        /// <summary>Optional check against a real file: set TIFFPP_SAMPLE to its path.</summary>
        [Fact]
        public void ReadsSampleFileIfConfigured()
        {
            string sample = Environment.GetEnvironmentVariable("TIFFPP_SAMPLE");
            if (string.IsNullOrEmpty(sample)) return;

            var meta = TiffMetadataReader.Read(sample);

            Assert.NotEmpty(meta.XmlBlocks);
            Assert.StartsWith("<", XmlDocumentBuilder.Build(meta));
        }

        private string Write(byte[] data)
        {
            string path = Path.Combine(_dir, Guid.NewGuid().ToString("N") + ".tiff");
            File.WriteAllBytes(path, data);
            return path;
        }
    }

    /// <summary>Writes minimal 4x4 16-bit gray TIFFs with an optional XML tag.</summary>
    internal static class TiffBuilder
    {
        public static byte[] Classic(bool littleEndian, ushort xmlTag, ushort xmlType, string xml)
        {
            var w = new Writer(littleEndian);
            w.Bytes(littleEndian ? new byte[] { (byte)'I', (byte)'I' } : new byte[] { (byte)'M', (byte)'M' });
            w.U16(42);
            w.U32(8 + 32);             // IFD after the pixel data
            w.Bytes(new byte[32]);     // 4x4 pixels, 16 bit

            byte[] xmlBytes = xml == null ? null : Encoding.UTF8.GetBytes(xml);
            var entries = new List<(ushort id, ushort type, uint count, uint value)>
            {
                (256, 3, 1, 4), (257, 3, 1, 4), (258, 3, 1, 16), (273, 4, 1, 8), (279, 4, 1, 32),
            };
            if (xmlBytes != null) entries.Add((xmlTag, xmlType, (uint)xmlBytes.Length, 0));

            int ifdSize = 2 + entries.Count * 12 + 4;
            uint dataOffset = (uint)(w.Position + ifdSize);
            w.U16((ushort)entries.Count);
            foreach (var e in entries)
            {
                w.U16(e.id); w.U16(e.type); w.U32(e.count);
                if (e.id == xmlTag && xmlBytes != null) w.U32(dataOffset);
                else if (e.type == 3) { w.U16((ushort)e.value); w.U16(0); }
                else w.U32(e.value);
            }
            w.U32(0);
            if (xmlBytes != null) w.Bytes(xmlBytes);
            return w.ToArray();
        }

        public static byte[] Big(ushort xmlTag, string xml)
        {
            var w = new Writer(true);
            w.Bytes(new byte[] { (byte)'I', (byte)'I' });
            w.U16(43); w.U16(8); w.U16(0);
            w.U64(16);
            byte[] xmlBytes = Encoding.UTF8.GetBytes(xml);
            w.U64(2);
            w.U16(256); w.U16(3); w.U64(1); w.U64(4);
            w.U16(xmlTag); w.U16(1); w.U64((ulong)xmlBytes.Length); w.U64((ulong)(16 + 8 + 2 * 20 + 8));
            w.U64(0);
            w.Bytes(xmlBytes);
            return w.ToArray();
        }

        private sealed class Writer
        {
            private readonly MemoryStream _ms = new MemoryStream();
            private readonly bool _le;
            public Writer(bool littleEndian) => _le = littleEndian;
            public long Position => _ms.Position;
            public void Bytes(byte[] b) => _ms.Write(b, 0, b.Length);
            public void U16(ushort v) => Ordered(BitConverter.GetBytes(v));
            public void U32(uint v) => Ordered(BitConverter.GetBytes(v));
            public void U64(ulong v) => Ordered(BitConverter.GetBytes(v));
            private void Ordered(byte[] b) { if (BitConverter.IsLittleEndian != _le) Array.Reverse(b); Bytes(b); }
            public byte[] ToArray() => _ms.ToArray();
        }
    }
}
