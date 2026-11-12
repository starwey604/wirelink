#!/usr/bin/env node
// SPDX-License-Identifier: Apache-2.0
import assert from 'node:assert/strict';
import { existsSync, readFileSync, readdirSync, statSync } from 'node:fs';
import { dirname, extname, join, relative, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const root = resolve(dirname(fileURLToPath(import.meta.url)), '../..');
const content = join(root, 'site/src/content/docs');
function files(directory, extensions) {
  return readdirSync(directory).flatMap((name) => {
    const path = join(directory, name);
    return statSync(path).isDirectory() ? files(path, extensions)
      : extensions.includes(extname(path)) ? [path] : [];
  });
}
const sitePages = files(content, ['.mdx']);
const codeBlocks = (path) => [...readFileSync(path, 'utf8').matchAll(/```(\w*)\n([\s\S]*?)\n```/g)]
  .map((m) => [m[1], m[2]]);
const route = (page) => '/wirelink/' + relative(content, page)
  .replace(/\\/g, '/').replace(/(?:^|\/)index\.mdx$/, '/').replace(/\.mdx$/, '/')
  .replace(/^\//, '');
const routes = new Map(sitePages.map((page) => [route(page), page]));
const en = sitePages.filter((page) => !relative(content, page).startsWith('zh-cn/'));
for (const page of en) {
  const translated = join(content, 'zh-cn', relative(content, page));
  assert(existsSync(translated), `Missing Chinese topic: ${page}`);
  assert.deepEqual(codeBlocks(page), codeBlocks(translated), `${page}: bilingual code blocks differ`);
}
for (const page of sitePages.filter((p) => relative(content, p).startsWith('zh-cn/'))) {
  assert(existsSync(join(content, relative(content, page).slice(6))), `Missing English topic: ${page}`);
}
function prose(text) {
  return text.replace(/```[\s\S]*?```/g, '');
}
function anchors(path) {
  const text = prose(readFileSync(path, 'utf8'));
  const ids = new Set([...text.matchAll(/\bid=["']([^"']+)["']/g)].map((m) => m[1]));
  const counts = new Map();
  for (const [, title] of text.matchAll(/^#{1,6}\s+(.+)$/gm)) {
    const slug = title.toLowerCase().replace(/<[^>]*>/g, '')
      .replace(/[^\p{L}\p{N}_ -]/gu, '').replace(/ /g, '-');
    const n = counts.get(slug) || 0;
    counts.set(slug, n + 1);
    ids.add(n ? `${slug}-${n}` : slug);
  }
  return ids;
}
let checked = 0;
const pages = [join(root, 'README.md'), ...files(join(root, 'docs'), ['.md']), ...sitePages];
for (const page of pages) {
  const text = prose(readFileSync(page, 'utf8'));
  if (page.endsWith('-cn.md')) {
    const english = text.match(/\[English\]\(([^)]+)\)/);
    if (english) assert.equal(english[1], page.split('/').pop().replace('-cn.md', '.md'), `${page}: incorrect language switch`);
  }
  const links = [...text.matchAll(/\[[^\]]*\]\(([^)]+)\)/g)].map((m) => m[1]);
  if (page.endsWith('.mdx')) {
    links.push(...[...text.matchAll(/^\s+link:\s+([^\s]+)\s*$/gm)].map((m) => m[1]));
  }
  for (const href of links) {
    const url = new URL(href, 'https://local.invalid/');
    let target;
    if (href.startsWith('/wirelink/') || url.origin === 'https://docs.silkenkite.ink') {
      target = routes.get(url.pathname);
    } else if (href.startsWith('https://github.com/starwey604/wirelink/')) {
      const match = url.pathname.match(/^\/starwey604\/wirelink\/(?:blob|tree)\/main\/(.*)$/);
      if (!match) continue;
      target = join(root, decodeURIComponent(match[1]));
    } else if (/^[a-z][a-z\d+.-]*:/i.test(href) || href.startsWith('//')) {
      continue; // External repositories/services are not local-link checks.
    } else if (page.endsWith('.mdx')) {
      const resolved = new URL(href, 'https://local.invalid' + route(page));
      target = routes.get(resolved.pathname);
    } else {
      target = url.pathname === '/' ? page : resolve(dirname(page), decodeURIComponent(href.split('#')[0]));
    }
    assert(target && existsSync(target), `${relative(root, page)}: missing target ${href}`);
    if (url.hash && ['.md', '.mdx'].includes(extname(target))) {
      assert(anchors(target).has(decodeURIComponent(url.hash.slice(1))),
        `${relative(root, page)}: missing anchor ${href}`);
    }
    checked++;
  }
}
const lessons = {
  'getting-started': 'getting-started',
  'request-a-result': 'tutorial-rpc',
  'save-device-info': 'tutorial-rpc-values',
  'async-rpc': 'tutorial-rpc-async',
  'application-jobs': 'tutorial-jobs',
  'udp-transport': 'tutorial-integration',
};
const shellBlocks = (path) => [...readFileSync(path, 'utf8').matchAll(/```sh\n([\s\S]*?)\n```/g)].map((m) => m[1]);
for (const [lesson, chapter] of Object.entries(lessons)) {
  for (const cn of [false, true]) {
    assert.deepEqual(shellBlocks(join(content, cn ? 'zh-cn' : '', `learn/${lesson}.mdx`)),
      shellBlocks(join(root, `docs/${chapter}${cn ? '-cn' : ''}.md`)), `${lesson}: site/repository commands differ`);
  }
}
console.log(`PASS: ${en.length} bilingual topics, ${checked} local/GitHub targets and six paired tutorial command sets`);
