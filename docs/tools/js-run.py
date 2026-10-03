"""Builds every JavaScript example, runs its tests, and records what the
guide shows for it: each `ffrwd-wasm --describe` and `--shape`, each
`ffrwd compile` and `ffrwd explain`, and what each query makes when it runs.
Everything lands in examples/js/out/, under the names run.py gives it where
run.py records the same thing. Given the Rust examples built, it asks the
Rust modules the same and writes compare.txt, example by example.

    python js-run.py               install, build, test, record, compare
    python js-run.py --no-build    from the modules already built
    python js-run.py --rust DIR    the Rust crates built under DIR, laid out
                                   as examples/ is (default: examples/)
"""

import argparse
import json
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
from run import CRATES, DESCRIBES, FFRWD, MEDIA, RUNS, SHAPES, TESTED, TOOLCHAIN, WASM, compact, quote, wrap  # noqa: E402

EXAMPLES = HERE.parent / "examples"
JS = EXAMPLES / "js"
OUT = JS / "out"
SDK = HERE.parent.parent / "js"
FIXTURES = TOOLCHAIN / "tests" / "fixtures"
NPM = shutil.which("npm") or "npm"

# The example each query is there to show.
SHOWS = {
    "01-first-node/invert.sql": "01-first-node/invert",
    "02-filter/zoom.sql": "02-filter/zoom",
    "02-filter/hflip.sql": "02-filter/blend",
    "02-filter/blend.sql": "02-filter/blend",
    "03-detector/glow.sql": "03-detector/glow",
    "03-detector/spans.sql": "03-detector/glow",
    "04-reader/band.sql": "04-reader/band",
    "04-reader/boxmask.sql": "04-reader/boxmask",
    "05-window/level.sql": "05-window/level",
    "05-window/still.sql": "05-window/still",
    "06-source/bars.sql": "06-source/bars",
    "06-source/beat.sql": "06-source/beat",
    "07-sink/tally.sql": "07-sink/tally",
    "07-sink/encoded.sql": "07-sink/tally",
    "08-held/cutin.sql": "08-held/cutin",
    "08-held/presence.sql": "08-held/cutin",
    "08-held/mosaic.sql": "08-held/mosaic",
    "09-pure/spans.sql": "09-pure/glow",
    "10-testing/refused.sql": "10-testing/levels",
}

# Queries whose product moves from run to run, by the guide's own account:
# what of it the two languages have to agree on.
LOOSE = {
    "06-source/beat.sql": "frame times (each picture's grey is the wall clock's second)",
    "08-held/presence.sql": "events (their times follow when the second file arrives)",
    "07-sink/tally.sql": "rows (but the tick each was reported on)",
    "07-sink/encoded.sql": "rows (but the tick each was reported on)",
}

# The products run.py records, by the names it gives them.
NAMED = {
    "03-detector/glow.sql": "03-detector-glows",
    "03-detector/spans.sql": "03-detector-spans",
    "05-window/level.sql": "05-window-levels",
    "05-window/still.sql": "05-window-stills",
    "09-pure/spans.sql": "09-pure-spans",
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


def js_wasm(crate):
    return f"build/{CRATES[crate]}.wasm"


def rust_wasm(crate):
    return f"target/wasm32-wasip2/release/{CRATES[crate]}.wasm"


def build():
    run([NPM, "install", "--no-audit", "--no-fund"], SDK)
    run([NPM, "install", "--no-audit", "--no-fund"], JS)
    lines = []
    for crate in CRATES:
        run(["node", "build.js"], JS / crate)
        size = (JS / crate / js_wasm(crate)).stat().st_size
        lines.append(f"{crate}: {js_wasm(crate)} built, {size / 2**20:.2f} MiB")
    for crate in TESTED:
        tested = run(["node", "--test"], JS / crate)
        passed = re.search(r"^\S+ pass (\d+)", tested.stdout, re.M)
        lines.append(f"{crate}: node --test, {passed.group(1) if passed else '?'} passed")
    save("build.txt", "\n".join(lines) + "\n")


def ask(argv, cwd):
    """What `ffrwd-wasm` answers: its JSON, or its refusal with the module's
    path written as <module>, so two languages' answers compare."""
    done = run([str(WASM), *argv], cwd, ok=(0, 1))
    if done.returncode:
        return done.stderr.replace(argv[1], "<module>")
    return json.loads(done.stdout)


def unordered(schema):
    if isinstance(schema, dict):
        return {k: sorted(v) if k == "required" else unordered(v) for k, v in schema.items()}
    return schema


def schemas_read(value):
    """`value` with every row schema read, its `required` taken as a set."""
    if isinstance(value, dict):
        return {
            k: unordered(json.loads(v)) if k == "schema" and isinstance(v, str) else schemas_read(v)
            for k, v in value.items()
        }
    if isinstance(value, list):
        return [schemas_read(v) for v in value]
    return value


def agree(js, rust):
    if json.dumps(js) == json.dumps(rust):
        return "same"
    if schemas_read(js) == schemas_read(rust):
        return "same but for the order of a row schema's keys"
    return "DIFFERS"


def shapes(rust_root, verdicts):
    for name, crate, params, bound in SHAPES:
        argv = ["--shape", js_wasm(crate)]
        if params is not None:
            argv += ["--params", json.dumps(params, separators=(",", ":"))]
        if bound is not None:
            argv += ["--bound", bound if isinstance(bound, str) else json.dumps(bound, separators=(",", ":"))]
        answer = ask(argv, JS / crate)
        command = "$ ffrwd-wasm " + " ".join(quote(a) for a in argv)
        if isinstance(answer, str):
            save(f"{name}.shape.txt", f"{command}\n{answer.replace('<module>', js_wasm(crate))}")
        else:
            save(f"{name}.shape.txt", f"{command}\n{compact(answer)}\n")
        if rust_root:
            argv[1] = rust_wasm(crate)
            verdicts.setdefault(crate, []).append((f"--shape {name}", agree(answer, ask(argv, rust_root / crate))))
    for name, crate in DESCRIBES:
        argv = ["--describe", js_wasm(crate)]
        answer = ask(argv, JS / crate)
        save(f"{name}.describe.txt", f"$ ffrwd-wasm {' '.join(argv)}\n{compact(answer)}\n")
        if rust_root:
            argv[1] = rust_wasm(crate)
            verdicts.setdefault(crate, []).append((f"--describe {name}", agree(answer, ask(argv, rust_root / crate))))


def stage(crates, directory, wasm_of):
    directory.mkdir(parents=True, exist_ok=True)
    for medium in MEDIA:
        shutil.copy(FIXTURES / medium, directory / medium)
    for crate in crates:
        shutil.copy(wasm_of(crate), directory / f"{CRATES[crate]}.wasm")


def compiled(query, directory, also):
    done = run(FFRWD + ["compile", "-f", query], directory, ok=(0, 1))
    printed = "\n".join(
        line if line.startswith(("error", "hint", "#")) else wrap(line)
        for line in (done.stdout + done.stderr).splitlines()
    )
    texts = {"compile": f"$ ffrwd compile -f {query}\n{printed}\n"}
    if "delays" in also:
        explained = run(FFRWD + ["explain", "--delays", "-f", query], directory)
        texts["delays"] = f"$ ffrwd explain --delays -f {query}\n{explained.stdout}"
    return done.returncode == 0, texts


def made_by(query, directory, jobs, subtitled=False):
    """Runs `query` and answers what it made, as text: a picture as its
    frames' md5s, a subtitle track as WebVTT, rows as written, and a sink's
    rows as the run printed them."""
    sql = (directory / query).read_text(encoding="utf-8")
    done = run(FFRWD + ["run", "-q", "-y", "--jobs", str(jobs), "-f", query], directory)
    target = re.search(r"\) TO '([^']+)'", sql)
    if target is None:
        return "rows", done.stdout
    made = target.group(1)
    if made.endswith(".ndjson"):
        return "ndjson", (directory / made).read_text(encoding="utf-8")
    if subtitled:
        return "vtt", run(["ffmpeg", "-v", "error", "-i", made, "-map", "0:s:0", "-f", "webvtt", "-"], directory).stdout
    return "framemd5", run(["ffmpeg", "-v", "error", "-i", made, "-map", "0:v", "-f", "framemd5", "-"], directory).stdout


def loosely(key, text):
    if key == "06-source/beat.sql":
        return [line.rsplit(",", 1)[0] for line in text.splitlines() if not line.startswith("#")]
    if key == "08-held/presence.sql":
        return [json.loads(line)["event"] for line in text.splitlines()]
    return [{k: v for k, v in json.loads(line).items() if k not in ("pts", "time")} for line in text.splitlines()]


def compared(key, kind, js, rust):
    if key in LOOSE:
        return f"same {LOOSE[key]}" if loosely(key, js) == loosely(key, rust) else "DIFFERS"
    if js == rust:
        return "same bytes"
    if kind in ("ndjson", "rows"):
        if [json.loads(line) for line in js.splitlines()] == [json.loads(line) for line in rust.splitlines()]:
            return "same rows, a whole number written 2 where serde writes 2.0"
    return "DIFFERS"


def queries(rust_root, verdicts):
    scratch = Path(tempfile.mkdtemp(prefix="js-run-"))
    for where, (crates, listed) in RUNS.items():
        chapter = where.split("/")[0]
        directory = JS / where
        stage(crates, directory, lambda crate: JS / crate / js_wasm(crate))
        other = scratch / where
        if rust_root:
            stage(crates, other, lambda crate: rust_root / crate / rust_wasm(crate))
            for query in listed:
                shutil.copy(directory / query, other / query)
        for query, also in listed.items():
            key = f"{chapter}/{query}"
            crate = SHOWS[key]
            ran, texts = compiled(query, directory, also)
            for kind, text in texts.items():
                save(f"{chapter}-{query}.{kind}.txt", text)
            if rust_root:
                theirs = compiled(query, other, also)[1]
                for kind in texts:
                    verdicts.setdefault(crate, []).append(
                        (f"{query} {kind}", "same" if texts[kind] == theirs[kind] else "DIFFERS")
                    )
            if not ran:
                continue
            for jobs in [1, 4] if key == "09-pure/spans.sql" else [1]:
                kind, made = made_by(query, directory, jobs, key == "05-window/level.sql")
                stem = NAMED.get(key, f"{chapter}-{Path(query).stem}")
                save(f"{stem}-jobs{jobs}.ndjson" if kind == "ndjson" else f"{stem}.{kind}", made)
                if rust_root:
                    theirs = made_by(query, other, jobs, key == "05-window/level.sql")[1]
                    run_name = f"{query} run" + (f" --jobs {jobs}" if jobs > 1 else "")
                    verdicts.setdefault(crate, []).append((run_name, compared(key, kind, made, theirs)))
    shutil.rmtree(scratch, ignore_errors=True)


def package(rust_root, verdicts):
    """The levels package's own recipe, compiled as `ffrwd run levels` would
    run it. Its plan names the module by its full path, so the two compare
    with each package's own path taken out."""
    argv = ["compile", "levels", "-v", "source=../run/av.mp4", "-v", "dest=out.mp4"]
    directory = JS / "10-testing" / "levels"
    done = run(FFRWD + argv, directory)
    save("10-testing-package.compile.txt", "$ ffrwd " + " ".join(argv) + "\n" + done.stdout)
    if rust_root:
        other = rust_root / "10-testing" / "levels"
        theirs = run(FFRWD + argv, other).stdout
        ours = done.stdout.replace(str(directory / "build" / "levels.wasm"), "<module>")
        theirs = theirs.replace(str(other / "target" / "wasm32-wasip2" / "release" / "levels.wasm"), "<module>")
        verdict = "same but for the module's path" if ours == theirs else "DIFFERS"
        verdicts.setdefault("10-testing/levels", []).append(("ffrwd compile levels", verdict))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--no-build", action="store_true")
    parser.add_argument("--rust", type=Path, default=EXAMPLES)
    args = parser.parse_args()
    rust_root = args.rust.resolve()
    if not all((rust_root / crate / rust_wasm(crate)).exists() for crate in CRATES):
        print(f"no Rust crates built under {rust_root}: recording without comparing")
        rust_root = None
    if not args.no_build:
        build()
    verdicts = {}
    shapes(rust_root, verdicts)
    queries(rust_root, verdicts)
    package(rust_root, verdicts)
    if rust_root:
        lines = [f"{crate}: {what}: {verdict}" for crate in CRATES for what, verdict in verdicts.get(crate, [])]
        save("compare.txt", "\n".join(lines) + "\n")


if __name__ == "__main__":
    main()
