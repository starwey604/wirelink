#!/usr/bin/env node
import { readFileSync } from 'node:fs';
import { relative } from 'node:path';
import { pathToFileURL } from 'node:url';
import { collectFiles, unwrapText } from './unwrap-mdx.mjs';

const DEFAULT_ROOT = 'src/content/docs/zh-cn';

const CURLY_QUOTES = /[\u2018\u2019\u201C\u201D]/u;
const CJK = /[\u3400-\u4DBF\u4E00-\u9FFF\uF900-\uFAFF]/u;
const NEGATION_CLUSTER =
  /(?:不是|不会|不能|不要|不再|并非)[^。！？\n]{0,48}(?:也不是|也不会|也不能|也不|而是|且不|又不)/u;

const DISCOURAGED = [
  {
    pattern: /Unreliable latest-value delivery/i,
    hint: '英文短语直译；改为中文，或使用 `unreliable` / `latest` 代码形式',
  },
  {
    pattern: /高级 callback/,
    hint: '半译形态；改为「高级 API 的完成回调」等中文表述',
  },
  {
    pattern: /完成契约/,
    hint: '术语不稳定；改为「完成语义」或直接描述行为',
  },
  {
    pattern: /内存工作台/,
    hint: '生造词；改为具体描述，例如「内存中的模拟设备」',
  },
];

function maskCode(lines) {
  const masked = [...lines];
  let inFrontmatter = lines.length > 0 && lines[0].trim() === '---';
  let fence = null;
  for (let index = 0; index < lines.length; index += 1) {
    const line = lines[index];
    if (inFrontmatter) {
      masked[index] = '';
      if (index !== 0 && /^(?:---|\.\.\.)\s*$/.test(line.trim())) inFrontmatter = false;
      continue;
    }
    if (fence) {
      masked[index] = '';
      const close = line.match(/^\s*(`{3,}|~{3,})\s*$/);
      if (close && close[1][0] === fence.char && close[1].length >= fence.length) {
        fence = null;
      }
      continue;
    }
    const open = line.match(/^\s*(`{3,}|~{3,})/);
    if (open) {
      fence = { char: open[1][0], length: open[1].length };
      masked[index] = '';
      continue;
    }
    masked[index] = line.replace(/`[^`]*`/g, (span) => ' '.repeat(span.length));
  }
  return masked;
}

function lintFile(file) {
  const text = readFileSync(file, 'utf8');
  const lines = text.split('\n');
  const masked = maskCode(lines);
  const errors = [];
  const warnings = [];

  if (unwrapText(text) !== text) {
    errors.push('段落存在硬换行（运行 npm run fix:docs 展开）');
  }

  for (let index = 0; index < masked.length - 1; index += 1) {
    const tail = masked[index].trimEnd().slice(-1);
    const head = masked[index + 1].trimStart().slice(0, 1);
    if (tail && head && CJK.test(tail) && CJK.test(head)) {
      errors.push(`${index + 1}: 受保护区域内的段落硬换行（如 JSX 子节点），请手动合并`);
    }
  }

  masked.forEach((line, index) => {
    if (CURLY_QUOTES.test(line)) {
      errors.push(`${index + 1}: 使用中文弯引号，改为直角引号「」`);
    }
    if (NEGATION_CLUSTER.test(line)) {
      warnings.push(
        `${index + 1}: 否定堆叠，考虑改为肯定陈述或移入提示框/练习`,
      );
    }
    for (const rule of DISCOURAGED) {
      if (rule.pattern.test(line)) {
        warnings.push(`${index + 1}: ${rule.hint}`);
      }
    }
  });

  return { errors, warnings };
}

function runCli() {
  const targets = process.argv.slice(2).filter((arg) => !arg.startsWith('--'));
  const roots = targets.length > 0 ? targets : [DEFAULT_ROOT];
  const files = roots.flatMap((root) => collectFiles(root));

  let errorCount = 0;
  let warningCount = 0;

  for (const file of files) {
    const { errors, warnings } = lintFile(file);
    if (errors.length === 0 && warnings.length === 0) continue;
    console.log(`\n${relative(process.cwd(), file)}`);
    for (const message of errors) console.log(`  error  ${message}`);
    for (const message of warnings) console.log(`  warn   ${message}`);
    errorCount += errors.length;
    warningCount += warnings.length;
  }

  console.log('');
  if (errorCount > 0) {
    console.error(`FAIL: ${files.length} 个文件，${errorCount} 个错误，${warningCount} 个警告。`);
    process.exit(1);
  }
  console.log(`PASS: ${files.length} 个文件，0 个错误，${warningCount} 个警告。`);
}

const isMain =
  process.argv[1] !== undefined && import.meta.url === pathToFileURL(process.argv[1]).href;
if (isMain) runCli();
