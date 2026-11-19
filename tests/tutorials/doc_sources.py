# SPDX-License-Identifier: Apache-2.0
"""Extract and verify [docs:<name>] source fragments against Markdown/MDX docs."""
from __future__ import annotations

import argparse
from pathlib import Path
import re
import textwrap

SOURCE_EXTENSIONS = {".c", ".h", ".cpp", ".hpp", ".wl", ".py"}
MARKER_RE = re.compile(
    r"^\s*(?:/\*\s*|//\s*|#\s*)\[(/?)docs:([a-zA-Z0-9_.-]+)\]\s*(?:\*/)?\s*$"
)
DOC_TAG_RE = re.compile(
    r"^[ \t]*(?:<!--|\{/\*)\s*\[docs:([a-zA-Z0-9_.-]+)\]\s*(?:-->|\*/\})\s*$",
    re.M,
)
DOC_BLOCK_RE = re.compile(
    r"^[ \t]*(?:<!--|\{/\*)\s*\[docs:([a-zA-Z0-9_.-]+)\]\s*(?:-->|\*/\})\s*\n+"
    r"[ \t]*```[^\n]*\n(.*?)\n[ \t]*```",
    re.S | re.M,
)
REQUIRED_SNIPPETS = {
    "telemetry_schema",
    "telemetry_tx_write",
    "telemetry_publish",
    "telemetry_receive",
    "calculator_schema",
    "calculator_server",
    "calculator_sync_rpc",
    "calculator_rejected_callback",
    "calculator_async_rpc",
    "calculator_cancel_timeout",
    "device_info_schema",
    "device_info_server",
    "device_info_done",
    "device_info_client",
    "device_job_start",
    "device_job_poll",
    "composed_product_schema",
    "udp_server_loop",
    "udp_host_step",
    "python_sdk_client",
    "loopback_setup",
}


def collect_snippets(examples_dir: Path) -> dict[str, tuple[Path, str]]:
    """Extract all named [docs:<name>] fragments from compiled example sources."""
    snippets: dict[str, tuple[Path, str]] = {}
    for path in sorted(examples_dir.rglob("*")):
        if not path.is_file() or path.suffix not in SOURCE_EXTENSIONS:
            continue
        if any(part in {"build", "generated", "__pycache__"} for part in path.parts):
            continue
        active: dict[str, list[str]] = {}
        for lineno, line in enumerate(path.read_text(encoding="utf-8").splitlines(), start=1):
            match = MARKER_RE.match(line)
            if match:
                closing, name = match.group(1) == "/", match.group(2)
                if not closing:
                    if name in active or name in snippets:
                        raise RuntimeError(f"Duplicate [docs:{name}] in {path}:{lineno}")
                    active[name] = []
                else:
                    if name not in active:
                        raise RuntimeError(f"Unmatched [/docs:{name}] in {path}:{lineno}")
                    raw = "\n".join(active.pop(name))
                    snippets[name] = (path, textwrap.dedent(raw).strip())
                continue
            for lines in active.values():
                lines.append(line)
        if active:
            unclosed = ", ".join(sorted(active))
            raise RuntimeError(f"Unclosed [docs:{unclosed}] in {path}")
    missing = sorted(REQUIRED_SNIPPETS - snippets.keys())
    if missing:
        raise RuntimeError(f"Missing required source anchors in examples/: {', '.join(missing)}")
    return snippets


def verify_documents(
    root: Path, documents: list[Path], snippets: dict[str, tuple[Path, str]]
) -> int:
    """Verify every [docs:<name>] code block in documents matches examples/."""
    checked = 0
    for document in documents:
        text = document.read_text(encoding="utf-8")
        for match in DOC_BLOCK_RE.finditer(text):
            name, actual = match.group(1), textwrap.dedent(match.group(2)).strip()
            if name not in snippets:
                raise RuntimeError(f"{document.relative_to(root)} references unknown [docs:{name}]")
            source_path, expected = snippets[name]
            if actual != expected:
                raise RuntimeError(
                    f"{document.relative_to(root)} [docs:{name}] is out of sync with "
                    f"{source_path.relative_to(root)}"
                )
            checked += 1
        # Catch dangling doc annotations not followed by a code fence.
        for tag in DOC_TAG_RE.finditer(text):
            if not DOC_BLOCK_RE.match(text, tag.start()):
                raise RuntimeError(
                    f"{document.relative_to(root)} has [docs:{tag.group(1)}] without a fenced code block"
                )
    return checked


def default_documents(root: Path) -> list[Path]:
    """Return repository documentation entry points present in the checkout."""
    docs = [root / "README.md", root / "README-en.md"]
    docs_dir = root / "docs"
    if docs_dir.is_dir():
        docs.extend(sorted(docs_dir.glob("*.md")))
    wlc_readme = root / "wlc" / "README.md"
    if wlc_readme.is_file():
        docs.append(wlc_readme)
    return [path for path in docs if path.is_file()]


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("paths", nargs="*", help="Optional Markdown/MDX files or directories to verify")
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    snippets = collect_snippets(root / "examples")
    documents: list[Path] = []
    if args.paths:
        for raw in args.paths:
            path = (root / raw).resolve() if not Path(raw).is_absolute() else Path(raw)
            if path.is_dir():
                documents.extend(sorted(p for p in path.rglob("*") if p.suffix in {".md", ".mdx"}))
            elif path.is_file():
                documents.append(path)
    else:
        documents = default_documents(root)
    checked = verify_documents(root, documents, snippets)
    if checked == 0:
        raise RuntimeError("No [docs:<name>] blocks found in checked documents")
    print(f"Verified {checked} doc source block(s) across {len(documents)} file(s)")


if __name__ == "__main__":
    main()
