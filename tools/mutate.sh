#!/usr/bin/env bash
#
# Mutation testing: change ONE character of the shipped code and require that a
# check fails. A check that still passes against a mutant was not looking at
# the code it claims to cover.
#
# One copy of the tree in a temporary directory (the FFGL SDK is symlinked, not
# copied), built arm64-only once; each mutant is applied in place, the harness
# rebuilt (only the changed file recompiles), the named check run under a time
# limit -- a mutant can hang (an inverted loop test), and a hang counts as
# caught -- and the file put back.
#
#     tools/mutate.sh
#
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
LIMIT=600

# file | the exact text | its one-character mutant | the check that must fail | what it is
# (\n and \t in the texts are a newline and a tab, for a target that needs its
# next line to be unique.)
MUTANTS=(
	"source/Shaders.cpp|float cover = clamp( ( r - d ) / PixelSize + 0.5, 0.0, 1.0 );|float cover = clamp( ( r - d ) / PixelSize + 0.6, 0.0, 1.0 );|--width|GLSL: the coverage ramp's centre, 0.5 -> 0.6"
	"source/Shaders.cpp|float r      = max( Radii.x, Radii.y ) + 1.5 * PixelSize;|float r      = max( Radii.x, Radii.y ) - 1.5 * PixelSize;|--width|GLSL: the capsule's quad, a pixel and a half wider -> narrower"
	"source/Shaders.cpp|vec4 back = BackdropMode == 0 ? vec4( 0.0 )|vec4 back = BackdropMode == 1 ? vec4( 0.0 )|--over-check|GLSL: Backdrop None, 0 -> 1 (paper under the clip)"
	"source/Hand.cpp|( 0.003 + 0.02 * std::max( 0.0, extent ) );|( 0.004 + 0.02 * std::max( 0.0, extent ) );|--hand|C++: the hand's amplitude, 0.003 -> 0.004"
	"source/Audio.cpp|		band.flux = 0.0f;|		band.flux = 9.0f;|--onset|C++: the primed first frame's flux, 0 -> 9"
	"source/Controls.cpp|	default: return 12.0;|	default: return 13.0;|--twos|C++: drawings a second on twos, 12 -> 13"
	"source/Doodles.cpp|std::max( 0.0, time - d.birth ) + 0.5 * exposure;|std::max( 0.0, time - d.birth ) + 0.0 * exposure;|--latency|C++: a drawing shows its exposure's middle, 0.5 -> 0.0"
	"source/Scrawl.cpp|const long long inBar = ( e / 2 ) % 4;|const long long inBar = ( e / 2 ) % 3;|--beat|C++: beats in a bar, 4 -> 3"
)

tree="$WORK/tree"
mkdir -p "$tree/external"
cp -R "$REPO/source" "$REPO/tools" "$REPO/cmake" "$REPO/CMakeLists.txt" "$tree/"
ln -s "$REPO/external/ffgl" "$tree/external/ffgl"
cmake -S "$tree" -B "$tree/build" -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES=arm64 >/dev/null
cmake --build "$tree/build" --target sctest -j"$(sysctl -n hw.ncpu)" >/dev/null 2>&1

caught=0
for entry in "${MUTANTS[@]}"; do
	IFS='|' read -r file original mutant check what <<<"$entry"
	cp "$tree/$file" "$WORK/original"
	python3 - "$tree/$file" "$original" "$mutant" <<'PY'
import sys, pathlib
path = pathlib.Path(sys.argv[1])
unescape = lambda s: s.replace("\\n", "\n").replace("\\t", "\t")
original, mutant = unescape(sys.argv[2]), unescape(sys.argv[3])
text = path.read_text()
if text.count(original) != 1:
    sys.exit(f"mutation target found {text.count(original)} times in {path}: '{original}'")
if len(original) != len(mutant) or sum(a != b for a, b in zip(original, mutant)) != 1:
    sys.exit("a mutant must differ by exactly one character")
path.write_text(text.replace(original, mutant))
PY

	printf '\n== mutant: %s\n' "$what"
	# The file's object and the harness go, so both ARE rebuilt: make compares
	# times to the second, and Shaders.cpp compiles in under one -- its new
	# object could share the old harness's second, the link was skipped, the
	# old binary ran and the mutant "survived" (twice, in jackpot, before this).
	rm -f "$tree/build/CMakeFiles/scrawl_core.dir/$file.o" "$tree/build/sctest"
	cmake --build "$tree/build" --target sctest -j"$(sysctl -n hw.ncpu)" >/dev/null 2>&1
	status=0
	perl -e 'alarm shift; exec @ARGV' "$LIMIT" "$tree/build/sctest" "$check" >"$WORK/log" 2>&1 || status=$?
	if [[ "$status" -eq 0 ]]; then
		printf '   FAIL  %s still PASSES -- the check does not cover this code\n' "$check"
	elif [[ "$status" -eq 142 ]]; then
		printf '   ok    %s hung past %d s against the mutant (counted as caught)\n' "$check" "$LIMIT"
		caught=$(( caught + 1 ))
	else
		printf '   ok    %s fails against the mutant:\n' "$check"
		{ grep -E '^  FAIL' "$WORK/log" || true; } | head -3 | cut -c1-150 | sed 's/^/        /'
		caught=$(( caught + 1 ))
	fi
	cp "$WORK/original" "$tree/$file"
	rm -f "$tree/build/CMakeFiles/scrawl_core.dir/$file.o"
done

printf '\nmutants: %d, caught: %d\n' "${#MUTANTS[@]}" "$caught"
[[ "$caught" -eq "${#MUTANTS[@]}" ]]
