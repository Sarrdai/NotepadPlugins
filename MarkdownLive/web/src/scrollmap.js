// Bidirectional mapping between source lines and preview pixel offsets.
// The map is a monotonic polyline built from all "leaf" blocks carrying data-line.

export class ScrollMap {
  constructor(container) {
    this.container = container;
    this.points = null; // [{ line, y }]
    this.leaves = null; // [{ el, line, end }]
    this.totalLines = 1;
  }

  invalidate() {
    this.points = null;
    this.leaves = null;
  }

  setTotalLines(n) {
    this.totalLines = Math.max(1, n);
    this.points = null;
  }

  collectLeaves() {
    if (this.leaves) return this.leaves;
    const leaves = [];
    for (const el of this.container.querySelectorAll('[data-line]')) {
      if (el.querySelector('[data-line]')) continue;
      const line = Number(el.dataset.line);
      const end = Number(el.dataset.lineEnd ?? line + 1);
      if (Number.isFinite(line)) leaves.push({ el, line, end: Math.max(end, line + 1) });
    }
    this.leaves = leaves;
    return leaves;
  }

  build() {
    if (this.points) return this.points;
    const scrollY = window.scrollY;
    const points = [{ line: 0, y: 0 }];
    const push = (line, y) => {
      const last = points[points.length - 1];
      if (line > last.line && y >= last.y) points.push({ line, y });
    };
    for (const { el, line, end } of this.collectLeaves()) {
      const rect = el.getBoundingClientRect();
      if (rect.height === 0 && rect.width === 0) continue;
      push(line, rect.top + scrollY);
      push(end, rect.bottom + scrollY);
    }
    const docHeight = document.documentElement.scrollHeight;
    push(Math.max(this.totalLines, points[points.length - 1].line + 1), docHeight);
    this.points = points;
    return points;
  }

  lineToY(line) {
    const pts = this.build();
    if (line <= 0) return 0;
    let lo = 0;
    let hi = pts.length - 1;
    if (line >= pts[hi].line) return pts[hi].y;
    while (hi - lo > 1) {
      const mid = (lo + hi) >> 1;
      if (pts[mid].line <= line) lo = mid;
      else hi = mid;
    }
    const a = pts[lo];
    const b = pts[hi];
    return a.y + ((line - a.line) / (b.line - a.line)) * (b.y - a.y);
  }

  yToLine(y) {
    const pts = this.build();
    if (y <= 0) return 0;
    let lo = 0;
    let hi = pts.length - 1;
    if (y >= pts[hi].y) return pts[hi].line;
    while (hi - lo > 1) {
      const mid = (lo + hi) >> 1;
      if (pts[mid].y <= y) lo = mid;
      else hi = mid;
    }
    const a = pts[lo];
    const b = pts[hi];
    if (b.y === a.y) return a.line;
    return a.line + ((y - a.y) / (b.y - a.y)) * (b.line - a.line);
  }

  // Innermost block that contains the given source line.
  elementForLine(line) {
    let best = null;
    for (const leaf of this.collectLeaves()) {
      if (leaf.line > line) break;
      if (line < leaf.end) best = leaf.el;
    }
    return best;
  }
}
