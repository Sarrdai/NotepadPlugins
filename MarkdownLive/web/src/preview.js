// Preview page running inside WebView2. Talks to the native plugin via
// window.chrome.webview messages (see src/preview_panel.cpp for the host side).
import DOMPurify from 'dompurify';
import morphdom from 'morphdom';
import 'katex/dist/katex.min.css';
import './preview.css';
import { createMarkdown } from './markdown.js';
import { ScrollMap } from './scrollmap.js';

const FILE_HOST = 'https://mdlive.file/';
const KATEX_CDN = `https://cdn.jsdelivr.net/npm/katex@${__KATEX_VERSION__}/dist/fonts/`;

const webview = window.chrome?.webview;
const outbox = []; // only used when running outside WebView2 (tests)
// Messages to the host are sent as plain strings: "type\nfield\nfield..." (the last
// field may contain line breaks). Keep in sync with PreviewPanel::onWebMessage.
const wireFields = {
  ready: [],
  scroll: ['line', 'atEnd'],
  toggleTask: ['line'],
  gotoLine: ['line'],
  togglePin: [],
  openLink: ['href'],
  exportReady: ['kind', 'html'],
};
function encodeMessage(msg) {
  const fields = wireFields[msg.type].map((name) => {
    const value = msg[name];
    return typeof value === 'boolean' ? (value ? '1' : '0') : String(value ?? '');
  });
  return [msg.type, ...fields].join('\n');
}
const post = (msg) => (webview ? webview.postMessage(encodeMessage(msg)) : outbox.push(msg));

const md = createMarkdown();
const content = document.getElementById('content');
const placeholder = document.getElementById('placeholder');
const pinButton = document.getElementById('pin');
const scrollMap = new ScrollMap(content);

const state = {
  baseDir: '',
  fileName: '',
  sync: true,
  dark: false,
  cursorLine: -1,
  hasDocument: false,
  pinned: false,
};

// ---------------------------------------------------------------- helpers

const escapeHtml = (s) =>
  s.replace(/[&<>"]/g, (c) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;' })[c]);

function isExternalUrl(src) {
  return /^(https?:|data:|blob:|\/\/)/i.test(src);
}

// Resolves an image path from the document against the document folder and
// returns a URL served by the native host.
function localFileUrl(src) {
  let path = src.split(/[?#]/)[0];
  try {
    path = decodeURIComponent(path);
  } catch {
    /* keep raw path */
  }
  if (/^file:\/\//i.test(path)) path = path.replace(/^file:\/\/\/?/i, '');
  path = path.replace(/\\/g, '/');
  const isAbsolute = /^[a-zA-Z]:\//.test(path) || path.startsWith('//');
  if (!isAbsolute) {
    if (!state.baseDir) return null;
    const base = state.baseDir.replace(/\\/g, '/').replace(/\/+$/, '');
    path = path.startsWith('/') ? base.slice(0, 2) + path : `${base}/${path}`;
  }
  const prefix = path.startsWith('//') ? '//' : '';
  const parts = [];
  for (const part of path.slice(prefix.length).split('/')) {
    if (part === '' || part === '.') continue;
    if (part === '..') {
      if (parts.length > 1) parts.pop();
    } else parts.push(part);
  }
  return `${FILE_HOST}?p=${encodeURIComponent(prefix + parts.join('/'))}`;
}

function rewriteImages(root) {
  for (const img of root.querySelectorAll('img[src]')) {
    const src = img.getAttribute('src');
    if (!src || isExternalUrl(src)) continue;
    const url = localFileUrl(src);
    if (!url) continue;
    img.setAttribute('data-orig', src);
    img.setAttribute('src', url);
  }
}

// ---------------------------------------------------------------- mermaid

let mermaidModule = null;
let mermaidSeq = 0;
const mermaidCache = new Map();
const mermaidSources = new WeakMap(); // block element -> diagram source
let diagramsDone = Promise.resolve();

async function renderMermaidBlocks() {
  const blocks = [...content.querySelectorAll('.mermaid-block:not([data-rendered])')];
  if (blocks.length === 0) return;
  mermaidModule ??= import('mermaid').then((m) => m.default);
  const mermaid = await mermaidModule;
  const theme = state.dark ? 'dark' : 'default';
  mermaid.initialize({ startOnLoad: false, theme, securityLevel: 'strict' });

  for (const el of blocks) {
    if (!el.isConnected || el.dataset.rendered) continue;
    const hash = el.dataset.hash;
    const source = el.querySelector('.mermaid-src')?.textContent ?? '';
    const key = `${theme}:${hash}`;
    let svg = mermaidCache.get(key);
    if (svg === undefined) {
      const id = `mmd-${++mermaidSeq}`;
      try {
        ({ svg } = await mermaid.render(id, source));
      } catch (err) {
        svg = `<pre class="mermaid-error">${escapeHtml(String(err?.message ?? err))}</pre>`;
        document.getElementById(id)?.remove();
        document.getElementById(`d${id}`)?.remove();
      }
      if (mermaidCache.size > 200) mermaidCache.clear();
      mermaidCache.set(key, svg);
    }
    if (el.isConnected && el.dataset.hash === hash) {
      mermaidSources.set(el, source);
      el.innerHTML = svg;
      el.dataset.rendered = theme;
    }
  }
  scrollMap.invalidate();
}

// ---------------------------------------------------------------- scrolling

let suppressScrollUntil = 0;
let scrollFrame = 0;

function scrollToY(y) {
  suppressScrollUntil = performance.now() + 250;
  window.scrollTo(0, Math.max(0, y));
}

function syncToEditorLine(line, atEnd) {
  if (!state.sync || !state.hasDocument) return;
  const max = document.documentElement.scrollHeight - window.innerHeight;
  scrollToY(atEnd ? max : Math.min(max, scrollMap.lineToY(line)));
}

window.addEventListener(
  'scroll',
  () => {
    if (!state.sync || !state.hasDocument || performance.now() < suppressScrollUntil) return;
    if (scrollFrame) return;
    scrollFrame = requestAnimationFrame(() => {
      scrollFrame = 0;
      const max = document.documentElement.scrollHeight - window.innerHeight;
      const atEnd = max > 0 && window.scrollY >= max - 1;
      post({ type: 'scroll', line: scrollMap.yToLine(window.scrollY), atEnd });
    });
  },
  { passive: true },
);

new ResizeObserver(() => scrollMap.invalidate()).observe(content);
window.addEventListener('resize', () => scrollMap.invalidate());

function highlightCursor(line, reveal) {
  state.cursorLine = line;
  content.querySelector('.cursor-block')?.classList.remove('cursor-block');
  if (line < 0) return;
  const el = scrollMap.elementForLine(line);
  if (!el) return;
  el.classList.add('cursor-block');
  if (reveal && state.sync) {
    const rect = el.getBoundingClientRect();
    if (rect.bottom < 0 || rect.top > window.innerHeight) {
      suppressScrollUntil = performance.now() + 250;
      el.scrollIntoView({ block: 'center' });
    }
  }
}

// ---------------------------------------------------------------- rendering

function showPlaceholder(text) {
  state.hasDocument = false;
  pinButton.hidden = !state.pinned;
  content.replaceChildren();
  placeholder.textContent = text;
  placeholder.hidden = false;
  scrollMap.invalidate();
}

function render(msg) {
  state.baseDir = msg.baseDir ?? '';
  state.fileName = msg.fileName ?? '';
  state.hasDocument = true;
  pinButton.hidden = false;
  placeholder.hidden = true;

  const text = msg.text ?? '';
  const html = md.render(text);
  const fragment = DOMPurify.sanitize(html, {
    RETURN_DOM_FRAGMENT: true,
    SANITIZE_DOM: false, // keep heading ids such as "links" or "title"
    ADD_ATTR: ['target'],
  });
  rewriteImages(fragment);

  const next = content.cloneNode(false);
  next.appendChild(fragment);

  morphdom(content, next, {
    onBeforeElUpdated(from, to) {
      if (from.isEqualNode(to)) return false;
      if (
        from.classList.contains('mermaid-block') &&
        from.dataset.rendered &&
        from.dataset.hash === to.dataset.hash
      ) {
        from.dataset.line = to.dataset.line;
        from.dataset.lineEnd = to.dataset.lineEnd;
        return false;
      }
      if (from.tagName === 'DETAILS' && from.open) to.setAttribute('open', '');
      return true;
    },
  });

  let lines = 1;
  for (let i = text.indexOf('\n'); i !== -1; i = text.indexOf('\n', i + 1)) lines++;
  scrollMap.invalidate();
  scrollMap.setTotalLines(lines);

  diagramsDone = renderMermaidBlocks().catch((err) => console.error(err));

  if (msg.reset) syncToEditorLine(msg.line ?? 0, msg.atEnd);
  highlightCursor(msg.cursor ?? -1, !msg.reset);
}

// ---------------------------------------------------------------- export

function imagesSettled() {
  const pending = [...content.querySelectorAll('img')].filter((img) => !img.complete);
  if (pending.length === 0) return Promise.resolve();
  const all = Promise.all(
    pending.map(
      (img) =>
        new Promise((resolve) => {
          img.addEventListener('load', resolve, { once: true });
          img.addEventListener('error', resolve, { once: true });
        }),
    ),
  );
  return Promise.race([all, new Promise((resolve) => setTimeout(resolve, 5000))]);
}

function collectCss(hasMath) {
  const out = [];
  for (const sheet of document.styleSheets) {
    let rules;
    try {
      rules = sheet.cssRules;
    } catch {
      continue;
    }
    for (const rule of rules) {
      let text = rule.cssText;
      const isKatex = /katex/i.test(text);
      if (isKatex && !hasMath) continue;
      if (isKatex && text.startsWith('@font-face')) {
        // keep only the woff2 source and load it from the CDN
        const m = /url\(["']?(?:[^)"']*\/)?(KaTeX_[\w-]+?)(?:-[A-Z0-9]{8})?\.woff2["']?\)/.exec(text);
        if (!m) continue;
        text = text.replace(/src:[^;]+;/, `src: url("${KATEX_CDN}${m[1]}.woff2") format("woff2");`);
      }
      out.push(text);
    }
  }
  return out.join('\n');
}

function buildStandaloneHtml() {
  const clone = content.cloneNode(true);
  clone.querySelectorAll('.copy-btn').forEach((el) => el.remove());
  clone.querySelectorAll('.cursor-block').forEach((el) => el.classList.remove('cursor-block'));
  for (const el of clone.querySelectorAll('[data-line], [data-task-line], [data-orig], [class=""]')) {
    if (el.hasAttribute('data-orig')) el.setAttribute('src', el.getAttribute('data-orig'));
    if (el.hasAttribute('data-task-line')) el.setAttribute('disabled', '');
    for (const name of ['data-line', 'data-line-end', 'data-task-line', 'data-orig']) el.removeAttribute(name);
    if (el.getAttribute('class') === '') el.removeAttribute('class');
  }
  const title = escapeHtml(state.fileName.replace(/\.[^.]+$/, '') || 'Markdown');
  const css = collectCss(Boolean(clone.querySelector('.katex')));
  return (
    `<!DOCTYPE html>\n<html lang="en" data-theme="${state.dark ? 'dark' : 'light'}">\n<head>\n` +
    `<meta charset="utf-8">\n<meta name="viewport" content="width=device-width, initial-scale=1">\n` +
    `<meta name="generator" content="MarkdownLive for Notepad++">\n<title>${title}</title>\n` +
    `<style>\n${css}\n</style>\n</head>\n<body>\n<main id="content" class="markdown-body">\n` +
    `${clone.innerHTML}\n</main>\n</body>\n</html>\n`
  );
}

async function handleExport(kind) {
  await diagramsDone;
  await imagesSettled();
  if (kind === 'html') post({ type: 'exportReady', kind, html: buildStandaloneHtml() });
  else post({ type: 'exportReady', kind });
}

// ---------------------------------------------------------------- host messages

function setTheme(dark) {
  if (state.dark === dark && document.documentElement.dataset.theme) return;
  state.dark = dark;
  document.documentElement.dataset.theme = dark ? 'dark' : 'light';
  // Diagrams are themed by mermaid itself: restore their source and render again.
  for (const el of content.querySelectorAll('.mermaid-block[data-rendered]')) {
    const pre = document.createElement('pre');
    pre.className = 'mermaid-src';
    pre.textContent = mermaidSources.get(el) ?? '';
    el.replaceChildren(pre);
    el.removeAttribute('data-rendered');
  }
  diagramsDone = diagramsDone.then(renderMermaidBlocks).catch((err) => console.error(err));
}

function setPinned(pinned, name) {
  state.pinned = pinned;
  pinButton.setAttribute('aria-pressed', String(pinned));
  pinButton.title = pinned
    ? `Pinned to ${name || 'this document'} – click to follow the current document again`
    : 'Pin the preview to this document';
  if (pinned) pinButton.hidden = false;
}

async function handle(msg) {
  switch (msg.type) {
    case 'render':
      render(msg);
      break;
    case 'scroll':
      syncToEditorLine(msg.line, msg.atEnd);
      break;
    case 'cursor':
      highlightCursor(msg.line, false);
      break;
    case 'theme':
      setTheme(Boolean(msg.dark));
      break;
    case 'sync':
      state.sync = Boolean(msg.enabled);
      break;
    case 'pin':
      setPinned(Boolean(msg.pinned), msg.name ?? '');
      break;
    case 'placeholder':
      showPlaceholder(msg.text ?? '');
      break;
    case 'export':
      await handleExport(msg.kind);
      break;
  }
}

let queue = Promise.resolve();
function enqueue(msg) {
  queue = queue.then(() => handle(msg)).catch((err) => console.error(err));
  return queue;
}

// ---------------------------------------------------------------- user interaction

document.addEventListener('click', (event) => {
  const target = event.target instanceof Element ? event.target : null;
  if (!target) return;

  if (target.closest('#pin')) {
    post({ type: 'togglePin' });
    return;
  }

  const copy = target.closest('.copy-btn');
  if (copy) {
    const code = copy.parentElement.querySelector('pre > code')?.textContent ?? '';
    navigator.clipboard?.writeText(code).then(() => {
      copy.textContent = 'Copied';
      setTimeout(() => (copy.textContent = 'Copy'), 1200);
    });
    return;
  }

  if (target.matches('input.task-checkbox')) {
    // the checkbox state always comes from the source text
    event.preventDefault();
    const line = Number(target.dataset.taskLine);
    if (line >= 0) post({ type: 'toggleTask', line });
    return;
  }

  const link = target.closest('a[href]');
  if (link) {
    event.preventDefault();
    const href = link.getAttribute('href');
    if (href.startsWith('#')) {
      let id = href.slice(1);
      try {
        id = decodeURIComponent(id);
      } catch {
        /* use raw id */
      }
      const el = document.getElementById(id) ?? document.getElementsByName(id)[0];
      if (el) {
        el.scrollIntoView({ block: 'start' });
      }
    } else {
      post({ type: 'openLink', href });
    }
  }
});

document.addEventListener('dblclick', (event) => {
  if (!state.hasDocument) return;
  if (event.target instanceof Element && event.target.closest('a, input, button')) return;
  post({ type: 'gotoLine', line: Math.floor(scrollMap.yToLine(event.pageY)) });
});

if (webview) webview.addEventListener('message', (event) => enqueue(event.data));

// Test hook (also handy for debugging in the DevTools console).
window.__mdlive = { enqueue, outbox, state, scrollMap, encodeMessage };

setTheme(false);
post({ type: 'ready' });
