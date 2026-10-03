"""Writes guide/*.md from templates/*.md, pasting code and outputs in place
of the directives, each on a line of its own:

    @rust <crate file> [<first line>-<last line>]   under **Rust**
    @toml <file>                                     under **Rust**
    @sql <file>
    @out <out file>                                  a recorded run, whole
    @json <out file> <path>                          one value of a run's JSON
    @json-file <file>                                a JSON file, whole
    @lines <out file> <info> <first>-<last>          lines of a run's product

Paths are relative to ../examples. Run after examples/run.py.
"""

import json
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
EXAMPLES = HERE.parent / "examples"
sys.path.insert(0, str(EXAMPLES))
from run import compact  # noqa: E402


def lines_of(path, span):
    text = (EXAMPLES / path).read_text(encoding="utf-8").rstrip("\n").split("\n")
    if not span:
        return text
    first, last = (int(n) for n in span.split("-"))
    return text[first - 1 : last]


def fenced(info, body):
    return [f"```{info}", *body, "```"]


def expand(line):
    words = line.split()
    kind, args = words[0], words[1:]
    if kind == "@rust":
        return ["**Rust**", "", *fenced("rust", lines_of(args[0], args[1] if len(args) > 1 else None))]
    if kind == "@toml":
        return ["**Rust**", "", *fenced("toml", lines_of(args[0], args[1] if len(args) > 1 else None))]
    if kind == "@sql":
        return fenced("sql", lines_of(args[0], None))
    if kind == "@out":
        return fenced("", lines_of(f"out/{args[0]}", None))
    if kind == "@json":
        text = (EXAMPLES / "out" / args[0]).read_text(encoding="utf-8")
        value = json.loads(text.split("\n", 1)[1] if text.startswith("$") else text)
        for step in args[1].split(".") if len(args) > 1 else []:
            value = value[int(step)] if isinstance(value, list) else value[step]
        return fenced("json", compact(value).split("\n"))
    if kind == "@json-file":
        return fenced("json", lines_of(args[0], None))
    if kind == "@lines":
        return fenced(args[1], lines_of(f"out/{args[0]}", args[2]))
    raise SystemExit(f"unknown directive {line}")


def main():
    for template in sorted((HERE / "templates").glob("*.md")):
        out = []
        for line in template.read_text(encoding="utf-8").split("\n"):
            out.extend(expand(line) if line.startswith("@") else [line])
        (HERE / "guide" / template.name).write_text("\n".join(out), encoding="utf-8", newline="\n")
        print(f"guide/{template.name}: {len(out)} lines")


if __name__ == "__main__":
    main()
