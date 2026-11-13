using System;
using System.Collections.Generic;

namespace TiffPlusPlus.Viewer
{
    /// <summary>LRU cache of decoded slices with a memory budget (virtual stack: never the whole stack in RAM).</summary>
    internal sealed class PixelCache
    {
        private const int MaxEntries = 32;
        private readonly long _budgetBytes;
        private readonly LinkedList<KeyValuePair<string, PixelImage>> _lru = new LinkedList<KeyValuePair<string, PixelImage>>();
        private readonly Dictionary<string, LinkedListNode<KeyValuePair<string, PixelImage>>> _map =
            new Dictionary<string, LinkedListNode<KeyValuePair<string, PixelImage>>>(StringComparer.OrdinalIgnoreCase);
        private long _bytes;

        public PixelCache(long budgetBytes) => _budgetBytes = budgetBytes;

        public PixelImage Get(string path)
        {
            if (!_map.TryGetValue(path, out var node)) return null;
            _lru.Remove(node);
            _lru.AddFirst(node);
            return node.Value.Value;
        }

        public void Add(string path, PixelImage image)
        {
            if (_map.TryGetValue(path, out var existing)) Remove(existing);
            var node = _lru.AddFirst(new KeyValuePair<string, PixelImage>(path, image));
            _map[path] = node;
            _bytes += image.Data.LongLength;

            // Always keep the newest entry, even if it alone exceeds the budget.
            while (_lru.Count > 1 && (_bytes > _budgetBytes || _lru.Count > MaxEntries))
                Remove(_lru.Last);
        }

        public void Clear()
        {
            _lru.Clear();
            _map.Clear();
            _bytes = 0;
        }

        private void Remove(LinkedListNode<KeyValuePair<string, PixelImage>> node)
        {
            _lru.Remove(node);
            _map.Remove(node.Value.Key);
            _bytes -= node.Value.Value.Data.LongLength;
        }
    }

    internal static class Histogram
    {
        private const long MaxSamplesPerImage = 2_000_000;

        /// <summary>Adds (sub-sampled) gray values of the image to 16-bit bins; returns the sample count.</summary>
        public static long Accumulate(PixelImage image, long[] bins)
        {
            byte[] d = image.Data;
            long pixels = (long)image.Width * image.Height;
            long step = Math.Max(1, pixels / MaxSamplesPerImage);
            long n = 0;
            for (long p = 0; p < pixels; p += step, n++)
            {
                if (image.Format == "gray16")
                {
                    bins[d[2 * p] | d[2 * p + 1] << 8]++;
                }
                else
                {
                    long o = 4 * p;   // BGRA -> luma, scaled to the 0..255 range used by the viewer
                    bins[(d[o] * 29 + d[o + 1] * 150 + d[o + 2] * 77) >> 8]++;
                }
            }
            return n;
        }

        public static (int Lo, int Hi) Percentiles(long[] bins, long total, double clipPercent)
        {
            double clip = total * clipPercent / 100;
            int lo = 0, hi = bins.Length - 1;
            long acc = 0;
            for (int v = 0; v < bins.Length; v++) { acc += bins[v]; if (acc > clip) { lo = v; break; } }
            acc = 0;
            for (int v = bins.Length - 1; v >= 0; v--) { acc += bins[v]; if (acc > clip) { hi = v; break; } }
            return hi > lo ? (lo, hi) : (lo, Math.Min(lo + 1, bins.Length - 1));
        }
    }
}
