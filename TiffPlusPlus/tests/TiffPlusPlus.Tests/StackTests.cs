using System;
using System.IO;
using System.Linq;
using TiffPlusPlus.Core;
using Xunit;

namespace TiffPlusPlus.Tests
{
    public class StackTests : IDisposable
    {
        private readonly string _dir = Path.Combine(Path.GetTempPath(), "TiffPlusPlusStack_" + Guid.NewGuid().ToString("N"));

        public StackTests() => Directory.CreateDirectory(_dir);
        public void Dispose() => Directory.Delete(_dir, true);

        [Fact]
        public void NaturalSortOrdersNumbersNumerically()
        {
            var names = new[] { "img10.tif", "img2.tif", "IMG1.tif", "img02b.tif" };
            Assert.Equal(new[] { "IMG1.tif", "img2.tif", "img02b.tif", "img10.tif" },
                names.OrderBy(n => n, NaturalStringComparer.Instance).ToArray());
        }

        [Fact]
        public void LineDiffMarksChangedAndInsertedLines()
        {
            var previous = new[] { "<a>", "<v>1</v>", "<w>2</w>", "</a>" };
            var current = new[] { "<a>", "<v>5</v>", "<w>2</w>", "<x>3</x>", "</a>" };

            Assert.Equal(new[] { false, true, false, true, false }, LineDiff.ChangedLines(previous, current));
        }

        [Fact]
        public void ValuePathFromCaretLineIsEvaluatedOnOtherSlices()
        {
            const string slice1 = "<R>\r\n  <Pos>\r\n    <X>1.5</X>\r\n  </Pos>\r\n  <Pos>\r\n    <X>7</X>\r\n  </Pos>\r\n</R>";
            const string slice2 = "<R>\r\n  <Pos>\r\n    <X>2.5</X>\r\n  </Pos>\r\n  <Pos>\r\n    <X>8</X>\r\n  </Pos>\r\n</R>";

            var path = XmlValuePath.FromLine(slice1, 5);   // second <X>

            Assert.Equal("/R/Pos[2]/X", path.FullName);
            Assert.Equal("Pos[2]/X", path.ShortName);
            Assert.Equal("8", path.Evaluate(slice2));
            Assert.Null(XmlValuePath.FromLine(slice1, 1));   // <Pos> has child elements, no value
        }

        [Fact]
        public void ValuePathWorksWithDeclarationAndMultipleBlocks()
        {
            const string text = "<?xml version=\"1.0\"?>\r\n<!-- block 1 -->\r\n<A><V>3</V></A>\r\n<B><V>4</V></B>";

            var path = XmlValuePath.FromLine(text, 3);

            Assert.Equal("/B/V", path.FullName);
            Assert.Equal("4", path.Evaluate(text));
        }

        [Fact]
        public void StackSortsReadsXmlAndRoundTrips()
        {
            string[] files = { Write("s10.tiff", "<R><V>10</V><T>x</T></R>"), Write("s2.tiff", "<R><V>2</V><T>x</T></R>"), Write("s1.TIF", "<R><V>1</V><T>x</T></R>") };
            File.WriteAllText(Path.Combine(_dir, "notes.txt"), "ignored");

            var stack = TiffStack.Create("Test", TiffStack.FilesInFolder(_dir), index: 1);

            Assert.Equal(new[] { "s1.TIF", "s2.tiff", "s10.tiff" }, stack.Files.Select(Path.GetFileName).ToArray());
            Assert.StartsWith("<!-- Tiff++ stack \"Test\": slice 2 / 3 - s2.tiff -->\r\n<R>", stack.BuildDisplayText(1));
            Assert.Equal(new[] { false, true, false, false }, stack.ChangedLinesComparedToPrevious(1));   // <V> changed

            var restored = TiffStack.Deserialize(stack.Serialize());
            Assert.Equal(stack.Files, restored.Files);
            Assert.Equal(1, restored.Index);
            Assert.Equal("Test", restored.Name);
        }

        private string Write(string name, string xml)
        {
            string path = Path.Combine(_dir, name);
            File.WriteAllBytes(path, TiffBuilder.Classic(true, xmlTag: 50635, xmlType: 2, xml: xml));
            return path;
        }
    }
}
