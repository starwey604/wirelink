#!/usr/bin/env node
import { readdirSync, readFileSync, statSync, writeFileSync } from 'node:fs';
import { join, relative } from 'node:path';
import { pathToFileURL } from 'node:url';

const DEFAULT_ROOT = 'src/content/docs/zh-cn';

const WIDE =
  /[\u2E80-\u303F\u3040-\u30FF\u3130-\u318F\u3400-\u4DBF\u4E00-\u9FFF\uAC00-\uD7AF\uF900-\uFAFF\uFE30-\uFE4F\uFF00-\uFFEF]/u;
const WIDE_PUNCT =
  /[\u2018\u2019\u201C\u201D\u2014\u2026\u3001\u3002\u300A-\u3011\u3014-\u301F\uFF01\uFF08\uFF09\uFF0C\uFF1A\uFF1B\uFF1F\uFF5E\uFF5F\uFF60]/u;
const EMPHASIS = new Set(['*', '_', '~', '`']);
const ASCII_TRAILING_NO_SPACE = new Set([
  '(', '[', '{', '<', '-', '/', '@', '#', '$', '&', '+', '=', '\\', '"', "'",
]);
const ASCII_LEADING_NO_SPACE = new Set([
  ',', '.', ';', ':', '!', '?', ')', ']', '}', '>', '%', '"', "'",
]);

function isWide(char) {
  return WIDE.test(char) || WIDE_PUNCT.test(char);
}

function effectiveTail(text) {
  const withoutLink = text.replace(/\]\((?:[^()\s]*)(?:\s+"[^"]*")?\)$/, '');
  let index = withoutLink.length - 1;
  while (index >= 0 && EMPHASIS.has(withoutLink[index])) index -= 1;
  return index >= 0 ? withoutLink[index] : '';
}

function effectiveHead(text) {
  let index = 0;
  while (index < text.length && (EMPHASIS.has(text[index]) || text[index] === '[')) {
    index += 1;
  }
  return index < text.length ? text[index] : '';
}

function joiner(left, right) {
  const tail = effectiveTail(left);
  const head = effectiveHead(right);
  if (!tail || !head) return '';
  if (isWide(tail) || isWide(head)) {
    if (isWide(tail) && isWide(head)) return '';
    if (WIDE_PUNCT.test(tail) || WIDE_PUNCT.test(head)) return '';
    return ' ';
  }
  if (ASCII_TRAILING_NO_SPACE.has(tail) || ASCII_LEADING_NO_SPACE.has(head)) return '';
  return ' ';
}

function indentWidth(line) {
  let width = 0;
  for (const char of line) {
    if (char === ' ') width += 1;
    else if (char === '\t') width += 4;
    else break;
  }
  return width;
}

const LIST_ITEM_RE = /^(\s*)([-*+]|\d{1,9}[.)])([ \t]+)(\S.*)$/;
const FENCE_RE = /^\s*(`{3,}|~{3,})/;
const FENCE_CLOSE_RE = /^\s*(`{3,}|~{3,})\s*$/;
const JSX_OPEN_RE = /^\s*<([A-Za-z][A-Za-z0-9.]*)/;
const ESM_RE = /^\s*(import|export)\s/;

function isStructural(line) {
  const trimmed = line.trimStart();
  if (FENCE_RE.test(line)) return true;
  if (/^#{1,6}(\s|$)/.test(trimmed)) return true;
  if (/^(?:=+|-{2,})\s*$/.test(trimmed)) return true;
  if (/^([-*_])([ \t]*\1){2,}[ \t]*$/.test(trimmed)) return true;
  if (trimmed.startsWith('>')) return true;
  if (trimmed.startsWith(':::')) return true;
  if (trimmed.startsWith('<!--')) return true;
  if (ESM_RE.test(line)) return true;
  if (line.includes('|')) return true;
  return false;
}

function isCommentStart(line) {
  return line.trimStart().startsWith('<!--');
}

function commentEndOnLine(line) {
  return line.includes('-->');
}

function jsxEndIndex(lines, start) {
  const first = lines[start];
  if (commentEndOnLine(first)) return start;
  if (/^\s*<https?:\/\//.test(first)) return start;
  const match = first.trimStart().match(JSX_OPEN_RE);
  const root = match ? match[1] : null;
  const closeTag = root ? `</${root}>` : null;
  if (closeTag && first.includes(closeTag)) return start;
  if (/\/>\s*$/.test(first)) return start;
  for (let index = start + 1; index < lines.length; index += 1) {
    const line = lines[index];
    if (closeTag && line.includes(closeTag)) return index;
    if (/\/>\s*$/.test(line)) return index;
    if (commentEndOnLine(line)) return index;
  }
  return lines.length - 1;
}

export function unwrapText(text) {
  const lines = text.split('\n');
  const output = [];
  let index = 0;
  let inFrontmatter = lines.length > 0 && lines[0].trim() === '---';
  let fence = null;

  while (index < lines.length) {
    const line = lines[index];

    if (inFrontmatter) {
      output.push(line);
      if (index !== 0 && /^(?:---|\.\.\.)\s*$/.test(line.trim())) inFrontmatter = false;
      index += 1;
      continue;
    }

    if (fence) {
      output.push(line);
      const close = line.match(FENCE_CLOSE_RE);
      if (close && close[1][0] === fence.char && close[1].length >= fence.length) {
        fence = null;
      }
      index += 1;
      continue;
    }

    if (line.trim() === '') {
      output.push(line);
      index += 1;
      continue;
    }

    const openFence = line.match(FENCE_RE);
    if (openFence) {
      fence = { char: openFence[1][0], length: openFence[1].length };
      output.push(line);
      index += 1;
      continue;
    }

    if (isCommentStart(line)) {
      const end = jsxEndIndex(lines, index);
      for (let cursor = index; cursor <= end; cursor += 1) output.push(lines[cursor]);
      index = end + 1;
      continue;
    }

    if (ESM_RE.test(line)) {
      output.push(line);
      index += 1;
      continue;
    }

    if (/^\s*</.test(line)) {
      const end = jsxEndIndex(lines, index);
      for (let cursor = index; cursor <= end; cursor += 1) output.push(lines[cursor]);
      index = end + 1;
      continue;
    }

    if (/^(?: {4}|\t)/.test(line) && !LIST_ITEM_RE.test(line)) {
      output.push(line);
      index += 1;
      continue;
    }

    if (isStructural(line)) {
      output.push(line);
      index += 1;
      continue;
    }

    const listMatch = line.match(LIST_ITEM_RE);
    if (listMatch) {
      const prefix = listMatch[1] + listMatch[2] + listMatch[3];
      const contentIndent = indentWidth(prefix);
      let content = listMatch[4].trimEnd();
      index += 1;
      while (index < lines.length) {
        const next = lines[index];
        if (next.trim() === '') break;
        if (FENCE_RE.test(next) || ESM_RE.test(next) || /^\s*</.test(next)) break;
        if (isStructural(next)) break;
        if (LIST_ITEM_RE.test(next)) break;
        if (indentWidth(next) < contentIndent) break;
        const piece = next.trim();
        content += joiner(content, piece) + piece;
        index += 1;
      }
      output.push(prefix + content);
      continue;
    }

    let content = line.trim();
    index += 1;
    while (index < lines.length) {
      const next = lines[index];
      if (next.trim() === '') break;
      if (FENCE_RE.test(next) || ESM_RE.test(next) || /^\s*</.test(next)) break;
      if (isStructural(next)) break;
      if (LIST_ITEM_RE.test(next)) break;
      if (/^(?: {4}|\t)/.test(next)) break;
      const piece = next.trim();
      content += joiner(content, piece) + piece;
      index += 1;
    }
    output.push(content);
  }

  const result = output.join('\n');
  return result;
}

export function collectFiles(root) {
  const stats = statSync(root);
  if (stats.isFile()) return root.endsWith('.mdx') ? [root] : [];
  const files = [];
  for (const entry of readdirSync(root)) {
    const path = join(root, entry);
    if (statSync(path).isDirectory()) files.push(...collectFiles(path));
    else if (path.endsWith('.mdx')) files.push(path);
  }
  return files.sort();
}

function runCli() {
  const args = process.argv.slice(2);
  const check = args.includes('--check');
  const targets = args.filter((arg) => !arg.startsWith('--'));
  const roots = targets.length > 0 ? targets : [DEFAULT_ROOT];
  const files = roots.flatMap((root) => collectFiles(root));

  let changed = 0;
  for (const file of files) {
    const original = readFileSync(file, 'utf8');
    const unwrapped = unwrapText(original);
    if (original === unwrapped) continue;
    changed += 1;
    if (check) {
      console.error(`NEEDS UNWRAP  ${relative(process.cwd(), file)}`);
    } else {
      writeFileSync(file, unwrapped, 'utf8');
      console.log(`unwrapped     ${relative(process.cwd(), file)}`);
    }
  }

  if (check) {
    if (changed > 0) {
      console.error(`\nFAIL: ${changed} 个文件存在段落硬换行，运行 npm run fix:docs 展开。`);
      process.exit(1);
    }
    console.log(`PASS: 共检查 ${files.length} 个文件，未发现段落硬换行。`);
  } else {
    console.log(`DONE: 共检查 ${files.length} 个文件，改写 ${changed} 个。`);
  }
}

const isMain =
  process.argv[1] !== undefined && import.meta.url === pathToFileURL(process.argv[1]).href;
if (isMain) runCli();
