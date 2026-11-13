using System;
using System.Collections.Generic;

namespace TiffPlusPlus.Core
{
    public static class LineDiff
    {
        private const long MaxLcsCells = 16_000_000;

        /// <summary>
        /// Marks every line of <paramref name="current"/> that is not part of the longest common
        /// subsequence with <paramref name="previous"/>, i.e. changed or inserted lines.
        /// </summary>
        public static bool[] ChangedLines(IReadOnlyList<string> previous, IReadOnlyList<string> current)
        {
            int n = previous.Count, m = current.Count;
            var changed = new bool[m];

            if ((long)n * m > MaxLcsCells)
            {
                // Huge documents: positional comparison is good enough and bounded in cost.
                for (int j = 0; j < m; j++) changed[j] = j >= n || previous[j] != current[j];
                return changed;
            }

            // lcs[i, j] = LCS length of previous[i..] and current[j..]
            var lcs = new int[(n + 1) * (m + 1)];
            int w = m + 1;
            for (int i = n - 1; i >= 0; i--)
                for (int j = m - 1; j >= 0; j--)
                    lcs[i * w + j] = previous[i] == current[j]
                        ? lcs[(i + 1) * w + j + 1] + 1
                        : Math.Max(lcs[(i + 1) * w + j], lcs[i * w + j + 1]);

            for (int j = 0; j < m; j++) changed[j] = true;
            for (int i = 0, j = 0; i < n && j < m;)
            {
                if (previous[i] == current[j]) { changed[j] = false; i++; j++; }
                else if (lcs[(i + 1) * w + j] >= lcs[i * w + j + 1]) i++;
                else j++;
            }
            return changed;
        }

        public static string[] SplitLines(string text) =>
            (text ?? string.Empty).Replace("\r\n", "\n").Split('\n');
    }
}
