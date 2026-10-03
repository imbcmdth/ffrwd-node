"""run.py for the Go examples: builds every one under ../examples/go, runs
their tests, and prints every `ffrwd-wasm --describe`, `--shape`, `ffrwd
compile` and `ffrwd explain` the guide shows, from the Go modules. Each
output lands in ../examples/go/out under the name run.py gives it.

Given the Rust examples built, it also asks both modules the same questions
and runs every chapter's queries on both, and writes what it found to
out/compare.txt: shapes and describes byte for byte, compiles as printed,
pictures by framemd5, rows as written.

    python go-run.py                  build, test, print and compare
    python go-run.py --no-build       print from the modules already built
    python go-run.py --rust DIR       the Rust examples, built, under DIR
                                      (default ../examples)

GO and COMPONENTIZE_GO name the tools when they are not on PATH.
"""

import difflib
import json
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
from run import CRATES, DESCRIBES, EXECS, FFRWD, MEDIA, RUNS, SHAPES, TOOLCHAIN, WASM, compact, quote, wrap  # noqa: E402

EXAMPLES = HERE.parent / "examples" / "go"
OUT = EXAMPLES / "out"
FIXTURES = TOOLCHAIN / "tests" / "fixtures"
GO = os.environ.get("GO") or shutil.which("go") or r"C:\tools\go1.27.0\go\bin\go.exe"
COMPONENTIZE_GO = (
    os.environ.get("COMPONENTIZE_GO")
    or shutil.which("componentize-go")
    or r"C:\tools\componentize-go-native\componentize-go.exe"
)
WORLD = "ffrwd:av/node-module@0.19.1"
SDK = "github.com/imbcmdth/ffrwd-node/go"

TESTED = ["03-detector/glow", "09-pure/glow", "10-testing/levels"]

# What each query writes: a file, or None for a sink's rows on stdout.
PRODUCTS = {
    "01-first-node/run": {"invert.sql": "inverted.mp4"},
    "02-filter/run": {"zoom.sql": "zoomed.mp4", "blend.sql": "blended.mp4"},
    "03-detector/run": {"glow.sql": "glows.ndjson", "spans.sql": "spans.ndjson"},
    "04-reader/run": {"band.sql": "banded.mp4", "boxmask.sql": "mask.mkv"},
    "05-window/run": {"level.sql": "levels.mkv", "still.sql": "stills.ndjson"},
    "06-source/run": {"bars.sql": "bars.mp4", "beat.sql": "beat.mp4"},
    "07-sink/run": {"tally.sql": None, "encoded.sql": None},
    "08-held/run": {"cutin.sql": "cutin.mp4", "presence.sql": "presence.ndjson", "mosaic.sql": "mosaic.mp4"},
    "09-pure/run": {"spans.sql": "spans.ndjson"},
}

# Pictures drawn from the wall clock: their times compare, their bytes do not.
WALL_CLOCK = {"beat.mp4"}

found = []
RUST = None


def run(argv, cwd, ok=(0,), env=None):
    done = subprocess.run(
        argv, cwd=cwd, capture_output=True, text=True, encoding="utf-8", env=env, timeout=900
    )
    if done.returncode not in ok:
        sys.exit(f"{' '.join(map(str, argv))} in {cwd} failed:\n{done.stdout}{done.stderr}")
    return done


def save(name, text):
    OUT.mkdir(exist_ok=True)
    (OUT / name).write_text(text, encoding="utf-8", newline="\n")
    print(f"--- {name}\n{text}", end="" if text.endswith("\n") else "\n")


def note(line):
    found.append(line)
    print(f"=== {line}")


def rows_of(text):
    try:
        return [json.loads(line) for line in text.splitlines()]
    except ValueError:
        return None


def compare(what, go, rust_text):
    if go == rust_text:
        note(f"{what}: identical")
        return
    diff = difflib.unified_diff(rust_text.splitlines(), go.splitlines(), "rust", "go", lineterm="", n=0)
    rows = rows_of(go)
    verdict = "the same rows, numbers spelled apart" if rows and rows == rows_of(rust_text) else "differs"
    note(f"{what}: {verdict}\n" + "\n".join(f"    {line}" for line in diff))


def go_wasm(crate):
    return EXAMPLES / crate / "build" / f"{CRATES[crate]}.wasm"


def rust_wasm(crate):
    return RUST / crate / "target" / "wasm32-wasip2" / "release" / f"{CRATES[crate]}.wasm"


def build():
    env = dict(os.environ, GO=GO, COMPONENTIZE_GO=COMPONENTIZE_GO)
    lines = []
    for crate, module in CRATES.items():
        directory = EXAMPLES / crate
        if (directory / "build.sh").exists():
            run(["sh", "build.sh"], directory, env=env)
        else:
            wit = run([GO, "list", "-m", "-f", "{{.Dir}}", SDK], directory).stdout.strip()
            argv = [COMPONENTIZE_GO, "-d", f"{wit}/wit", "-w", WORLD, "build", "--go", GO, "-o", f"build/{module}.wasm"]
            run(argv, directory)
        vetted = run([GO, "vet", "./..."], directory)
        unformatted = run([str(Path(GO).with_name("gofmt")), "-l", "."], directory).stdout.split()
        findings = len((vetted.stdout + vetted.stderr).splitlines())
        size = go_wasm(crate).stat().st_size
        lines.append(
            f"{crate}: {module}.wasm built, {size} bytes, {findings} go vet findings, {len(unformatted)} unformatted"
        )
    for crate in TESTED:
        tested = run([GO, "test", "-v", "./..."], EXAMPLES / crate)
        lines.append(f"{crate}: go test, {tested.stdout.count('--- PASS')} passed")
    save("build.txt", "\n".join(lines) + "\n")


def ask(wasm, crate, flag, params=None, bound=None, root=EXAMPLES):
    argv = [str(WASM), flag, wasm]
    if params is not None:
        argv += ["--params", json.dumps(params, separators=(",", ":"))]
    if bound is not None:
        argv += ["--bound", bound if isinstance(bound, str) else json.dumps(bound, separators=(",", ":"))]
    return argv, run(argv, root / crate, ok=(0, 1))


def answer(done, wasm):
    return done.stdout if done.returncode == 0 else done.stderr.replace(wasm, "<module>")


def shapes():
    for name, crate, params, bound in SHAPES:
        wasm = f"build/{CRATES[crate]}.wasm"
        argv, done = ask(wasm, crate, "--shape", params, bound)
        command = "$ ffrwd-wasm " + " ".join(quote(a) for a in argv[1:])
        if done.returncode:
            save(f"{name}.shape.txt", f"{command}\n{done.stderr}")
        else:
            save(f"{name}.shape.txt", f"{command}\n{compact(json.loads(done.stdout))}\n")
        if RUST:
            theirs = f"target/wasm32-wasip2/release/{CRATES[crate]}.wasm"
            _, other = ask(theirs, crate, "--shape", params, bound, root=RUST)
            compare(f"{name}.shape", answer(done, wasm), answer(other, theirs))
    described = {crate: name for name, crate in DESCRIBES}
    for crate in CRATES:
        wasm = f"build/{CRATES[crate]}.wasm"
        argv, done = ask(wasm, crate, "--describe")
        if crate in described:
            command = "$ ffrwd-wasm " + " ".join(quote(a) for a in argv[1:])
            save(f"{described[crate]}.describe.txt", f"{command}\n{compact(json.loads(done.stdout))}\n")
        if RUST:
            theirs = f"target/wasm32-wasip2/release/{CRATES[crate]}.wasm"
            _, other = ask(theirs, crate, "--describe", root=RUST)
            compare(f"{crate} describe", answer(done, wasm), answer(other, theirs))


def stage(directory, crates, wasm_of, queries=(), source=None):
    """A run directory: the media, the modules, and the queries from source."""
    directory.mkdir(parents=True, exist_ok=True)
    for medium in MEDIA:
        shutil.copy(FIXTURES / medium, directory / medium)
    for crate in crates:
        shutil.copy(wasm_of(crate), directory / wasm_of(crate).name)
    for query in queries:
        shutil.copy(source / query, directory / query)


def compiled(directory, query):
    done = run(FFRWD + ["compile", "-f", query], directory, ok=(0, 1))
    return "\n".join(
        line if line.startswith(("error", "hint", "#")) else wrap(line)
        for line in (done.stdout + done.stderr).splitlines()
    )


def compiles():
    for where, (crates, queries) in RUNS.items():
        directory = EXAMPLES / where
        stage(directory, crates, go_wasm)
        if RUST:
            stage(directory / "rust", crates, rust_wasm, queries, directory)
        chapter = where.split("/")[0]
        for query, also in queries.items():
            printed = compiled(directory, query)
            save(f"{chapter}-{query}.compile.txt", f"$ ffrwd compile -f {query}\n{printed}\n")
            if RUST:
                compare(f"{chapter} {query} compile", printed, compiled(directory / "rust", query))
            if "delays" in also:
                done = run(FFRWD + ["explain", "--delays", "-f", query], directory)
                save(f"{chapter}-{query}.delays.txt", f"$ ffrwd explain --delays -f {query}\n{done.stdout}")
                if RUST:
                    other = run(FFRWD + ["explain", "--delays", "-f", query], directory / "rust")
                    compare(f"{chapter} {query} delays", done.stdout, other.stdout)


def digest(directory, product):
    """What a run made, as text two runs can be compared by."""
    if product.endswith(".ndjson"):
        return (directory / product).read_text(encoding="utf-8")
    argv = ["ffmpeg", "-v", "error", "-i", product, "-map", "0:v", "-f", "framemd5", "-"]
    lines = run(argv, directory).stdout.splitlines()
    if product in WALL_CLOCK:
        lines = [line.rsplit(",", 1)[0] for line in lines]
    text = "\n".join(lines) + "\n"
    if product.endswith(".mkv") and "subtitle" in run(["ffprobe", "-v", "error", "-show_streams", product], directory).stdout:
        text += run(["ffmpeg", "-v", "error", "-i", product, "-map", "0:s:0", "-f", "webvtt", "-"], directory).stdout
    return text


def executed(directory, query, product, jobs):
    done = run(FFRWD + ["run", "-q", "-y", "--jobs", str(jobs), "-f", query], directory)
    return done.stdout if product is None else digest(directory, product)


def executes():
    for where, query, product, jobs in EXECS:
        directory = EXAMPLES / where
        chapter = where.split("/")[0]
        for count in jobs:
            run(FFRWD + ["run", "-q", "-y", "--jobs", str(count), "-f", query], directory)
            made = directory / product
            if made.suffix == ".mkv":
                argv = ["ffmpeg", "-v", "error", "-i", product, "-map", "0:s:0", "-f", "webvtt", "-"]
                text = run(argv, directory).stdout
                name = f"{chapter}-{made.stem}.vtt"
            else:
                text = made.read_text(encoding="utf-8")
                name = f"{chapter}-{made.stem}-jobs{count}.ndjson"
            save(name, text)
    if not RUST:
        return
    for where, queries in PRODUCTS.items():
        directory = EXAMPLES / where
        chapter = where.split("/")[0]
        for query, product in queries.items():
            for jobs in [1, 4] if where == "09-pure/run" else [1]:
                go = executed(directory, query, product, jobs)
                theirs = executed(directory / "rust", query, product, jobs)
                frames = sum(1 for line in go.splitlines() if line and not line.startswith("#"))
                what = f"{chapter} {query} run, --jobs {jobs}, {product or 'stdout'} ({frames} lines)"
                compare(what, go, theirs)


def package():
    """The levels package's own recipe, compiled as `ffrwd run levels` would
    run it."""
    directory = EXAMPLES / "10-testing" / "levels"
    argv = ["compile", "levels", "-v", "source=../run/av.mp4", "-v", "dest=out.mp4"]
    done = run(FFRWD + argv, directory)
    save("10-testing-package.compile.txt", "$ ffrwd " + " ".join(argv) + "\n" + done.stdout)
    if RUST:
        source = f"source={(EXAMPLES / '10-testing' / 'run' / 'av.mp4').resolve()}"
        argv = ["compile", "levels", "-v", source, "-v", "dest=out.mp4"]
        unplaced = [re.sub(r"-m '?levels=[^ ]+", "-m <module>", run(FFRWD + argv, where).stdout)
                    for where in (directory, RUST / "10-testing" / "levels")]
        compare("10-testing package compile", *unplaced)


def sizes():
    lines = []
    for crate in CRATES:
        go = go_wasm(crate).stat().st_size
        line = f"{crate}: go {go} bytes"
        if RUST:
            line += f", rust {rust_wasm(crate).stat().st_size} bytes"
        lines.append(line)
    save("sizes.txt", "\n".join(lines) + "\n")


def main():
    global RUST
    argv = sys.argv[1:]
    RUST = Path(argv[argv.index("--rust") + 1]).resolve() if "--rust" in argv else HERE.parent / "examples"
    if not all(rust_wasm(crate).exists() for crate in CRATES):
        print(f"no Rust modules built under {RUST}: printing the Go outputs alone")
        RUST = None
    if "--no-build" not in argv:
        build()
    sizes()
    shapes()
    compiles()
    executes()
    package()
    if RUST:
        save("compare.txt", "\n".join(found) + "\n")


if __name__ == "__main__":
    main()
