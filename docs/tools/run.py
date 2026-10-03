"""Builds every Rust example, runs the tests, and prints every
`ffrwd-wasm --describe`, `--shape`, `ffrwd compile` and `ffrwd explain` the
guide shows. Each output also lands in examples/out/, which check.py reads.
Then it runs cpp-run.py, js-run.py and go-run.py, which record the same for
their languages under examples/<lang>/out/ and compare them with these.

    python run.py            build, test and print everything
    python run.py --no-build print from the modules already built
    python run.py --rust     the Rust examples alone

FFRWD_CLI names the toolchain and FFRWD_MEDIA the test media, its
tests/fixtures unless set.
"""

import json
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
EXAMPLES = HERE.parent / "examples"
OUT = EXAMPLES / "out"
TOOLCHAIN = Path(os.environ.get("FFRWD_CLI", r"E:\ffrwd-cli-node-cli\cli"))
WASM = TOOLCHAIN / ".venv" / "Scripts" / "ffrwd-wasm.exe"
FFRWD = ["uv", "run", "--no-sync", "--project", str(TOOLCHAIN), "ffrwd"]
FIXTURES = Path(os.environ.get("FFRWD_MEDIA", TOOLCHAIN / "tests" / "fixtures"))
LANGUAGES = ["cpp", "js", "go"]

# Every crate: its directory, and the module it builds.
CRATES = {
    "01-first-node/invert": "invert",
    "01-first-node/scaffold": "invert",
    "02-filter/zoom": "zoom",
    "02-filter/blend": "blend",
    "03-detector/glow": "glow",
    "04-reader/band": "band",
    "04-reader/boxmask": "boxmask",
    "05-window/level": "level",
    "05-window/still": "still",
    "06-source/bars": "bars",
    "06-source/beat": "beat",
    "07-sink/tally": "tally",
    "08-held/cutin": "cutin",
    "08-held/mosaic": "mosaic",
    "09-pure/glow": "glow",
    "10-testing/levels": "levels",
}

TESTED = ["03-detector/glow", "09-pure/glow", "10-testing/levels"]


def rate(input_name, *rates):
    return {"input": input_name, "streams": [{"rate": {"num": r, "den": 1}} for r in rates]}


# (output name, crate, params or None, bound as the CLI takes it or None)
SHAPES = [
    ("invert", "01-first-node/invert", None, "v"),
    ("zoom", "02-filter/zoom", None, "v"),
    ("blend", "02-filter/blend", None, "v,over"),
    ("glow-3", "03-detector/glow", None, "v"),
    ("band", "04-reader/band", None, "v,cues"),
    ("boxmask", "04-reader/boxmask", None, "v,boxes"),
    ("level-tumbling", "05-window/level", {"window": 2}, [rate("a", 48000)]),
    ("level-hopping", "05-window/level", {"window": 1, "hop": 0.5}, [rate("a", 44100)]),
    ("level-names", "05-window/level", None, "a"),
    ("still", "05-window/still", {"longest": 2}, [rate("v", 15)]),
    ("bars", "06-source/bars", {"width": 640, "height": 360}, None),
    ("bars-seconds", "06-source/bars", {"width": 640, "height": 360, "seconds": 5}, None),
    ("beat", "06-source/beat", {"every": 0.5}, None),
    ("tally", "07-sink/tally", None, "video,audio"),
    ("cutin-port", "08-held/cutin", {"port": 9100}, [rate("v", 15)]),
    ("cutin-stream", "08-held/cutin", {"lead": 1}, [rate("v", 15), rate("feed", 15)]),
    ("mosaic", "08-held/mosaic", {"columns": 3, "width": 960, "height": 240}, "v,v,v"),
    ("glow-9", "09-pure/glow", None, "v"),
    ("levels-names", "10-testing/levels", None, "v"),
    ("levels-rates", "10-testing/levels", None, [rate("v", 25)]),
    ("levels-refused", "10-testing/levels", {"black": 200, "white": 100}, "v"),
]

DESCRIBES = [
    ("invert", "01-first-node/invert"),
    ("glow-3", "03-detector/glow"),
    ("tally", "07-sink/tally"),
]

# Each chapter's run directory: the modules and media it needs, and its
# queries, each compiled, and explained where the guide shows the delays.
MEDIA = ["av.mp4", "av2.mp4", "testsrc.mp4", "smptebars.mp4"]
RUNS = {
    "01-first-node/run": (["01-first-node/invert"], {"invert.sql": []}),
    "02-filter/run": (["02-filter/zoom", "02-filter/blend"], {"zoom.sql": [], "blend.sql": [], "hflip.sql": []}),
    "03-detector/run": (["03-detector/glow"], {"glow.sql": [], "spans.sql": []}),
    "04-reader/run": (
        ["03-detector/glow", "04-reader/band", "04-reader/boxmask", "05-window/level"],
        {"band.sql": ["delays"], "boxmask.sql": []},
    ),
    "05-window/run": (
        ["05-window/level", "05-window/still"],
        {"level.sql": ["delays"], "still.sql": ["delays"]},
    ),
    "06-source/run": (["06-source/bars", "06-source/beat"], {"bars.sql": [], "beat.sql": []}),
    "07-sink/run": (["07-sink/tally"], {"tally.sql": [], "encoded.sql": []}),
    "08-held/run": (
        ["08-held/cutin", "08-held/mosaic"],
        {"cutin.sql": [], "presence.sql": [], "mosaic.sql": []},
    ),
    "09-pure/run": (["09-pure/glow"], {"spans.sql": []}),
    "10-testing/run": (["10-testing/levels"], {"refused.sql": []}),
}


def run(argv, cwd, ok=(0,)):
    done = subprocess.run(argv, cwd=cwd, capture_output=True, text=True, encoding="utf-8")
    if done.returncode not in ok:
        sys.exit(f"{' '.join(map(str, argv))} in {cwd} failed:\n{done.stdout}{done.stderr}")
    return done


def save(name, text):
    OUT.mkdir(exist_ok=True)
    (OUT / name).write_text(text, encoding="utf-8", newline="\n")
    print(f"--- {name}\n{text}", end="" if text.endswith("\n") else "\n")


def wasm_of(crate):
    return EXAMPLES / crate / "target" / "wasm32-wasip2" / "release" / f"{CRATES[crate]}.wasm"


def node_commit(crate):
    lock = (EXAMPLES / crate / "Cargo.lock").read_text(encoding="utf-8")
    found = re.search(r'source = "git\+https://github.com/imbcmdth/ffrwd-node\?tag=([^#]+)#(\w+)"', lock)
    return f"{found.group(1)} {found.group(2)[:8]}" if found else "from the repo's rust/"


def build():
    lines = []
    for crate in CRATES:
        run(["cargo", "build", "--release", "--target", "wasm32-wasip2"], EXAMPLES / crate)
        warned = run(["cargo", "build", "--release", "--target", "wasm32-wasip2"], EXAMPLES / crate)
        warnings = warned.stderr.count("warning:")
        lines.append(f"{crate}: {wasm_of(crate).name} built, ffrwd-node {node_commit(crate)}, {warnings} warnings")
    for crate in TESTED:
        tested = run(["cargo", "test"], EXAMPLES / crate)
        passed = re.findall(r"test result: ok\. (\d+) passed", tested.stdout)
        lines.append(f"{crate}: cargo test, {sum(map(int, passed))} passed")
    save("build.txt", "\n".join(lines) + "\n")


def compact(value, indent=0, width=88):
    """JSON as NODE-SHAPE.md shows it: an object or list on one line when it
    fits, otherwise one member a line."""
    flat = json.dumps(value, ensure_ascii=False, separators=(", ", ": "))
    if len(flat) + indent <= width or not isinstance(value, (dict, list)) or not value:
        return flat
    pad = " " * (indent + 2)
    if isinstance(value, dict):
        members = [f"{pad}{json.dumps(k)}: {compact(v, indent + 2, width)}" for k, v in value.items()]
        return "{\n" + ",\n".join(members) + "\n" + " " * indent + "}"
    members = [f"{pad}{compact(v, indent + 2, width)}" for v in value]
    return "[\n" + ",\n".join(members) + "\n" + " " * indent + "]"


def shapes():
    for name, crate, params, bound in SHAPES:
        argv = [str(WASM), "--shape", f"target/wasm32-wasip2/release/{CRATES[crate]}.wasm"]
        if params is not None:
            argv += ["--params", json.dumps(params, separators=(",", ":"))]
        if bound is not None:
            argv += ["--bound", bound if isinstance(bound, str) else json.dumps(bound, separators=(",", ":"))]
        done = run(argv, EXAMPLES / crate, ok=(0, 1))
        command = "$ ffrwd-wasm " + " ".join(quote(a) for a in argv[1:])
        if done.returncode:
            save(f"{name}.shape.txt", f"{command}\n{done.stderr}")
        else:
            save(f"{name}.shape.txt", f"{command}\n{compact(json.loads(done.stdout))}\n")
    for name, crate in DESCRIBES:
        argv = [str(WASM), "--describe", f"target/wasm32-wasip2/release/{CRATES[crate]}.wasm"]
        done = run(argv, EXAMPLES / crate)
        command = "$ ffrwd-wasm " + " ".join(quote(a) for a in argv[1:])
        save(f"{name}.describe.txt", f"{command}\n{compact(json.loads(done.stdout))}\n")


def quote(arg):
    return arg if re.fullmatch(r"[\w./,=:-]+", arg) else "'" + arg + "'"


def wrap(line, width=88):
    """A long shell line broken at spaces outside quotes, ` \\` at each break."""
    words, word, quoted = [], "", False
    for char in line:
        if char == "'":
            quoted = not quoted
        if char == " " and not quoted:
            words.append(word)
            word = ""
        else:
            word += char
    words.append(word)
    lines, current = [], ""
    for word in words:
        if current and len(current) + 1 + len(word) > width - 2:
            lines.append(current + " \\")
            current = "  " + word
        else:
            current = f"{current} {word}" if current else word
    lines.append(current)
    return "\n".join(lines)


def compiles():
    for where, (crates, queries) in RUNS.items():
        directory = EXAMPLES / where
        directory.mkdir(parents=True, exist_ok=True)
        for medium in MEDIA:
            shutil.copy(FIXTURES / medium, directory / medium)
        for crate in crates:
            shutil.copy(wasm_of(crate), directory / wasm_of(crate).name)
        chapter = where.split("/")[0]
        for query, also in queries.items():
            done = run(FFRWD + ["compile", "-f", query], directory, ok=(0, 1))
            printed = "\n".join(
                line if line.startswith(("error", "hint", "#")) else wrap(line)
                for line in (done.stdout + done.stderr).splitlines()
            )
            save(f"{chapter}-{query}.compile.txt", f"$ ffrwd compile -f {query}\n{printed}\n")
            if "delays" in also:
                done = run(FFRWD + ["explain", "--delays", "-f", query], directory)
                save(f"{chapter}-{query}.delays.txt", f"$ ffrwd explain --delays -f {query}\n{done.stdout}")


# Queries the guide shows the results of: run with `ffrwd run`, at each
# `--jobs` given, and the product saved beside the other outputs.
EXECS = [
    ("03-detector/run", "glow.sql", "glows.ndjson", [1]),
    ("03-detector/run", "spans.sql", "spans.ndjson", [1]),
    ("05-window/run", "level.sql", "levels.mkv", [1]),
    ("05-window/run", "still.sql", "stills.ndjson", [1]),
    ("09-pure/run", "spans.sql", "spans.ndjson", [1, 4]),
]


def executes():
    for where, query, product, jobs in EXECS:
        directory = EXAMPLES / where
        chapter = where.split("/")[0]
        for count in jobs:
            run(FFRWD + ["run", "-q", "-y", "--jobs", str(count), "-f", query], directory)
            made = directory / product
            if made.suffix == ".mkv":
                text = run(["ffmpeg", "-v", "error", "-i", product, "-map", "0:s:0", "-f", "webvtt", "-"], directory).stdout
                name = f"{chapter}-{made.stem}.vtt"
            else:
                text = made.read_text(encoding="utf-8")
                name = f"{chapter}-{made.stem}-jobs{count}.ndjson"
            save(name, text)


def package():
    """The levels package's own recipe, compiled as `ffrwd run levels` would
    run it. Its plan names the module by its full path, so it is kept out
    of the guide."""
    directory = EXAMPLES / "10-testing" / "levels"
    argv = ["compile", "levels", "-v", "source=../run/av.mp4", "-v", "dest=out.mp4"]
    done = run(FFRWD + argv, directory)
    save("10-testing-package.compile.txt", "$ ffrwd " + " ".join(argv) + "\n" + done.stdout)


def languages():
    flags = ["--no-build"] if "--no-build" in sys.argv else []
    for language in LANGUAGES:
        print(f"=== {language}-run.py", flush=True)
        done = subprocess.run([sys.executable, str(HERE / f"{language}-run.py"), *flags], cwd=HERE)
        if done.returncode:
            sys.exit(f"{language}-run.py failed")


def main():
    if "--no-build" not in sys.argv:
        build()
    shapes()
    compiles()
    executes()
    package()
    if "--rust" not in sys.argv:
        languages()


if __name__ == "__main__":
    main()
