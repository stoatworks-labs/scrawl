# scrawl

Hand-drawn doodles to the music for Resolume Arena/Avenue, as two FFGL plugins from one
core: `SW Scrawl` (`SK01`, source: the doodles over nothing, black or paper) and
`SW Scrawl Over` (`SK02`, effect: the same over the clip). Rings, bursts, bolts, curls and
a wobbly wave, set off by three onset detectors on Resolume's FFT (or by the beat), and
redrawn 24, 12 or 8 times a second by a hand model that boils every line. C++/GLSL, CMake
MODULE → two universal `.bundle`s (macOS) + Windows `.dll`s (CI, MSVC). MIT. Bundle ids
`com.stoatworks.ffgl.scrawl` and `com.stoatworks.ffgl.scrawl.over`.

Read `AGENTS.md` before touching the hand, the drawing clock, the bolt rule or the
harness's checks.

## Commands (CMake)
- Configure: `cmake -B build -DCMAKE_BUILD_TYPE=Release`
- Fast dev build: add `-DCMAKE_OSX_ARCHITECTURES=arm64`
- Build: `cmake --build build`
- Install to Resolume: `cmake --install build` (never from `~/Projects`)
- A frame of the source: `./build/sctest --out /tmp/scrawl.png` (100 frames, the beat
  driving because there is no audio)
- The effect on the harness's card with the synthetic band: `./build/sctest --over --audio demo --out /tmp/o.png`
- Real music: `--wav "/Applications/Resolume Arena/media/AV/Beat 001.wav"` (a stand-in
  for Resolume's FFT; see README Status). `--listen --wav PATH --frames 450` prints each
  band's onsets.
- List parameters: `./build/sctest --list` (`--over` for the effect's)
- Set anything by name: `--set "Palette=Neon" --set "Line Width=0.7"`; an option by index
  or name; `--set Hit=1` presses Hit
- Film: `./build/sctest --film 600 --size 1280x720 --audio demo | ffmpeg -f rawvideo -pix_fmt rgba -s 1280x720 -r 60 -i - out.mp4`
- A clip through the effect: `ffmpeg -i in.mov -vf fps=60,format=rgba -f rawvideo - | ./build/sctest --over --pipe --size WxH --wav music.wav | ffmpeg …`
  Cue lines are `frame  Parameter Name  value` (`#` comments), interpolated linearly
  between a name's cues, so a press is three cues (0, 1, 0). An unknown name exits 2 before
  any frame; a partial frame at EOF ends with exit 0; a reader that hangs up ends with
  exit 1 (SIGPIPE ignored).
- Every program as compiled, and through this Mac's driver: `./build/sctest --shaders --out DIR`

## Verify
- Everything: `tools/verify.sh` (~40 s: reserved words, reversed smoothsteps, glslc, the
  pin, a fresh universal build, both bundles through lipo/plist/codesign/oxbow probe +
  selftest, the programs through the driver, every check, the picture checks again on
  Apple's software renderer, `--offline`, the `--pipe` format, the negative controls, the
  mutants, the sweep, the bench)
- **The ears**: `--onset`, `--bands`, `--music` (Resolume's own Beat loops), `--beat`.
- **The hand**: `--hand` (the boil's law, registration, pressure), `--boil` (the same out of
  the pixels), `--width`.
- **The drawing clock**: `--twos`, `--latency`.
- **The plugin**: `--defaults`, `--names`, `--determinism`, `--over-check`, `--resize`, `--state`.
- **The checks can fail**: `--negative` (14 wrong models), `tools/mutate.sh` (8 mutants).
- What CI runs: `--offline` (the no-GL checks and their negative controls; it says loudly
  that the pixel checks were not run) and `tools/glslc.sh`.
- No dead controls: `python3 tools/sweep.py` (27 parameters; `--no-context` shows what the
  context table holds up).
- Cost: `--bench` (720p/1080p/4K, a new drawing and a held one).

## Notes
- **Lengths are frame heights**, the canvas 1 tall and `aspect` wide, origin in the middle,
  y up. Times are seconds of plugin clock (re-based from the host's time; jumps re-base).
- **The shaders decide nothing.** Every line is made by `hand::Draw` on the CPU; the GPU
  rasterises capsules with GL_MAX, blurs a quarter-size copy and composites.
- **A held drawing is the same picture to the bit**: the stroke and glow textures are made
  only when the drawing index changes (or a control, or the size), and re-composited.
- **Randomness is PCG**: a doodle's from `Hash( Seed, spawn count )`, a drawing's hand from
  `Hash( doodle, stroke, pass, drawing index )`.
- `smoothstep` with edge0 >= edge1 is undefined in GLSL; verify.sh greps for it.
- **`near`, `far`, `packed`, `noise1`-`noise4`** and the GLSL reserved words: verify.sh
  greps for all of them.
- **Arena addresses parameters by name**, lower case, spaces removed: names must be unique
  that way and no longer than 16 characters (`--names`).
- Option parameters arrive as the element's value (its index here); `OptionIndex()` clamps.
- Override `SetTextParameter` to return FF_SUCCESS for the About block, or no host can
  instantiate the plugin.
- `scrawl_core` is an OBJECT library: the registrations are file-scope constructors
  nothing references.
- Lives at `~/Projects/resolume/scrawl`. Public at github.com/stoatworks-labs/scrawl: a
  `v*` tag runs `release.yml`, and CI runs because the repo is public (the fleet's rule for
  Actions minutes).
- **Generated, do not edit:** `source/StoatworksAbout*.h` (stoatworks-backend's
  sync-about.py; the About block is the credit line and four buttons, so `PT_ABOUT_BUTTON_4`
  exists and Mix is id 32), and `ATTRIBUTIONS.md`, `.github/FUNDING.yml` and the issue forms
  once the backend's sync scripts have run on the public repo.
- The user guide is `docs/USER-GUIDE.md` (also on the website at
  stoatworks-labs.com/software/scrawl/guide/). A control change means a guide change.
- Parameter ids are **append only** from v0.1.0.

## Browser demo
- `demo/` is the page at https://scrawl-demo.stoatworks-labs.com (Cloudflare Worker
  `scrawl-demo`, a ROUTE on a proxied AAAA 100:: record -- the zone's custom domains are
  full; deleting the record takes the page dark with a green deploy). `deploy.yml` redeploys
  it on a push to main; by hand: `cf-run npx wrangler deploy`.
- `demo/shaders.js` is GENERATED: after changing `source/Shaders.cpp` run
  `python3 demo/tools/check_shaders.py --write`; verify.sh fails on drift, on a new piece
  the check does not list, and on a change to how `Programs()` joins them.
- `demo/plugin.js`'s PORT block (Maths, Controls, Hand, Doodles, Audio, Transport, the
  harness's demoSpectrum and Scrawl.cpp's clock/Listen/Advance/BuildDrawing) is held to the
  REAL plugin class by `demo/tools/crosscheck/crosscheck.sh` (verify.sh runs it; needs node
  and the submodule, no GL, ~30 s): change the C++ and the port with it. The printer splices
  demoSpectrum out of `tools/sctest/main.cpp`, so a change to the band is a change the port
  must follow. It builds the C++ `-ffp-contract=off`; the arm64 build is reported only.
- Pixels: `window.scrawlDemo.renderer` runs a fresh instance through N frames at n/60
  (`newInstance`, `SetAudio`, `SetTime`, `processFrame`), and the canvas read back compares
  with `sctest --frames N --size 960x540 --audio demo --out x.png`. Use headless Chrome with
  `--use-angle=metal`: SwiftShader's line edges come out up to ten levels apart.
- `demo/vendor/` is the shared kit: never edit it, re-vendor from
  `stoatworks-backend/resolume-demo/`.

## Not done yet
- Never loaded into Resolume on macOS.
- Windows: gated in Arena 7.27.1 on win-lab (llvmpipe, no sound): 15 passed, audio skipped;
  2 + 2 controls live (Opacity too in one run), the rest inconclusive (the doodles never
  stand still), none dead, and a by-eye spot-check of Arena's frames saw the main ones work;
  plugin-bench `arena/expect/scrawl.json`.
- No OpenFX port or presets. No lettering ("KRAKOOM") and no aura round the
  clip's subject (AGENTS.md, the idea).

## Diagnostics

`source/Diag.{h,cpp}` is a log file only, with no crash handler (this runs inside
Resolume). It records the GL vendor/renderer, a shader that failed to compile, a
framebuffer that would not complete, and the first time audio reached the FFT buffer.
`SCRAWL_LOG_DIR` overrides the folder.

    macOS    ~/Library/Logs/scrawl/scrawl.YYYY-MM-DD.log
    Windows  %LOCALAPPDATA%\scrawl\logs\scrawl.YYYY-MM-DD.log
