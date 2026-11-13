using System;
using System.Collections.Generic;
using System.Linq;
using System.Text.RegularExpressions;
using System.Xml;
using System.Xml.Linq;

namespace TiffPlusPlus.Core
{
    /// <summary>
    /// Path to a value element (e.g. /ImageRemarks/ImageInfo/XrayInfo/Voltage), determined from a line
    /// in the displayed XML and evaluated on the XML of every slice of a stack.
    /// </summary>
    public sealed class XmlValuePath
    {
        private const string WrapperName = "__tiffpp_root";
        private static readonly Regex Declaration = new Regex(@"<\?xml[^>]*\?>", RegexOptions.Compiled);

        private readonly List<(XName Name, int Index)> _steps;

        private XmlValuePath(List<(XName, int)> steps) => _steps = steps;

        /// <summary>Full path, e.g. "/ImageRemarks/ImageInfo/Position[2]/Xo".</summary>
        public string FullName => "/" + string.Join("/", _steps.Select(s => s.Name.LocalName + (s.Index > 0 ? $"[{s.Index + 1}]" : "")));

        /// <summary>Last two path segments, used as compact column / chart title.</summary>
        public string ShortName => string.Join("/", _steps.Skip(Math.Max(0, _steps.Count - 2)).Select(s => s.Name.LocalName + (s.Index > 0 ? $"[{s.Index + 1}]" : "")));

        /// <summary>Finds the value element starting on <paramref name="line"/> (0-based); null if there is none.</summary>
        public static XmlValuePath FromLine(string xmlText, int line)
        {
            XElement root = Parse(xmlText, withLineInfo: true);
            if (root == null) return null;

            // Elements without child elements that start on the caret line (deepest/first one wins).
            XElement target = root.Descendants()
                .Where(e => !e.HasElements && ((IXmlLineInfo)e).LineNumber == line + 1)
                .FirstOrDefault();
            if (target == null) return null;

            var steps = new List<(XName, int)>();
            for (XElement e = target; e != null && e.Name.LocalName != WrapperName; e = e.Parent)
            {
                int index = e.ElementsBeforeSelf(e.Name).Count();
                steps.Insert(0, (e.Name, index));
            }
            return new XmlValuePath(steps);
        }

        /// <summary>Returns the element text, or null if the path does not exist in this XML.</summary>
        public string Evaluate(string xmlText)
        {
            XElement current = Parse(xmlText, withLineInfo: false);
            foreach (var (name, index) in _steps)
            {
                current = current?.Elements(name).ElementAtOrDefault(index);
                if (current == null) return null;
            }
            return current?.Value.Trim();
        }

        private static XElement Parse(string xmlText, bool withLineInfo)
        {
            if (string.IsNullOrEmpty(xmlText)) return null;
            // A wrapper element makes several root elements (multiple XML blocks) parseable;
            // it is placed on the first line, so line numbers stay unchanged.
            string wrapped = "<" + WrapperName + ">" + Declaration.Replace(xmlText, "") + "</" + WrapperName + ">";
            try
            {
                return XElement.Parse(wrapped, withLineInfo ? LoadOptions.SetLineInfo : LoadOptions.None);
            }
            catch (XmlException)
            {
                return null;
            }
        }
    }
}
