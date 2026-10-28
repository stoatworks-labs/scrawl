"""Compare printer.cpp's output (the real plugin class) with run.mjs's (the
page's port).

    python3 compare.py CPP_EVENTS JS_EVENTS CPP_SEGMENTS JS_SEGMENTS

Exit 1 on a failure. What must hold, and why it is not "identical":

  * Every line has the same tokens, and every token that is not a number in
    bits -- frame, due, the drawing index, the drive, the doodle count, how many
    were spawned, each band's onset, every doodle's kind, seed, colour, count
    and shape flags, each drawing's index and its number of segments -- is
    IDENTICAL. That is the score: what is drawn, of what kind, from what seed,
    on which frame.
  * The analyser's floats (strengths, the level) are identical too: Audio.cpp is
    float arithmetic, which the port rounds step by step as the C++ does, and
    its one libm call (expf) gives the same float in both here.
  * Doubles (the clock, a doodle's birth, anchor, direction, scale and strength)
    within DOUBLE_ULPS units in the last place. The JavaScript runs on V8's
    Math.sin, Math.cos, Math.exp and Math.pow, the plugin on Apple's libm, and
    the two disagree in the last bit of a small share of results.
  * Segments: the same count in every drawing (from the D lines), the colour
    and alpha of every one identical, and every end point and radius within
    GEOMETRY of the plugin's, in frame heights. That is the libm's last bits
    again, carried through the hand's sines and turned into floats.

The numbers are printed whether or not they pass, so a reader sees how close.
"""
import struct
import sys
from array import array

DOUBLE_ULPS = 4096          # about 1e-12 relative: libm last bits, through a few operations
GEOMETRY = 1e-6             # frame heights: a thousandth of a pixel at 1080


def ordered(bits, width):
    """Bits -> an integer whose order is the float's, for counting ulps."""
    sign = 1 << (width - 1)
    return (sign - (bits & (sign - 1))) if bits & sign else (sign + bits)


def ulps(a, b, width):
    return abs(ordered(int(a, 16), width) - ordered(int(b, 16), width))


def as_double(h):
    return struct.unpack(">d", bytes.fromhex(h))[0]


def compare_events(cpp_lines, js_lines, report=False):
    problems = []
    differing_floats = 0
    worst_double = 0
    worst_double_abs = 0.0
    doubles = differing_doubles = 0
    floats = 0
    if len(cpp_lines) != len(js_lines):
        problems.append(f"{len(cpp_lines)} lines from the plugin, {len(js_lines)} from the port")
    for n, (a, b) in enumerate(zip(cpp_lines, js_lines), 1):
        ta, tb = a.split(), b.split()
        if len(ta) != len(tb):
            problems.append(f"line {n}: plugin '{a}' / port '{b}'")
            continue
        for x, y in zip(ta, tb):
            if x.startswith("d:") and y.startswith("d:"):
                doubles += 1
                u = ulps(x[2:], y[2:], 64)
                if u:
                    differing_doubles += 1
                    worst_double = max(worst_double, u)
                    worst_double_abs = max(worst_double_abs, abs(as_double(x[2:]) - as_double(y[2:])))
                if u > DOUBLE_ULPS:
                    problems.append(f"line {n}: a double {u} ulps apart: plugin '{a}' / port '{b}'")
            elif x.startswith("f:") and y.startswith("f:"):
                floats += 1
                if x != y:
                    differing_floats += 1
                    if not report:
                        problems.append(f"line {n}: a float differs: plugin '{a}' / port '{b}'")
            elif x != y:
                problems.append(f"line {n}: plugin '{a}' / port '{b}'")
                break
        if len(problems) > 12:
            break
    stats = {
        "differing_floats": differing_floats,
        "lines": len(cpp_lines), "doubles": doubles, "differing_doubles": differing_doubles,
        "worst_double_ulps": worst_double, "worst_double_abs": worst_double_abs, "floats": floats,
        "frames": sum(1 for l in cpp_lines if l.startswith("F ")),
        "doodles": sum(1 for l in cpp_lines if l.startswith("N ")),
        "drawings": sum(1 for l in cpp_lines if l.startswith("D ")),
        "params": sum(1 for l in cpp_lines if l.startswith("P ")),
    }
    return problems, stats


def compare_segments(cpp_path, js_path, report=False):
    a, b = array("f"), array("f")
    with open(cpp_path, "rb") as f:
        a.frombytes(f.read())
    with open(js_path, "rb") as f:
        b.frombytes(f.read())
    problems = []
    if len(a) != len(b):
        return [f"{len(a) // 10} segments from the plugin, {len(b) // 10} from the port"], {}
    worst = 0.0
    exact = 0
    for i in range(0, len(a), 10):
        for c in range(6):
            d = abs(a[i + c] - b[i + c])
            if d == 0.0:
                exact += 1
            elif d > worst:
                worst = d
        for c in range(6, 10):
            if a[i + c] != b[i + c]:
                problems.append(f"segment {i // 10}: ink {tuple(a[i + 6:i + 10])} / {tuple(b[i + 6:i + 10])}")
                break
        if len(problems) > 5:
            break
    if worst > GEOMETRY and not report:
        problems.append(f"a segment's end or radius is {worst:.3g} frame heights from the plugin's (limit {GEOMETRY:g})")
    n = len(a) // 10
    return problems, {"segments": n, "geometry_exact": exact / max(1, 6 * n), "worst_geometry": worst}


def main(argv):
    report = "--report" in argv
    cpp_events, js_events, cpp_segs, js_segs = [a for a in argv if a != "--report"][:4]
    cpp_lines = open(cpp_events).read().splitlines()
    js_lines = open(js_events).read().splitlines()
    problems, ev = compare_events(cpp_lines, js_lines, report)
    seg_problems, sg = compare_segments(cpp_segs, js_segs, report)
    problems += seg_problems
    floats = (f"{ev['floats']:,} analyser floats identical" if not ev["differing_floats"]
              else f"{ev['differing_floats']:,} of {ev['floats']:,} analyser floats differ")
    print(f"{ev['params']} parameter names and defaults, {ev['frames']:,} frames, {ev['doodles']:,} doodles spawned, "
          f"{ev['drawings']:,} drawings: "
          + ("every event, seed, onset and segment count identical; " if not problems else "")
          + f"{floats}; "
          f"{ev['differing_doubles']:,} of {ev['doubles']:,} doubles differ, worst {ev['worst_double_ulps']} ulps "
          f"({ev['worst_double_abs']:.2g} absolute)")
    if sg:
        print(f"{sg['segments']:,} segments: every ink identical; {100 * sg['geometry_exact']:.3f}% of end points and radii "
              + ("identical to the bit" if sg["worst_geometry"] == 0 else f"identical to the bit, the rest within {sg['worst_geometry']:.2g} frame heights"))
    if problems:
        print("FAIL")
        for p in problems[:12]:
            print("  " + p)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
