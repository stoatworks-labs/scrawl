#!/usr/bin/env bash
#
# The browser demo's JavaScript port of the CPU half, against the real plugin.
#
#     demo/tools/crosscheck/crosscheck.sh        exit 1 on any difference that matters
#
# printer.cpp drives the REAL plugin class (Scrawl.cpp and everything it calls,
# with the FFGL SDK) without GL through scenario.mjs's frames: the synthetic
# band, the ghost band and silence; both plugins; Hit; every Placement, Palette
# and Drawings; a moved control; a restart and a stall; 50 fps as well as 60.
# run.mjs runs the PORT block of demo/plugin.js through the same frames.
# compare.py requires the same doodles spawned on the same frames from the same
# seeds, the same onsets, the same number of segments in every drawing with the
# same ink, and every end point and radius within a millionth of a frame height.
#
# The band the page feeds is checked too: demoSpectrum is spliced out of
# tools/sctest/main.cpp into the printer, so the C++ side runs the harness's own
# text, not a copy of it.
#
# The C++ is built twice. -ffp-contract=off is the x86_64 slice's arithmetic
# (its baseline has no FMA) and what JavaScript can do, and is what must match.
# The native build (Apple clang fuses a*b+c on arm64) is compared and reported.
#
# Run by tools/verify.sh. Needs node, a C++ compiler and the FFGL submodule; no
# GL context.
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "$HERE/../../.." && pwd)"
CXX="${CXX:-c++}"

command -v node >/dev/null 2>&1 || { echo "crosscheck: node is not installed"; exit 2; }
[[ -f "$REPO/external/ffgl/CMakeLists.txt" ]] || { echo "crosscheck: FFGL SDK missing -- git submodule update --init --recursive"; exit 2; }

tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

# The harness's band, verbatim: from `constexpr int kDemoPeriod` to the end of
# demoSpectrum(). A change to it there is a change here, and the port must follow.
awk '/^constexpr int kDemoPeriod/ { on = 1 } on { print } on && /^}/ { exit }' \
	"$REPO/tools/sctest/main.cpp" > "$tmp/demo_band.h"
grep -q 'void demoSpectrum' "$tmp/demo_band.h" || { echo "FAIL demoSpectrum not found in tools/sctest/main.cpp"; exit 1; }

version="$( sed -n 's/^[[:space:]]*VERSION \([0-9.]*\)$/\1/p' "$REPO/CMakeLists.txt" | head -1 )"
sources=( "$REPO/source/Diag.cpp" "$REPO/source/Controls.cpp" "$REPO/source/Audio.cpp" "$REPO/source/Hand.cpp"
          "$REPO/source/Doodles.cpp" "$REPO/source/Shaders.cpp" "$REPO/source/Scrawl.cpp"
          "$REPO/external/ffgl/source/lib/FFGLSDK.cpp" )
flags=( -std=c++17 -O2 -DGL_SILENCE_DEPRECATION -DSCRAWL_VERSION="\"$version\"" -I "$tmp" -I "$REPO/source"
        -I "$REPO/external/ffgl/source/lib" -framework OpenGL -framework AppKit -w )

"$CXX" -ffp-contract=off "${flags[@]}" "$HERE/printer.cpp" "${sources[@]}" -o "$tmp/printer-strict"
"$CXX" "${flags[@]}" "$HERE/printer.cpp" "${sources[@]}" -o "$tmp/printer-native"

node "$HERE/scenario.mjs" > "$tmp/scenario.txt"
# The plugin's log goes to the scratch folder, not the user's Logs.
export SCRAWL_LOG_DIR="$tmp/logs"
"$tmp/printer-strict" "$tmp/cpp.bin" < "$tmp/scenario.txt" > "$tmp/cpp.txt"
"$tmp/printer-native" "$tmp/cpp-native.bin" < "$tmp/scenario.txt" > "$tmp/cpp-native.txt"
node "$HERE/run.mjs" "$tmp/js.bin" < "$tmp/scenario.txt" > "$tmp/js.txt"

if ! out=$( python3 "$HERE/compare.py" "$tmp/cpp.txt" "$tmp/js.txt" "$tmp/cpp.bin" "$tmp/js.bin" ); then
	echo "FAIL the page's port differs from the real plugin class (contraction off):"
	printf '%s\n' "$out" | sed 's/^/      /'
	exit 1
fi
echo "ok   the port against the real plugin class, contraction off (the x86_64 slice's arithmetic):"
printf '%s\n' "$out" | sed 's/^/     /'
echo "     against the plugin as built for this Mac (fused multiply-adds), reported, not required:"
python3 "$HERE/compare.py" --report "$tmp/cpp-native.txt" "$tmp/js.txt" "$tmp/cpp-native.bin" "$tmp/js.bin" 2>&1 | sed 's/^/     /' || true
