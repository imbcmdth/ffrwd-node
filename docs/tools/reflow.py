"""Rewraps the prose of templates/*.md at 76 columns: paragraphs and list
items, never code, directives, headings or tables."""

import re
import textwrap
from pathlib import Path

WIDTH = 76


def flush(block, out):
    if not block:
        return
    first = block[0]
    item = re.match(r"^(\s*(?:- |\d+\. ))", first)
    indent = " " * len(item.group(1)) if item else ""
    text = " ".join(line.strip() for line in block)
    if item:
        text = text[len(item.group(1).strip()) + 1 :]
        out.extend(textwrap.wrap(text, WIDTH, initial_indent=item.group(1), subsequent_indent=indent,
                                 break_long_words=False, break_on_hyphens=False))
    else:
        out.extend(textwrap.wrap(text, WIDTH, break_long_words=False, break_on_hyphens=False))


for path in sorted(Path(__file__).parent.glob("templates/*.md")):
    out, block, fenced = [], [], False
    for line in path.read_text(encoding="utf-8").split("\n"):
        if line.startswith("```"):
            flush(block, out); block = []
            fenced = not fenced
            out.append(line)
            continue
        if fenced or not line.strip() or line.startswith(("@", "#", "|", "**Rust**")):
            flush(block, out); block = []
            out.append(line)
            continue
        if re.match(r"^\s*(- |\d+\. )", line):
            flush(block, out); block = [line]
            continue
        block.append(line)
    flush(block, out)
    path.write_text("\n".join(out), encoding="utf-8", newline="\n")
    print(path.name)
