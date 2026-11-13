// End-to-end test of the preview page in headless Chromium (outside WebView2).
// Usage: npm run build && npm test
import { createServer } from 'node:http';
import { readFile } from 'node:fs/promises';
import { extname, join, normalize } from 'node:path';
import { fileURLToPath } from 'node:url';
import assert from 'node:assert/strict';
import { createRequire } from 'node:module';

// resolved via require() so a globally installed Playwright (NODE_PATH) works too
const { chromium } = createRequire(import.meta.url)('playwright');

const root = fileURLToPath(new URL('../dist/', import.meta.url));
const sample = await readFile(new URL('./sample.md', import.meta.url), 'utf8');
const types = { '.html': 'text/html', '.js': 'text/javascript', '.css': 'text/css', '.woff2': 'font/woff2' };

const server = createServer(async (req, res) => {
  try {
    const path = normalize(decodeURIComponent(new URL(req.url, 'http://x').pathname));
    const data = await readFile(join(root, path));
    res.writeHead(200, { 'Content-Type': types[extname(path)] ?? 'application/octet-stream' });
    res.end(data);
  } catch {
    res.writeHead(404).end();
  }
});
await new Promise((resolve) => server.listen(0, '127.0.0.1', resolve));
const url = `http://127.0.0.1:${server.address().port}/preview.html`;

const browser = await chromium.launch();
const page = await browser.newPage({ viewport: { width: 760, height: 600 } });
const errors = [];
page.on('pageerror', (e) => errors.push(String(e)));
// The sample contains <img onerror=...> on purpose: newer Chromium versions log that the page CSP
// blocked such a handler. That is the desired outcome, so only this message is tolerated.
const tolerated = /Failed to load resource|Executing inline event handler violates/;
page.on('console', (m) => m.type() === 'error' && !tolerated.test(m.text()) && errors.push(m.text()));
await page.route('https://mdlive.file/**', (route) => route.fulfill({ status: 404 }));
await page.route('https://example.com/**', (route) => route.fulfill({ status: 404 }));

await page.goto(url);
await page.waitForFunction(() => window.__mdlive?.outbox.some((m) => m.type === 'ready'));
const send = (msg) => page.evaluate((m) => window.__mdlive.enqueue(m), msg);
const outbox = () => page.evaluate(() => window.__mdlive.outbox.splice(0));

await send({ type: 'render', text: sample, baseDir: 'C:\\Users\\me\\My Docs', fileName: 'sample.md', reset: true, line: 0, cursor: 4 });

// --- structure
assert.equal(await page.locator('h1#markdownlive-sample').count(), 1, 'h1 id');
assert.equal(await page.locator('h2#links').count(), 1, 'first duplicate heading id');
assert.equal(await page.locator('h2#links-1').count(), 1, 'second duplicate heading id');
assert.equal(await page.locator('input.task-checkbox').count(), 2, 'task checkboxes');
assert.equal(await page.locator('input.task-checkbox:checked').count(), 1, 'checked task');
assert.equal(await page.locator('.markdown-alert-note').count(), 1, 'alert');
assert.equal(await page.locator('table td').count(), 3, 'table');
assert.ok((await page.locator('.code-block .hljs-keyword').count()) > 0, 'syntax highlighting');
assert.equal(await page.locator('.katex').count(), 2, 'math');
assert.equal(await page.locator('mark').count(), 1, 'mark');
assert.equal(await page.locator('details.front-matter').count(), 1, 'front matter');
assert.equal(await page.locator('.footnotes').count(), 1, 'footnotes');
assert.equal(await page.locator('dl dt').count(), 1, 'definition list');
assert.equal(await page.locator('h1.cursor-block').count(), 1, 'cursor block highlight');

// --- sanitizing
assert.equal(await page.evaluate(() => window.__pwned), undefined, 'scripts must not run');
assert.equal(await page.locator('#content script').count(), 0, 'script removed');
assert.equal(await page.locator('#content [onerror]').count(), 0, 'event handlers removed');

// --- images
const local = page.locator('img[alt="local"]');
assert.equal(await local.getAttribute('src'), 'https://mdlive.file/?p=' + encodeURIComponent('C:/Users/me/My Docs/images/pic.png'));
assert.equal(await local.getAttribute('data-orig'), 'images/pic.png');
assert.equal(await page.locator('img[alt="remote"]').getAttribute('src'), 'https://example.com/x.png');

// --- mermaid (lazy loaded)
await page.waitForSelector('.mermaid-block[data-rendered="default"] svg', { timeout: 30000 });

// --- interactions
await outbox();
await page.locator('input.task-checkbox').first().click();
await page.locator('a[href="docs/readme.md"]').click();
await page.locator('table td').first().dblclick();
let out = await outbox();
const taskLine = sample.split('\n').indexOf('- [ ] open task');
assert.deepEqual(out.find((m) => m.type === 'toggleTask'), { type: 'toggleTask', line: taskLine });
assert.deepEqual(out.find((m) => m.type === 'openLink'), { type: 'openLink', href: 'docs/readme.md' });
const tableLine = sample.split('\n').indexOf('| a    |   b    |     c |');
assert.equal(out.find((m) => m.type === 'gotoLine')?.line, tableLine, 'double click maps to source line');

// --- scroll sync: editor -> preview
const mermaidLine = sample.split('\n').indexOf('```mermaid');
await send({ type: 'scroll', line: mermaidLine });
const top = await page.locator('.mermaid-block').evaluate((el) => el.getBoundingClientRect().top);
assert.ok(Math.abs(top) < 3, `mermaid block should be at the top (got ${top})`);

// --- scroll sync: preview -> editor
await page.waitForTimeout(350);
await outbox();
const pyLine = sample.split('\n').indexOf('```python');
await page.locator('.code-block').evaluate((el) => window.scrollTo(0, el.getBoundingClientRect().top + window.scrollY));
await page.waitForFunction(() => window.__mdlive.outbox.some((m) => m.type === 'scroll'));
out = await outbox();
const reported = out.filter((m) => m.type === 'scroll').pop().line;
assert.ok(Math.abs(reported - pyLine) < 0.5, `preview scroll should report line ${pyLine}, got ${reported}`);

// --- incremental update keeps rendered diagrams and existing nodes
await page.evaluate(() => (document.querySelector('.mermaid-block svg').dataset.keep = '1'));
await send({ type: 'render', text: sample.replace('Some **bold**', 'Changed **bold**'), baseDir: 'C:\\Users\\me\\My Docs', fileName: 'sample.md', cursor: 6 });
assert.equal(await page.locator('.mermaid-block svg[data-keep="1"]').count(), 1, 'diagram not re-rendered');
assert.ok((await page.locator('#content').innerText()).includes('Changed bold'));

// --- dark theme re-renders diagrams
await send({ type: 'theme', dark: true });
await page.waitForSelector('.mermaid-block[data-rendered="dark"] svg', { timeout: 30000 });
assert.equal(await page.evaluate(() => document.documentElement.dataset.theme), 'dark');
if (process.env.SCREENSHOT_DIR) await page.screenshot({ path: join(process.env.SCREENSHOT_DIR, 'dark.png') });
await send({ type: 'theme', dark: false });
await page.waitForSelector('.mermaid-block[data-rendered="default"] svg', { timeout: 30000 });
if (process.env.SCREENSHOT_DIR) {
  await page.evaluate(() => window.scrollTo(0, 0));
  await page.screenshot({ path: join(process.env.SCREENSHOT_DIR, 'light.png'), fullPage: true });
}

// --- export
await outbox();
await send({ type: 'export', kind: 'html' });
out = await outbox();
const html = out.find((m) => m.type === 'exportReady')?.html ?? '';
assert.ok(html.startsWith('<!DOCTYPE html>'), 'standalone document');
assert.ok(html.includes('<title>sample</title>'));
assert.ok(html.includes('src="images/pic.png"'), 'original image path restored');
assert.ok(!html.includes('data-line="'), 'source annotations removed');
assert.ok(!html.includes('copy-btn"'), 'copy buttons removed');
assert.ok(html.includes('cdn.jsdelivr.net/npm/katex@'), 'katex fonts via CDN');
assert.ok(html.includes('<svg'), 'diagram embedded');

// --- wire format used towards the native host
assert.equal(
  await page.evaluate(() => window.__mdlive.encodeMessage({ type: 'scroll', line: 12.5, atEnd: true })),
  'scroll\n12.5\n1',
);
assert.equal(
  await page.evaluate(() => window.__mdlive.encodeMessage({ type: 'exportReady', kind: 'html', html: 'a\nb' })),
  'exportReady\nhtml\na\nb',
);
assert.equal(await page.evaluate(() => window.__mdlive.encodeMessage({ type: 'ready' })), 'ready');

// --- task checkboxes are driven by the source text only
assert.equal(await page.locator('input.task-checkbox:checked').count(), 1, 'click must not change the state');

// --- pin button
await outbox();
await page.locator('#pin').click();
assert.deepEqual((await outbox()).find((m) => m.type === 'togglePin'), { type: 'togglePin' });
assert.equal(await page.evaluate(() => window.__mdlive.encodeMessage({ type: 'togglePin' })), 'togglePin');
await send({ type: 'pin', pinned: true, name: 'guide.md' });
assert.equal(await page.locator('#pin').getAttribute('aria-pressed'), 'true');
assert.ok((await page.locator('#pin').getAttribute('title')).includes('guide.md'));
await send({ type: 'pin', pinned: false, name: '' });
assert.equal(await page.locator('#pin').getAttribute('aria-pressed'), 'false');

// --- placeholder
await send({ type: 'placeholder', text: 'Not a Markdown file' });
assert.equal(await page.locator('#placeholder').innerText(), 'Not a Markdown file');
assert.equal(await page.locator('#content > *').count(), 0);
assert.equal(await page.locator('#pin').isVisible(), false, 'nothing to pin');

assert.deepEqual(errors, [], 'no page errors');
await browser.close();
server.close();
console.log('preview tests passed');
