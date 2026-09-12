import assert from 'node:assert/strict';
import test from 'node:test';
import { unwrapText } from './unwrap-mdx.mjs';

test('joins CJK soft wraps without a space', () => {
  assert.equal(unwrapText('中文一\n中文二\n'), '中文一中文二\n');
});

test('keeps one space at a CJK and Latin boundary', () => {
  assert.equal(unwrapText('运行 C\n源码\n'), '运行 C 源码\n');
});

test('keeps a full-width punctuation boundary free of spaces', () => {
  assert.equal(unwrapText('设备名称、\n固件版本\n'), '设备名称、固件版本\n');
});

test('preserves a fenced mermaid block', () => {
  const input = '前言：\n\n```mermaid\nflowchart LR\n  A --> B\n  B --> C\n```\n\n正文开始。\n';
  assert.equal(unwrapText(input), input);
});

test('preserves frontmatter and an import line', () => {
  const input = "---\ntitle: 测试\n---\n\nimport X from './x.astro';\n\n中文一\n中文二\n";
  const output = unwrapText(input);
  assert.match(output, /title: 测试/);
  assert.match(output, /import X from '\.\/x\.astro';/);
  assert.match(output, /中文一中文二/);
});

test('does not let an import swallow later content', () => {
  const input = "import X from './x.astro';\n\n中文段。\n\n<X />\n\n后文。\n";
  assert.equal(unwrapText(input), input);
});

test('joins a wrapped list item but keeps its marker', () => {
  const input = '- 第一行\n  第二行\n';
  assert.equal(unwrapText(input), '- 第一行第二行\n');
});
