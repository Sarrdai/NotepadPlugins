// markdown-it configuration: GFM-style rendering with source line annotations.
import MarkdownIt from 'markdown-it';
import footnote from 'markdown-it-footnote';
import { full as emoji } from 'markdown-it-emoji';
import deflist from 'markdown-it-deflist';
import sub from 'markdown-it-sub';
import sup from 'markdown-it-sup';
import mark from 'markdown-it-mark';
import frontMatter from 'markdown-it-front-matter';
import alerts from 'markdown-it-github-alerts';
import katex from '@vscode/markdown-it-katex';
import hljs from 'highlight.js';
import { createSlugger } from './slug.js';

const katexPlugin = katex.default ?? katex;

export function hashString(s) {
  let h = 5381;
  for (let i = 0; i < s.length; i++) h = ((h << 5) + h + s.charCodeAt(i)) | 0;
  return (h >>> 0).toString(36);
}

function lineAttrs(token) {
  if (!token.map) return '';
  return ` data-line="${token.map[0]}" data-line-end="${token.map[1]}"`;
}

// Adds data-line / data-line-end to every block token that knows its source range.
function sourceLinePlugin(md) {
  md.core.ruler.push('source_lines', (state) => {
    for (const token of state.tokens) {
      if (token.map && token.nesting !== -1 && token.type !== 'inline') {
        token.attrSet('data-line', String(token.map[0]));
        token.attrSet('data-line-end', String(token.map[1]));
      }
    }
  });
}

// GitHub-compatible heading ids.
function headingIdPlugin(md) {
  md.core.ruler.push('heading_ids', (state) => {
    const slug = createSlugger();
    const tokens = state.tokens;
    for (let i = 0; i < tokens.length - 1; i++) {
      if (tokens[i].type !== 'heading_open') continue;
      const inline = tokens[i + 1];
      const text = (inline.children ?? [])
        .filter((t) => t.type === 'text' || t.type === 'code_inline')
        .map((t) => t.content)
        .join('');
      tokens[i].attrSet('id', slug(text));
    }
  });
}

// "- [ ] item" / "- [x] item" -> clickable checkboxes that know their source line.
function taskListPlugin(md) {
  md.core.ruler.after('inline', 'task_lists', (state) => {
    const tokens = state.tokens;
    for (let i = 2; i < tokens.length; i++) {
      const inline = tokens[i];
      if (inline.type !== 'inline' || tokens[i - 1].type !== 'paragraph_open') continue;
      const item = tokens[i - 2];
      if (item.type !== 'list_item_open') continue;
      const first = inline.children?.[0];
      if (!first || first.type !== 'text') continue;
      const m = /^\[([ xX])\](\s+|$)/.exec(first.content);
      if (!m) continue;
      first.content = first.content.slice(m[0].length);
      const checked = m[1] !== ' ';
      const box = new state.Token('html_inline', '', 0);
      const line = item.map ? item.map[0] : -1;
      box.content = `<input type="checkbox" class="task-checkbox" data-task-line="${line}"${
        checked ? ' checked' : ''
      }> `;
      inline.children.unshift(box);
      item.attrJoin('class', 'task-list-item');
    }
  });
}

export function createMarkdown() {
  const md = new MarkdownIt({ html: true, linkify: true, breaks: false, typographer: false });

  md.use(footnote)
    .use(emoji)
    .use(deflist)
    .use(sub)
    .use(sup)
    .use(mark)
    .use(alerts)
    .use(frontMatter, () => {})
    .use(katexPlugin, { output: 'html', throwOnError: false })
    .use(taskListPlugin)
    .use(headingIdPlugin)
    .use(sourceLinePlugin);

  const escape = md.utils.escapeHtml;

  md.renderer.rules.fence = (tokens, idx) => {
    const token = tokens[idx];
    const info = token.info ? md.utils.unescapeAll(token.info).trim() : '';
    const lang = info.split(/\s+/)[0] ?? '';
    const attrs = lineAttrs(token);

    if (lang.toLowerCase() === 'mermaid') {
      return (
        `<div class="mermaid-block"${attrs} data-hash="${hashString(token.content)}">` +
        `<pre class="mermaid-src">${escape(token.content)}</pre></div>\n`
      );
    }

    let body;
    let langClass = '';
    if (lang && hljs.getLanguage(lang)) {
      try {
        body = hljs.highlight(token.content, { language: lang, ignoreIllegals: true }).value;
        langClass = ` language-${escape(lang)}`;
      } catch {
        body = escape(token.content);
      }
    } else {
      body = escape(token.content);
    }
    const label = lang ? `<span class="code-lang">${escape(lang)}</span>` : '';
    return (
      `<div class="code-block"${attrs}>${label}` +
      `<button type="button" class="copy-btn" title="Copy code">Copy</button>` +
      `<pre><code class="hljs${langClass}">${body}</code></pre></div>\n`
    );
  };

  md.renderer.rules.front_matter = (tokens, idx) => {
    const token = tokens[idx];
    return (
      `<details class="front-matter"${lineAttrs(token)}><summary>Front matter</summary>` +
      `<pre><code>${escape(token.meta ?? '')}</code></pre></details>\n`
    );
  };

  // Wrap tables so wide tables scroll horizontally instead of breaking the layout.
  md.renderer.rules.table_open = (tokens, idx, options, env, self) =>
    `<div class="table-wrap"><table${self.renderAttrs(tokens[idx])}>\n`;
  md.renderer.rules.table_close = () => '</table></div>\n';

  return md;
}
