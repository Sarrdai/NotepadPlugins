using System;
using System.IO;
using System.Linq;
using System.Text;
using System.Xml;
using System.Xml.Linq;

namespace TiffPlusPlus.Core
{
    /// <summary>Builds the text shown in Notepad++ for a TIFF file.</summary>
    public static class XmlDocumentBuilder
    {
        public static string Build(TiffMetadata meta)
        {
            if (meta.XmlBlocks.Count == 1)
                return PrettyPrint(meta.XmlBlocks[0].Xml);

            var sb = new StringBuilder();
            if (meta.XmlBlocks.Count > 1)
            {
                foreach (var block in meta.XmlBlocks)
                {
                    sb.Append("<!-- ").Append(EscapeComment(block.Source)).Append(" -->\r\n");
                    sb.Append(PrettyPrint(block.Xml)).Append("\r\n\r\n");
                }
                return sb.ToString();
            }

            // No XML found: say so clearly and list the basic TIFF tags instead.
            string reason = meta.Error != null ? $"Could not read TIFF structure: {meta.Error}" : "This file contains no embedded XML.";
            sb.Append("<!--\r\n");
            sb.Append("  NO XML FOUND\r\n");
            sb.Append("  File:   ").Append(EscapeComment(meta.Path)).Append("\r\n");
            sb.Append("  Reason: ").Append(EscapeComment(reason)).Append("\r\n");
            sb.Append("-->\r\n");

            if (meta.IsTiff)
            {
                var info = new XElement("TiffInfo",
                    new XAttribute("byteOrder", meta.LittleEndian ? "II" : "MM"),
                    new XAttribute("bigTiff", meta.IsBigTiff),
                    new XAttribute("fileSize", meta.FileSize),
                    meta.Tags.Select(t => new XElement("Tag",
                        new XAttribute("ifd", t.Ifd),
                        new XAttribute("id", t.Id),
                        new XAttribute("name", TagNames.Get(t.Id)),
                        StripInvalidXmlChars(t.Preview ?? string.Empty))));
                sb.Append(info.ToString().Replace("\n", "\r\n").Replace("\r\r\n", "\r\n"));
            }
            return sb.ToString();
        }

        /// <summary>Re-indents well-formed XML; returns the input unchanged if it cannot be parsed.</summary>
        public static string PrettyPrint(string xml)
        {
            try
            {
                var doc = XDocument.Parse(xml, LoadOptions.None);
                var settings = new XmlWriterSettings
                {
                    Indent = true,
                    IndentChars = "  ",
                    NewLineChars = "\r\n",
                    NewLineHandling = NewLineHandling.Replace,
                    OmitXmlDeclaration = doc.Declaration == null,
                };
                var sw = new Utf8StringWriter();
                using (var writer = XmlWriter.Create(sw, settings))
                    doc.Save(writer);
                string result = sw.ToString();
                // XmlWriter writes encoding="utf-8" for the declaration; keep the original one instead.
                if (doc.Declaration != null)
                {
                    int end = result.IndexOf("?>", StringComparison.Ordinal);
                    if (end > 0) result = doc.Declaration + result.Substring(end + 2);
                }
                return result;
            }
            catch (XmlException ex)
            {
                return $"<!-- XML is not well-formed ({EscapeComment(ex.Message)}); shown unformatted. -->\r\n{xml}";
            }
        }

        private static string EscapeComment(string text) => (text ?? string.Empty).Replace("--", "- -");

        private static string StripInvalidXmlChars(string text) =>
            new string(text.Where(XmlConvert.IsXmlChar).ToArray());

        private sealed class Utf8StringWriter : StringWriter
        {
            public override Encoding Encoding => Encoding.UTF8;
        }
    }
}
