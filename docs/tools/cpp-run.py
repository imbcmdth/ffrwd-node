"""Builds every C++ example, runs its tests, and records every output the
guide shows for it into examples/cpp/out/, under the names run.py gives the
Rust ones, so the guide can paste either. It also records what the
chapters' queries make: the framemd5 of every picture, and every row.

    python cpp-run.py                  build, test and record everything
    python cpp-run.py --no-build       record from the modules already built
    python cpp-run.py --reference DIR  also run the same with the Rust
                                       modules, built in a copy of examples/
                                       at DIR, and write out/compare.txt

The library is built first, as the SDK's build.sh builds it, into
examples/cpp/build/sdk. WASI_SDK, WIT_BINDGEN and CXX name the tools when
they are not where the SDK's build.sh looks; FFRWD_CLI the toolchain and
FFRWD_MEDIA the test media, its tests/fixtures unless set.
"""

import json
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
from run import compact, quote, wrap  # noqa: E402

REPO = HERE.parent.parent
CPP = HERE.parent / "examples" / "cpp"
OUT = CPP / "out"
SDK = REPO / "cpp"
LIBRARY = CPP / "build" / "sdk"
TOOLCHAIN = Path(os.environ.get("FFRWD_CLI", r"E:\ffrwd-cli-node-cli\cli"))
WASM = TOOLCHAIN / ".venv" / "Scripts" / "ffrwd-wasm.exe"
FFRWD = ["uv", "run", "--no-sync", "--project", str(TOOLCHAIN), "ffrwd"]
MEDIA = Path(os.environ.get("FFRWD_MEDIA", TOOLCHAIN / "tests" / "fixtures"))
WASI_SDK = Path(os.environ.get("WASI_SDK", r"C:\tools\wasi-sdk-34.0-x86_64-windows"))
WIT_BINDGEN = os.environ.get("WIT_BINDGEN", "wit-bindgen")

# Every example: its directory, and the module it builds.
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


# (output name, example, params or None, bound as the CLI takes it or None),
# run.py's own list.
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

# Each chapter's run directory: the modules it needs, and its queries, each
# compiled, and explained where the guide shows the delays.
MEDIA_FILES = ["av.mp4", "av2.mp4", "testsrc.mp4", "smptebars.mp4"]
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

# Queries run with `ffrwd run` whose product is rows, at each `--jobs`
# given: run.py's, and the ones it only compiles.
ROWS = [
    ("03-detector/run", "glow.sql", "glows.ndjson", [1]),
    ("03-detector/run", "spans.sql", "spans.ndjson", [1]),
    ("05-window/run", "level.sql", "levels.mkv", [1]),
    ("05-window/run", "still.sql", "stills.ndjson", [1]),
    ("08-held/run", "presence.sql", "presence.ndjson", [1]),
    ("09-pure/run", "spans.sql", "spans.ndjson", [1, 4]),
]

# Queries whose product is a picture, recorded as the framemd5 of its video.
PICTURES = [
    ("01-first-node/run", "invert.sql", "inverted.mp4"),
    ("02-filter/run", "zoom.sql", "zoomed.mp4"),
    ("02-filter/run", "blend.sql", "blended.mp4"),
    ("04-reader/run", "band.sql", "banded.mp4"),
    ("04-reader/run", "boxmask.sql", "mask.mkv"),
    ("06-source/run", "bars.sql", "bars.mp4"),
    ("06-source/run", "beat.sql", "beat.mp4"),
    ("08-held/run", "cutin.sql", "cutin.mp4"),
    ("08-held/run", "mosaic.sql", "mosaic.mp4"),
]

# Sinks, whose rows the run prints.
SINKS = [
    ("07-sink/run", "tally.sql"),
    ("07-sink/run", "encoded.sql"),
]


class Side:
    """One language's modules: where each lives, as a path and as the shape
    command names it, and where its runs and outputs go."""

    def __init__(self, root, out, module):
        self.root = root
        self.out = out
        self.module = module

    def wasm(self, crate):
        return self.root / crate / self.module(CRATES[crate])

    def save(self, name, text, show=True):
        self.out.mkdir(parents=True, exist_ok=True)
        (self.out / name).write_text(text, encoding="utf-8", newline="\n")
        if show:
            print(f"--- {name}\n{text}", end="" if text.endswith("\n") else "\n")


def run(argv, cwd, ok=(0,), env=None, timeout=900):
    done = subprocess.run(
        argv, cwd=cwd, capture_output=True, text=True, encoding="utf-8", env=env, timeout=timeout
    )
    if done.returncode not in ok:
        sys.exit(f"{' '.join(map(str, argv))} in {cwd} failed:\n{done.stdout}{done.stderr}")
    return done


def library():
    """The bindings and the library, as the SDK's build.sh builds them."""
    gen, objects = LIBRARY / "gen", LIBRARY / "wasm"
    gen.mkdir(parents=True, exist_ok=True)
    objects.mkdir(parents=True, exist_ok=True)
    run([WIT_BINDGEN, "c", str(SDK / "wit" / "av.wit"), "--world", "node-module", "--out-dir", str(gen)], REPO)
    clang = [str(WASI_SDK / "bin" / "clang"), "--target=wasm32-wasip2", "-Os", "-ffunction-sections", "-fdata-sections"]
    run(clang + ["-c", str(gen / "node_module.c"), "-o", str(objects / "node_module.o")], REPO)
    sources = ["json", "fields", "time", "shape", "params", "out", "glue"]
    cxx = [str(WASI_SDK / "bin" / "clang++"), "--target=wasm32-wasip2", "-std=c++23", "-Os", "-fno-exceptions"]
    cxx += ["-fno-rtti", "-ffunction-sections", "-fdata-sections", f"-I{SDK / 'include'}", f"-I{gen}"]
    for name in sources:
        run(cxx + ["-c", str(SDK / "src" / f"{name}.cpp"), "-o", str(objects / f"{name}.o")], REPO)
    archive = objects / "libffrwd-node.a"
    archive.unlink(missing_ok=True)
    run([str(WASI_SDK / "bin" / "llvm-ar"), "rcs", str(archive)] + [str(objects / f"{n}.o") for n in sources], REPO)


def build(side):
    env = dict(os.environ, FFRWD_NODE_BUILD=str(LIBRARY), WASI_SDK=str(WASI_SDK))
    lines = []
    for crate, name in CRATES.items():
        done = run(["sh", "build.sh"], CPP / crate, env=env)
        warnings = done.stderr.count("warning:")
        size = side.wasm(crate).stat().st_size
        lines.append(f"{crate}: {name}.wasm built, {size} bytes, {warnings} warnings")
    for crate in TESTED:
        done = run(["sh", "build.sh", "test"], CPP / crate, env=env)
        counted = re.search(r"(\d+) tests, (\d+) failed", done.stdout)
        lines.append(f"{crate}: sh build.sh test, {counted.group(1)} run, {counted.group(2)} failed")
    side.save("build.txt", "\n".join(lines) + "\n")


def shapes(side, show=True):
    for name, crate, params, bound in SHAPES:
        module = side.module(CRATES[crate])
        argv = [str(WASM), "--shape", module]
        if params is not None:
            argv += ["--params", json.dumps(params, separators=(",", ":"))]
        if bound is not None:
            argv += ["--bound", bound if isinstance(bound, str) else json.dumps(bound, separators=(",", ":"))]
        done = run(argv, side.root / crate, ok=(0, 1))
        command = "$ ffrwd-wasm " + " ".join(quote(a) for a in argv[1:])
        if done.returncode:
            side.save(f"{name}.shape.txt", f"{command}\n{done.stderr}", show)
        else:
            side.save(f"{name}.shape.txt", f"{command}\n{compact(json.loads(done.stdout))}\n", show)
    for name, crate in DESCRIBES:
        argv = [str(WASM), "--describe", side.module(CRATES[crate])]
        done = run(argv, side.root / crate)
        command = "$ ffrwd-wasm " + " ".join(quote(a) for a in argv[1:])
        side.save(f"{name}.describe.txt", f"{command}\n{compact(json.loads(done.stdout))}\n", show)


def compiles(side, show=True):
    for where, (crates, queries) in RUNS.items():
        directory = side.root / where
        directory.mkdir(parents=True, exist_ok=True)
        for medium in MEDIA_FILES:
            shutil.copy(MEDIA / medium, directory / medium)
        for crate in crates:
            shutil.copy(side.wasm(crate), directory / f"{CRATES[crate]}.wasm")
        for query in queries:
            if directory != CPP / where:
                shutil.copy(CPP / where / query, directory / query)
        chapter = where.split("/")[0]
        for query, also in queries.items():
            done = run(FFRWD + ["compile", "-f", query], directory, ok=(0, 1))
            printed = "\n".join(
                line if line.startswith(("error", "hint", "#")) else wrap(line)
                for line in (done.stdout + done.stderr).splitlines()
            )
            side.save(f"{chapter}-{query}.compile.txt", f"$ ffrwd compile -f {query}\n{printed}\n", show)
            if "delays" in also:
                done = run(FFRWD + ["explain", "--delays", "-f", query], directory)
                side.save(f"{chapter}-{query}.delays.txt", f"$ ffrwd explain --delays -f {query}\n{done.stdout}", show)


def executes(side, show=True):
    for where, query, product, jobs in ROWS:
        directory = side.root / where
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
            side.save(name, text, show)
    for where, query, product in PICTURES:
        directory = side.root / where
        chapter = where.split("/")[0]
        run(FFRWD + ["run", "-q", "-y", "-f", query], directory)
        text = run(["ffmpeg", "-v", "error", "-i", product, "-map", "0:v:0", "-f", "framemd5", "-"], directory).stdout
        side.save(f"{chapter}-{Path(product).stem}.framemd5", text, False)
    for where, query in SINKS:
        directory = side.root / where
        chapter = where.split("/")[0]
        done = run(FFRWD + ["run", "-q", "-y", "-f", query], directory)
        side.save(f"{chapter}-{Path(query).stem}.rows.ndjson", done.stdout, show)


# The two packages, each with its recipe: compiled as `ffrwd run <recipe>`
# would run it, and run on the chapter's media.
PACKAGES = [
    ("01-first-node/scaffold", "passthrough"),
    ("10-testing/levels", "levels"),
]


def package(side, show=True):
    for crate, recipe in PACKAGES:
        directory = side.root / crate
        chapter = crate.split("/")[0]
        argv = ["compile", recipe, "-v", "source=../run/av.mp4", "-v", "dest=out.mp4"]
        done = run(FFRWD + argv, directory)
        side.save(f"{chapter}-package.compile.txt", "$ ffrwd " + " ".join(argv) + "\n" + done.stdout, show)
        made = f"../run/{recipe}-package.mkv"
        run(FFRWD + ["run", "-q", "-y", recipe, "-v", "source=../run/av.mp4", "-v", f"dest={made}"], directory)
        text = run(["ffmpeg", "-v", "error", "-i", made, "-map", "0:v:0", "-f", "framemd5", "-"], directory).stdout
        side.save(f"{chapter}-package.framemd5", text, False)


MODULE = re.compile(r"(?:build|target[\\/]wasm32-wasip2[\\/]release)[\\/](\w+)\.wasm")


def from_announcement(text):
    """Presence rows counted from the tick the feed was announced on, which
    the host fixes by when the feed's frames arrive, so it moves from run
    to run in either language."""
    rows = [json.loads(line) for line in text.splitlines()]
    if not rows:
        return ""
    first = rows[0]["t"]
    return "\n".join(
        f"{row['event']} {row['t'] - first:.9f} {row['at'] - first:.9f} {row['text']}" for row in rows
    )


def body(name, text):
    """What a recorded output says, less what cannot be the same in two runs:
    the module's path, the greys `beat` takes from the wall clock, and the
    tick a held feed is first known on. With why it was left out."""
    if name.endswith((".shape.txt", ".describe.txt")):
        text = text.split("\n", 1)[1]
        try:
            return json.dumps(json.loads(text), sort_keys=True), "but for the module's path"
        except json.JSONDecodeError:
            pass
    if name == "06-source-beat.framemd5":
        hashless = "\n".join(line.rsplit(",", 1)[0] for line in text.splitlines())
        return hashless, "but for the greys, which follow the wall clock"
    if name.startswith("08-held-presence") and name.endswith(".ndjson"):
        return from_announcement(text), "once counted from the tick the feed was announced on"
    text = MODULE.sub(r"<module \1>", text)
    return re.sub(r"[^\s']*<module", "<module", text), "but for the module's path"


def compare(cpp, rust):
    lines = []
    for theirs in sorted(rust.out.iterdir()):
        ours = cpp.out / theirs.name
        if not ours.exists():
            lines.append(f"{theirs.name}: missing")
            continue
        a, b = ours.read_text(encoding="utf-8"), theirs.read_text(encoding="utf-8")
        (left, why), (right, _) = body(theirs.name, a), body(theirs.name, b)
        if a == b:
            lines.append(f"{theirs.name}: identical")
        elif left == right:
            lines.append(f"{theirs.name}: identical {why}")
        else:
            lines.append(f"{theirs.name}: DIFFERS")
    cpp.save("compare.txt", "\n".join(lines) + "\n")


def main():
    cpp = Side(CPP, OUT, lambda name: f"build/{name}.wasm")
    if "--no-build" not in sys.argv:
        library()
        build(cpp)
    shapes(cpp)
    compiles(cpp)
    executes(cpp)
    package(cpp)
    if "--reference" in sys.argv:
        root = Path(sys.argv[sys.argv.index("--reference") + 1]).resolve()
        rust = Side(root, root / "out-reference", lambda name: f"target/wasm32-wasip2/release/{name}.wasm")
        shapes(rust, False)
        compiles(rust, False)
        executes(rust, False)
        package(rust, False)
        compare(cpp, rust)


if __name__ == "__main__":
    main()
