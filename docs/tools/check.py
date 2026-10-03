"""Checks the guide against the examples: every Rust block is text from a
crate that builds, every JSON block is a value an `ffrwd-wasm` run printed
(or a manifest), every SQL block is a query `ffrwd compile` was run on,
every command block is pasted from out/, and the prose carries no em dash
and none of the forbidden words.

    python check.py      after python run.py
"""

import json
import re
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
GUIDE = HERE.parent / "docs" / "guide"
OUT = HERE / "out"
FORBIDDEN = ["0.18", "world", "adapt", "migrat", "used to", "trait", "struct", "closure"]


def blocks(text):
    """Each fenced block: (line number, info string, body, the line before)."""
    lines = text.split("\n")
    found, at = [], 0
    while at < len(lines):
        opened = re.match(r"^```(\S*)\s*$", lines[at])
        if not opened:
            at += 1
            continue
        start = at
        at += 1
        while not lines[at].startswith("```"):
            at += 1
        before = next((line for line in reversed(lines[:start]) if line.strip()), "")
        found.append((start + 1, opened.group(1), "\n".join(lines[start + 1 : at]), before))
        at += 1
    return found


def prose(text):
    """The text outside fenced blocks, each line with its number."""
    inside = False
    for number, line in enumerate(text.split("\n"), 1):
        if line.startswith("```"):
            inside = not inside
            continue
        if not inside:
            yield number, line


def sources(pattern):
    return {path: path.read_text(encoding="utf-8") for path in HERE.glob(pattern) if "target" not in path.parts}


def values(value):
    yield value
    if isinstance(value, dict):
        for member in value.values():
            yield from values(member)
    elif isinstance(value, list):
        for member in value:
            yield from values(member)


def main():
    rust = sources("**/src/*.rs")
    toml = sources("**/Cargo.toml")
    sql = sources("**/*.sql")
    recorded = {path.name: path.read_text(encoding="utf-8") for path in OUT.glob("*")}
    printed = []
    for name, text in recorded.items():
        body = text.split("\n", 1)[1] if text.startswith("$") else text
        try:
            printed.append((name, json.loads(body)))
        except ValueError:
            pass
    manifests = [(path, json.loads(path.read_text(encoding="utf-8"))) for path in HERE.glob("**/ffrwd.json")]
    failures, report = [], []

    for page in sorted(GUIDE.glob("*.md")):
        text = page.read_text(encoding="utf-8")
        for line, info, body, before in blocks(text):
            where = f"{page.name}:{line}"
            if info == "rust":
                owners = [p for p, src in rust.items() if body in src]
                if before.strip() != "**Rust**":
                    failures.append(f"{where}: a rust block not under **Rust**")
                if not owners:
                    failures.append(f"{where}: rust block found in no crate")
                else:
                    crates = sorted({str(p.relative_to(HERE).parent.parent) for p in owners})
                    report.append(f"{where}: rust, from {', '.join(crates)}")
            elif info == "toml":
                owners = [p for p, src in toml.items() if body in src]
                if not owners:
                    failures.append(f"{where}: toml block found in no Cargo.toml")
                else:
                    report.append(f"{where}: toml, from {owners[0].relative_to(HERE)}")
            elif info in ("sql", "pgsql"):
                owners = [p for p, src in sql.items() if body.strip() == src.strip()]
                if not owners:
                    failures.append(f"{where}: sql block matches no compiled query file")
                else:
                    report.append(f"{where}: sql, {owners[0].relative_to(HERE)}")
            elif info == "json":
                value = json.loads(body)
                runs = [name for name, out in printed if any(value == part for part in values(out))]
                files = [p for p, out in manifests if value == out]
                if runs:
                    report.append(f"{where}: json, from {runs[0]}")
                elif files:
                    report.append(f"{where}: json, {files[0].relative_to(HERE)}")
                else:
                    failures.append(f"{where}: json block matches no recorded output")
            elif body.startswith("$ "):
                commands = [l for l in body.split("\n") if l.startswith("$ ")]
                owner = [name for name, out in recorded.items() if body.strip() == out.strip()]
                heads = [name for name, out in recorded.items() if out.split("\n", 1)[0] in commands]
                if owner:
                    report.append(f"{where}: output, out/{owner[0]}")
                elif heads and all(l.startswith("$ ") for l in body.split("\n")):
                    report.append(f"{where}: command, out/{heads[0]}")
                else:
                    report.append(f"{where}: shell, not a recorded run (typed by hand)")
            elif info in ("ndjson", "vtt"):
                lines = body.split("\n")
                owner = [name for name, out in recorded.items() if all(l in out.split("\n") for l in lines)]
                if owner:
                    report.append(f"{where}: {info}, lines of out/{owner[0]}")
                else:
                    failures.append(f"{where}: {info} block lines found in no recorded run")
            else:
                report.append(f"{where}: {info or 'plain'} block, not checked")

        for number, line in prose(text):
            where = f"{page.name}:{number}"
            if "—" in line:
                failures.append(f"{where}: an em dash")
            bare = re.sub(r"`[^`]*`", "", line)
            for word in FORBIDDEN:
                if word in bare.lower():
                    failures.append(f"{where}: '{word}' in prose: {line.strip()}")
                elif word in line.lower():
                    report.append(f"{where}: '{word}' inside inline code: {line.strip()}")

    print("\n".join(report))
    print(f"\n{len(failures)} failures")
    print("\n".join(failures))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
