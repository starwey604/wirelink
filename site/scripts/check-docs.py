#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Check source-backed snippets, local routes, and quiz answer cardinality."""
from pathlib import Path
import re
import subprocess
import sys

site = Path(__file__).resolve().parents[1]
root = site.parent
docs = site / "src/content/docs"
subprocess.run([sys.executable, str(root / "tests/tutorials/doc_sources.py"), str(docs)], check=True)
paths = sorted(docs.rglob("*.mdx"))
routes = {"/wirelink/" + str(p.relative_to(docs).with_suffix("")).removesuffix("index") + ("" if p.stem == "index" else "/") for p in paths}
links = quizzes = source_links = 0
for path in paths:
    content = path.read_text()
    for link in re.findall(r"(?<![A-Za-z0-9])/wirelink/[A-Za-z0-9_./-]*", content):
        if link not in routes:
            raise SystemExit(f"{path.relative_to(site)}: missing local route {link}")
        links += 1
    for target in re.findall(r"https://github.com/starwey604/wirelink/(?:blob|tree)/main/([^\s)\]#]+)", content):
        if not (root / target).exists():
            raise SystemExit(f"{path.relative_to(site)}: missing repository path {target}")
        source_links += 1
    for quiz in re.findall(r"<MultipleChoice\b.*?/>", content, re.S):
        answers = re.findall(r'"?correct"?\s*:\s*(true|false)', quiz)
        if answers.count("true") != 1 or len(answers) < 2:
            raise SystemExit(f"{path.relative_to(site)}: quiz needs one correct answer and at least two choices")
        quizzes += 1
print(f"Verified {links} local links, {len(routes)} routes, {quizzes} quizzes, {source_links} repository paths")
