"""Checks the guide against the examples in all four languages: every code
block under a language's label is text from that language's examples (or
its SDK's README), every tabbed block keeps the order Rust, C++,
JavaScript, Go, every JSON block is a value an `ffrwd-wasm` run printed (or
a manifest), every SQL block is a query `ffrwd compile` was run on, every
command block is a recorded run's, and the prose carries no em dash and
none of the forbidden words. An output shown once for every language is
compared with each language's own record of it, and what differs is
listed apart: it is a note for the owner, not a failure. Example files
are read as the guide spells them, by assemble.py's PUBLISHED rule.

    python check.py      after python run.py and python assemble.py
"""

import json
import re
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
from assemble import published  # noqa: E402

DOCS = HERE.parent
GUIDE = DOCS / "guide"
EXAMPLES = DOCS / "examples"
REPO = DOCS.parent
FORBIDDEN = ["0.18", "world", "adapt", "migrat", "used to", "trait", "struct", "closure"]
SKIP = {"target", "build", "node_modules", "out"}
TEXT = {".rs", ".toml", ".cpp", ".hpp", ".js", ".go", ".mod", ".sum", ".json", ".sql", ".sh", ".md"}
MODULE_PATH = re.compile(r"(target/wasm32-wasip2/release|build)/(\w+\.wasm)")

# Each language: its label, its blocks' info, its examples, its SDK's README,
# and where its runs are recorded.
LANGUAGES = {
    "rust": ("**Rust**", "rust", [p for p in EXAMPLES.glob("[0-9]*") if p.is_dir()], REPO / "rust", EXAMPLES / "out"),
    "cpp": ("**C++**", "cpp", [EXAMPLES / "cpp"], REPO / "cpp", EXAMPLES / "cpp" / "out"),
    "js": ("**JavaScript**", "js", [EXAMPLES / "js"], REPO / "js", EXAMPLES / "js" / "out"),
    "go": ("**Go**", "go", [EXAMPLES / "go"], REPO / "go", EXAMPLES / "go" / "out"),
}
BY_LABEL = {spec[0]: name for name, spec in LANGUAGES.items()}
ORDER = list(LANGUAGES)


def read(path):
    return path.read_text(encoding="utf-8")


def sources(language):
    _, _, roots, sdk, _ = LANGUAGES[language]
    found = {sdk / "README.md": read(sdk / "README.md")}
    for root in roots:
        for path in root.rglob("*"):
            if not path.is_file() or path.suffix not in TEXT or path.name == "blocks.json":
                continue
            if not SKIP & set(path.relative_to(root).parts[:-1]):
                found[path] = "\n".join(published(read(path).split("\n")))
    return found


def outputs(language):
    return {path.name: read(path) for path in LANGUAGES[language][4].glob("*") if path.is_file()}


def printed(texts):
    found = []
    for name, text in texts.items():
        body = text.split("\n", 1)[1] if text.startswith("$") else text
        try:
            found.append((name, json.loads(body)))
        except ValueError:
            pass
    return found


def holds(source, body):
    return f"\n{body}\n" in "\n" + source.rstrip("\n") + "\n"


def is_label(line):
    return line.strip() in BY_LABEL


def blocks(text):
    """Each fenced block: (line number, info string, body, its tab's language
    or None). A tab is a label, then plain paragraphs, then the block."""
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
        found.append((start + 1, opened.group(1), "\n".join(lines[start + 1 : at]), label_above(lines, start)))
        at += 1
    return found


def label_above(lines, start):
    at = start - 1
    while at >= 0:
        while at >= 0 and not lines[at].strip():
            at -= 1
        if at < 0:
            return None
        if is_label(lines[at]):
            return BY_LABEL[lines[at].strip()]
        end = at
        while at >= 0 and lines[at].strip():
            at -= 1
        paragraph = lines[at + 1 : end + 1]
        if any(is_label(line) for line in paragraph):
            return None
        if paragraph[0].startswith(("```", "#", "|", "- ", "@")) or re.match(r"^\d+\. ", paragraph[0]):
            return None
        if any(line.startswith("```") for line in paragraph):
            return None


def tab_runs(text, found):
    """Tabbed blocks: two labelled blocks are one when nothing but the second
    one's label and paragraphs lies between them, and a label already in the
    run starts a new one."""
    lines = text.split("\n")
    runs, current, after = [], [], 0
    for line, info, body, language in found:
        between = lines[after : line - 1]
        closed = line + (body.count("\n") + 1 if body else 0) + 1
        if not language:
            if current:
                runs.append(current)
            current, after = [], closed
            continue
        labels = [l.strip() for l in between if is_label(l)]
        joined = (
            current
            and labels == [LANGUAGES[language][0]]
            and not any(l.startswith(("#", "```", "@")) for l in between)
            and language not in [tab[1] for tab in current]
        )
        if not joined and current:
            runs.append(current)
            current = []
        current.append((line, language))
        after = closed
    if current:
        runs.append(current)
    return runs


def values(value):
    yield value
    if isinstance(value, dict):
        for member in value.values():
            yield from values(member)
    elif isinstance(value, list):
        for member in value:
            yield from values(member)


def compare_text(rust, other):
    if rust == other:
        return "same"
    if MODULE_PATH.sub(r"\2", rust) == MODULE_PATH.sub(r"\2", other):
        return "same but for the module's path"
    return "differs"


def main():
    code = {language: sources(language) for language in LANGUAGES}
    records = {language: outputs(language) for language in LANGUAGES}
    shown = {language: printed(records[language]) for language in LANGUAGES}
    queries = {p: read(p) for root in LANGUAGES["rust"][2] for p in root.glob("**/*.sql") if "target" not in p.parts}
    manifests = [(p, json.loads(read(p))) for root in LANGUAGES["rust"][2] for p in root.glob("**/ffrwd.json")]
    failures, report, differences, tabs = [], [], [], []

    for page in sorted(GUIDE.glob("*.md")):
        text = read(page)
        found = blocks(text)
        for run in tab_runs(text, found):
            present = [language for _, language in run]
            if present != [language for language in ORDER if language in present]:
                failures.append(f"{page.name}:{run[0][0]}: tabs out of order: {', '.join(present)}")
            tabs.append((page.name, run[0][0], present))

        for line, info, body, language in found:
            where = f"{page.name}:{line}"
            if language:
                check_tab(where, info, body, language, code, records, failures, report)
                continue
            for other, (_, own, _, _, _) in LANGUAGES.items():
                if info == own:
                    failures.append(f"{where}: a {info} block under no label")
            if info in ("sql", "pgsql"):
                owners = [p for p, src in queries.items() if body.strip() == src.strip()]
                if not owners:
                    failures.append(f"{where}: sql block matches no compiled query file")
                    continue
                rel = owners[0].relative_to(EXAMPLES)
                report.append(f"{where}: sql, {rel.as_posix()}")
                for language in ORDER[1:]:
                    copy = EXAMPLES / language / rel
                    if not copy.exists() or read(copy).strip() != body.strip():
                        differences.append(f"{where}: sql {rel}: {language} differs")
            elif info == "json":
                value = json.loads(body)
                runs = [name for name, out in shown["rust"] if any(value == part for part in values(out))]
                files = [p for p, out in manifests if value == out]
                if runs:
                    report.append(f"{where}: json, from out/{runs[0]}")
                    for language in ORDER[1:]:
                        theirs = [out for name, out in shown[language] if name in runs]
                        if not any(value == part for out in theirs for part in values(out)):
                            differences.append(f"{where}: json from out/{runs[0]}: {language} differs")
                elif files:
                    report.append(f"{where}: json, {files[0].relative_to(EXAMPLES).as_posix()}")
                else:
                    failures.append(f"{where}: json block matches no recorded output")
            elif body.startswith("$ "):
                owner = [name for name, out in records["rust"].items() if body.strip() == out.strip()]
                if not owner:
                    failures.append(f"{where}: output matches no recorded run")
                    continue
                report.append(f"{where}: output, out/{owner[0]}")
                for language in ORDER[1:]:
                    theirs = records[language].get(owner[0])
                    verdict = compare_text(body.strip(), theirs.strip()) if theirs else "not recorded"
                    if verdict != "same":
                        differences.append(f"{where}: out/{owner[0]}: {language} {verdict}")
            elif info in ("ndjson", "vtt"):
                lines = body.split("\n")
                owner = [name for name, out in records["rust"].items() if all(l in out.split("\n") for l in lines)]
                if not owner:
                    failures.append(f"{where}: {info} block lines found in no recorded run")
                    continue
                report.append(f"{where}: {info}, lines of out/{owner[0]}")
                for language in ORDER[1:]:
                    theirs = records[language].get(owner[0], "").split("\n")
                    if not all(l in theirs for l in lines):
                        differences.append(f"{where}: {info} lines of out/{owner[0]}: {language} differs")
            else:
                report.append(f"{where}: {info or 'plain'} block, not checked")

        for number, line in prose(text):
            where = f"{page.name}:{number}"
            if "—" in line:
                failures.append(f"{where}: an em dash")
            bare = re.sub(r"`[^`]*`", "", line)
            bare = re.sub(r"\]\([^)]*\)", "]", bare)
            for word in FORBIDDEN:
                if word in bare.lower():
                    failures.append(f"{where}: '{word}' in prose: {line.strip()}")
                elif word in line.lower():
                    report.append(f"{where}: '{word}' inside inline code or a link: {line.strip()}")

    print("\n".join(report))
    print("\nTabbed blocks, by page: the line, the languages it has, and any it lacks")
    for page, line, present in tabs:
        missing = [LANGUAGES[l][0].strip("*") for l in ORDER if l not in present]
        names = ", ".join(LANGUAGES[l][0].strip("*") for l in present)
        print(f"{page}:{line}: {len(present)} tabs ({names}){'; no ' + ', '.join(missing) if missing else ''}")
    print(f"\nOutputs shown once that a language recorded otherwise: {len(differences)}")
    print("\n".join(differences))
    print(f"\n{len(failures)} failures")
    print("\n".join(failures))
    return 1 if failures else 0


def check_tab(where, info, body, language, code, records, failures, report):
    label, own = LANGUAGES[language][:2]
    for other, (_, theirs, _, _, _) in LANGUAGES.items():
        if info == theirs and other != language:
            failures.append(f"{where}: a {info} block under {label}")
    if body.startswith("$ "):
        lines = body.split("\n")
        whole = [name for name, out in records[language].items() if body.strip() == out.strip()]
        heads = [name for name, out in records[language].items() if len(lines) == 1 and out.split("\n", 1)[0] == lines[0]]
        if whole:
            report.append(f"{where}: {language} output, {LANGUAGES[language][4].relative_to(EXAMPLES).as_posix()}/{whole[0]}")
        elif heads:
            report.append(f"{where}: {language} command, {LANGUAGES[language][4].relative_to(EXAMPLES).as_posix()}/{heads[0]}")
        elif lines[0].startswith("$ ffrwd init"):
            report.append(f"{where}: {language} command, typed by hand: {lines[0]}")
        else:
            failures.append(f"{where}: {language} command matches no recorded run")
        return
    owners = [path for path, source in code[language].items() if holds(source, body)]
    if owners:
        report.append(f"{where}: {language} {info or 'plain'}, from {owners[0].relative_to(REPO).as_posix()}")
        return
    lines = [line for line in body.split("\n") if line.strip()]
    listed = [
        directory
        for root in LANGUAGES[language][2]
        for directory in [p.parent for p in root.rglob("ffrwd.json")]
        if all((directory / line).is_file() for line in lines)
    ]
    if listed:
        report.append(f"{where}: {language} listing of {listed[0].relative_to(REPO).as_posix()}")
        return
    if info in ("", "sh"):
        commands = [line.split("  #")[0].strip() for line in lines]
        unfound = [line for line in commands if not any(line in source for source in code[language].values())]
        if not unfound:
            report.append(f"{where}: {language} commands, each line in its SDK's README or examples")
        else:
            report.append(f"{where}: {language} commands, typed by hand: {'; '.join(l.strip() for l in unfound)}")
        return
    failures.append(f"{where}: {language} {info} block found in none of its files")


def prose(text):
    """The text outside fenced blocks, each line with its number."""
    inside = False
    for number, line in enumerate(text.split("\n"), 1):
        if line.startswith("```"):
            inside = not inside
            continue
        if not inside:
            yield number, line


if __name__ == "__main__":
    sys.exit(main())
