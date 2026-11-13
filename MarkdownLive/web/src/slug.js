// Heading slug generation (GitHub flavoured).
// IMPORTANT: this algorithm is mirrored 1:1 in src/md_text.cpp (slugify) so that
// the generated table of contents links match the anchors in the preview.

function lowerCodePoint(c) {
  if (c >= 0x41 && c <= 0x5a) return c + 0x20;
  if (c >= 0xc0 && c <= 0xde && c !== 0xd7) return c + 0x20;
  if ((c >= 0x100 && c <= 0x137) || (c >= 0x14a && c <= 0x177)) return c % 2 === 0 ? c + 1 : c;
  if ((c >= 0x139 && c <= 0x148) || (c >= 0x179 && c <= 0x17e)) return c % 2 === 1 ? c + 1 : c;
  if (c === 0x178) return 0xff;
  if (c >= 0x391 && c <= 0x3a9 && c !== 0x3a2) return c + 0x20;
  if (c >= 0x410 && c <= 0x42f) return c + 0x20;
  if (c >= 0x400 && c <= 0x40f) return c + 0x50;
  return c;
}

function isDropped(c) {
  if (c < 0x80) {
    const keep =
      (c >= 0x30 && c <= 0x39) || (c >= 0x61 && c <= 0x7a) || c === 0x5f || c === 0x2d || c === 0x20;
    return !keep;
  }
  if (c <= 0xbf || c === 0xd7 || c === 0xf7) return true;
  if (c >= 0x2000 && c <= 0x2bff) return true;
  if (c >= 0x3000 && c <= 0x303f) return true;
  if (c >= 0xfe00 && c <= 0xfe0f) return true;
  if (c >= 0x1f000 && c <= 0x1faff) return true;
  return false;
}

export function slugify(text) {
  let out = '';
  for (const ch of text.trim()) {
    const c = lowerCodePoint(ch.codePointAt(0));
    if (isDropped(c)) continue;
    out += c === 0x20 ? '-' : String.fromCodePoint(c);
  }
  return out;
}

// Returns a function producing unique slugs: "a", "a-1", "a-2", ...
export function createSlugger() {
  const seen = new Map();
  return (text) => {
    const base = slugify(text);
    const n = seen.get(base) ?? 0;
    seen.set(base, n + 1);
    return n === 0 ? base : `${base}-${n}`;
  };
}
