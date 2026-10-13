#!/usr/bin/env python3
"""Move every parameter and fail if any of them made no difference to the frame.

**This is the only thing in the repo that catches a dead control.** A GLSL
uniform whose name does not match the C++ is silently ignored --
`glGetUniformLocation` returns -1 and `glUniform` on -1 is a documented no-op --
so a slider can be stone dead while everything compiles, links and renders.

Each render is 100 frames at 320x180 from a cold start with no audio, so Drive
Auto falls back to the beat (120 bpm, free-running): rings on every beat, a
burst on two and four, a bolt on the one, curls in between, and the wave.

## The context table

Most controls act in every render. The rest need the moment or the mode they
act in: Hit only while its doodles are still alive (a short render), Bolts
only once the first downbeat has gone (frame 120), the Centre only under
Placement Centre, the Colour only under Palette Colour, and the audio
controls only with music playing and Drive on Audio -- Sensitivity with a
ghost note in it, since it decides whether quiet notes count. Emptying the table
(`--no-context`) makes those go dead -- which is how the table was checked.

Usage::

    tools/sweep.py [--build BUILD_DIR] [--verbose] [--jobs N] [--no-context]
"""

import argparse
import concurrent.futures
import pathlib
import subprocess
import sys
import tempfile
import zlib

REPO = pathlib.Path(__file__).resolve().parent.parent

SIZE = "320x180"
FRAMES = ["--frames", "100"]

MUSIC = ["--audio", "demo", "--set", "Drive=Audio"]

# Sensitivity decides whether the quiet notes count, and the demo band has
# none: every hit in it is caught at every setting. The ghost band adds a soft
# kick between the kicks.
GHOSTS = ["--audio", "ghost", "--set", "Drive=Audio"]

CONTEXT = {
    "Hit": ["--frames", "20"],
    "Bolts": ["--frames", "125"],
    "Centre X": ["--set", "Placement=Centre"],
    "Centre Y": ["--set", "Placement=Centre"],
    "Colour": ["--set", "Palette=Colour"],
    "Colour_Green": ["--set", "Palette=Colour"],
    "Colour_Blue": ["--set", "Palette=Colour"],
    "Sensitivity": GHOSTS,
    "Bass Bins": MUSIC,
    "Treble From": MUSIC,
}

# The awkward values are load-bearing: a control swept at 0, 0.5 and 1 can land
# on the same picture.
SWEEP_VALUES = [0.0, 0.137, 0.611, 1.0]

# Discrete parameters, by the values they take (--list reports the kind but
# not the element count, so these track Controls.h by hand).
DISCRETE = {
    "Drive": [0, 1, 2],
    "Hit": [0, 1],
    "Drawings": [0, 1, 2],
    "Passes": [1, 2, 3],
    "Seed": [0, 1, 2],
    "Placement": [0, 1, 2],
    "Palette": [0, 1, 2, 3],
    "Backdrop": [0, 1, 2],
    "Bass Bins": [1, 4, 8],
    "Treble From": [4, 16, 48],
}

SKIP_KINDS = {"buffer", "text"}
SKIP_NAMES = {"About", "User guide", "Project page", "Source on GitHub", "Support the work"}
OVER_ONLY = {"Mix"}


def read_png(path):
    data = path.read_bytes()
    pos, idat = 8, b""
    while pos < len(data):
        length = int.from_bytes(data[pos:pos + 4], "big")
        if data[pos + 4:pos + 8] == b"IDAT":
            idat += data[pos + 8:pos + 8 + length]
        pos += 12 + length
    return zlib.decompress(idat)


def parameters(harness, over):
    args = [str(harness), "--list"] + (["--over"] if over else [])
    out = subprocess.run(args, capture_output=True, text=True, check=True).stdout
    found = []
    for line in out.splitlines()[1:]:
        parts = line.split()
        if len(parts) >= 4:
            found.append((" ".join(parts[1:-2]), parts[-2]))
    return found


def render(harness, tmp, over, setting, extra, index):
    out = tmp / f"sweep-{index}.png"
    args = [str(harness), "--out", str(out), "--size", SIZE] + FRAMES
    args += (["--over"] if over else []) + extra + ["--set", setting]
    result = subprocess.run(args, capture_output=True, text=True)
    if result.returncode != 0:
        raise RuntimeError(f"sctest failed: {' '.join(args)}\n{result.stderr.strip()}")
    return read_png(out)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--build", default="build")
    parser.add_argument("--verbose", action="store_true")
    parser.add_argument("--jobs", type=int, default=6)
    parser.add_argument("--no-context", action="store_true", help="empty the context table (to see what it holds up)")
    args = parser.parse_args()
    context = {} if args.no_context else CONTEXT

    harness = REPO / args.build / "sctest"
    if not harness.exists():
        print(f"no sctest at {harness} -- build first", file=sys.stderr)
        return 2

    work = []
    for name, kind in parameters(harness, False):
        if kind in SKIP_KINDS or name in SKIP_NAMES:
            continue
        work.append((False, name, kind))
    for name, kind in parameters(harness, True):
        if name in OVER_ONLY:
            work.append((True, name, kind))

    dead, checked = [], 0
    with tempfile.TemporaryDirectory() as tmp, concurrent.futures.ThreadPoolExecutor(args.jobs) as pool:
        tmp = pathlib.Path(tmp)
        jobs = {}
        index = 0
        for over, name, kind in work:
            extra = context.get(name, [])
            values = DISCRETE.get(name, SWEEP_VALUES)
            futures = []
            for value in values:
                futures.append(pool.submit(render, harness, tmp, over, f"{name}={value}", extra, index))
                index += 1
            jobs[(over, name, kind)] = futures
        for (over, name, kind), futures in jobs.items():
            frames = [f.result() for f in futures]
            if len(frames) < 2:
                continue
            checked += 1
            label = f"{name}{' (Over)' if over else ''}"
            if all(f == frames[0] for f in frames[1:]):
                dead.append(label)
                print(f"  DEAD {label}")
            elif args.verbose:
                print(f"  ok   {label}")

    print()
    if dead:
        print(f"sweep: {checked} parameters, {len(dead)} made no difference:")
        for entry in dead:
            print(f"  - {entry}")
        return 1
    print(f"sweep: {checked} parameters, all live")
    return 0


if __name__ == "__main__":
    sys.exit(main())
