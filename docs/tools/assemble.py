"""Writes guide/*.md from templates/*.md, pasting code and outputs in place
of the directives, each on a line of its own:

    @rust <file> [<first>-<last>] [<info>]  **Rust**, a rust block, or <info>
    @toml <file>                            **Rust**, a toml block
    @cpp <file> [<first>-<last>] [<info>]   **C++**, a cpp block, or <info>
    @js <file> [<first>-<last>] [<info>]    **JavaScript**, a js block, or <info>
    @go <file> [<first>-<last>] [<info>]    **Go**, a go block, or <info>
    @code <info> <file> [<first>-<last>]    a block alone, under a label and a
                                            line the template writes itself
    @sql <file>
    @out <out file>                         a recorded run, whole
    @command <out file>                     its command line, a tab a language
    @body <out file>                        what it printed, without the command
    @outs <out file>                        the whole run, a tab a language
    @json <out file> <path>                 one value of a run's JSON
    @json-file <file>
    @lines <out file> <info> <first>-<last> lines of a run's product

An <info> of `-` is a block with none. A tab is a label, then any lines the
template puts between, then one block; tabs in a row are one tabbed block,
in the order Rust, C++, JavaScript, Go. Paths are relative to ../examples,
a language's under its own directory; Rust's runs are in examples/out and a
language's in examples/<lang>/out. Run after run.py.
"""

import json
import re
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
DOCS = HERE.parent
EXAMPLES = DOCS / "examples"
sys.path.insert(0, str(HERE))
from run import compact  # noqa: E402

# Each language: its label, its block's info, and where its runs are recorded.
LANGUAGES = {
    "rust": ("**Rust**", "rust", EXAMPLES / "out"),
    "cpp": ("**C++**", "cpp", EXAMPLES / "cpp" / "out"),
    "js": ("**JavaScript**", "js", EXAMPLES / "js" / "out"),
    "go": ("**Go**", "go", EXAMPLES / "go" / "out"),
}
SPAN = re.compile(r"\d+-\d+")


def lines_of(path, span=None):
    text = path.read_text(encoding="utf-8").rstrip("\n").split("\n")
    if not span:
        return text
    first, last = (int(n) for n in span.split("-"))
    return text[first - 1 : last]


def fenced(info, body):
    return [f"```{'' if info == '-' else info}", *body, "```"]


def tab(language, info, body):
    return [LANGUAGES[language][0], "", *fenced(info, body), ""]


def recorded(language, name):
    return lines_of(LANGUAGES[language][2] / name)


def file_args(args):
    """<file> [<first>-<last>] [<info>]"""
    path, rest = args[0], args[1:]
    span = rest.pop(0) if rest and SPAN.fullmatch(rest[0]) else None
    return path, span, rest[0] if rest else None


def expand(line):
    words = line.split()
    kind, args = words[0][1:], words[1:]
    if kind in ("rust", "toml"):
        path, span, info = file_args(args)
        return tab("rust", info or kind, lines_of(EXAMPLES / path, span))[:-1]
    if kind in ("cpp", "js", "go"):
        path, span, info = file_args(args)
        return tab(kind, info or LANGUAGES[kind][1], lines_of(EXAMPLES / path, span))[:-1]
    if kind == "code":
        path, span, _ = file_args(args[1:])
        return fenced(args[0], lines_of(EXAMPLES / path, span))
    if kind == "sql":
        return fenced("sql", lines_of(EXAMPLES / args[0]))
    if kind == "out":
        return fenced("", recorded("rust", args[0]))
    if kind == "command":
        out = []
        for language in LANGUAGES:
            out += tab(language, "-", recorded(language, args[0])[:1])
        return out[:-1]
    if kind == "body":
        body = recorded("rust", args[0])[1:]
        try:
            json.loads("\n".join(body))
            return fenced("json", body)
        except ValueError:
            return fenced("", body)
    if kind == "outs":
        out = []
        for language in LANGUAGES:
            out += tab(language, "-", recorded(language, args[0]))
        return out[:-1]
    if kind == "json":
        text = (EXAMPLES / "out" / args[0]).read_text(encoding="utf-8")
        value = json.loads(text.split("\n", 1)[1] if text.startswith("$") else text)
        for step in args[1].split(".") if len(args) > 1 else []:
            value = value[int(step)] if isinstance(value, list) else value[step]
        return fenced("json", compact(value).split("\n"))
    if kind == "json-file":
        return fenced("json", lines_of(EXAMPLES / args[0]))
    if kind == "lines":
        return fenced(args[1], lines_of(EXAMPLES / "out" / args[0], args[2]))
    raise SystemExit(f"unknown directive {line}")


def main():
    for template in sorted((HERE / "templates").glob("*.md")):
        out = []
        for line in template.read_text(encoding="utf-8").split("\n"):
            out.extend(expand(line) if line.startswith("@") else [line])
        (DOCS / "guide" / template.name).write_text("\n".join(out), encoding="utf-8", newline="\n")
        print(f"guide/{template.name}: {len(out)} lines")


if __name__ == "__main__":
    main()
