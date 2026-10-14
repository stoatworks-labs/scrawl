# AGENTS.md — scrawl

Why the code is the way it is. `CLAUDE.md` has the commands; this has the reasons, the
traps that cost time, and what is measured as opposed to assumed.

## The idea

Allan asked (2026-10-13) for "a new resolume plugin which creates scribbled lines to the
music, in the style of scott pilgrim vs the world". The film draws its sound into the air:
wavy rings off the amps, impact stars, zigzags, little curls, lines that boil because they
were drawn again for every drawing.

The one mechanism is **a hand that cannot hold still, drawing every doodle again for every
drawing.** The intended path belongs to the doodle and changes only with its age; the
error belongs to the drawing (`hand::Options::drawing`) and is all that changes between two
drawings of a still doodle. Everything the look needs falls out of that, and the harness
measures it (`--hand`, `--boil`, `--twos`). The music only decides what gets drawn and
when; it is three onset detectors (bassalt's, split into bands) and the transport.

The film's lettering ("KRAKOOM") and the aura round a character are not here: the first
needs a typeface and its own design, the second is about the clip's subject, not the air.
Both are the obvious v0.2 questions.

## Shape of the code

    source/Hand.*        the mechanism: a stroke's intended centreline -> capsules, with this
                         drawing's boil, registration and pressure
    source/Doodles.*     the vocabulary (Rings, Burst, Bolt, Curl, and the Wave) and the score:
                         spawn, expire, and Build( drawing time, exposure, index )
    source/Audio.*       three-band spectral-flux onsets, a level, the wave's six groups
    source/Transport.h   Resolume's BPM and bar phase -> the eighth notes crossed this frame
    source/Scrawl.*      the plugin: clock, listening, the drawing clock, GL, the composite
    source/Shaders.*     capsules (GL_MAX), reduce, blur, composite
    source/Controls.*    parameter ids and their conversions
    tools/sctest         the harness; tools/{verify.sh,sweep.py,mutate.sh,glslc.sh}

The plugin's frame: `Advance()` (clock, `Listen()` spawns at the frame's time, expire, the
drawing index), then only if the drawing changed (or a control did, or the size)
`BuildDrawing()` + `RenderDrawing()`, then the composite. `Advance` and `BuildDrawing` need
no GL, which is how the offline checks reach the real plugin class.

## Decisions

- **Source and effect, jackpot's shape.** A source can sit on its own layer over anything
  (Backdrop None is transparent); the effect draws over its clip. FFGL resolves one
  plugMain per binary, so they are two bundles from one OBJECT core. Ids `SK01`/`SK02`
  (free across `~/Projects` and `~/dev` on 2026-10-13).
- **The CPU draws, the GPU rasterises.** The hand is easier to state, check and keep
  deterministic in double precision on the CPU, and a drawing is at most ~50,000 capsules.
  The GPU draws each as an instanced quad and computes coverage from the distance to the
  tapered capsule.
- **GL_MAX blending.** Consecutive capsules overlap at every joint; "over" would darken
  every joint and every crossing. The union is what a pen leaves. Different colours
  crossing take the brighter per channel, which reads as light. The saved GL state had no
  blend equation, so it gained one (and the program).
- **The coverage ramp is one pixel wide, box-filtered:** clamp((r − d)/px + 0.5). Across a
  line of radius r ≥ ½ px the samples sum to exactly 2r, wherever the line lands, which is
  what `--width` checks.
- **Drawings are a clock, not a filter.** k = floor(clock × rate + 1e-6); the drawing is
  made at k / rate and shown until k + 1. Held frames re-composite the same textures, so a
  held drawing is the same picture to the bit, and costs a composite.
- **A drawing shows the middle of its exposure** (age = t_k − birth + exposure / 2). Made
  at t_k exactly, a doodle born on a boundary frame had age 0 and a write-on of nothing,
  so it missed its own drawing (see the traps).
- **Doodles spawn on the frame, not the drawing.** A hit's birth is the frame it arrived;
  the drawing quantises when it is seen, not when it happened, so a write-on's progress is
  right in the drawing that shows it.
- **Bolts ride the low band**, not a full-spectrum onset (see the traps), and are rate
  limited by Bolts: a gap of 0.4 + 1.6 × (1 − Bolts) s, a bar of 1 − 0.5 × Bolts on the
  low band's strength. That limit is the music drive's only: the beat drive spawns a bolt
  on every downbeat, and there Bolts sets only the size and the fork (and 0 is none).
- **Hit** spawns one of each kind at strength 1 through `Score::Spawn`, so a kind at 0
  stays out. It is one curl even at Curls ≥ 0.67, its bolt neither waits for nor resets the
  bolt gap (`lastBolt`), and it does nothing to the wave, which is never spawned.
- **Drive Auto** follows the music while any bin has been above 1e-5 within two seconds,
  and the beat otherwise, so the plugin does something on any machine, Arena gate included
  (win-lab has no sound device).
- **Placement** gives rings a different shape each way: `(( ))` arcs when scattered (full
  circles everywhere looked like bubbles), arcs facing the room from the Speakers, and
  full, overshooting circles round the Centre.
- **Wobble 0 is a plotter**, including the pressure's wander (0.15 × min(1, 2 × Wobble)):
  that is the control's meaning, and it gives `--width` a straight line to measure. The
  wander is at its full ±15% from Wobble 0.5 (the default) up, not only at 1. The landing
  and lift tapers don't depend on Wobble, so a plotter's strokes still taper at the ends.
- **Defaults** were chosen by looking at contact sheets over Resolume's own clips and
  loops: White with Glow 0.35, Line Width 0.0065, on twos, Scatter, Rings 0.7, Bursts 0.6,
  Bolts 0.4, Curls 0.5, Wave 0.5. The first look had rings growing to 70% of the frame;
  they were shrunk to about a third.
- **About block: generated, four buttons.** `StoatworksAbout.h` comes from
  stoatworks-backend's sync-about.py (2026-10-14), with the guide's URL, so the block is the
  credit line and User guide, Project page, Source on GitHub, Support the work. Until then it
  was a hand copy with three buttons; the guide's button added `PT_ABOUT_BUTTON_4`, which
  moved Mix from id 31 to 32 before v0.1.0 shipped. Ids are append only from v0.1.0.
  Scrawl.cpp's static_assert catches the day the block's size changes again.

## The traps, in the order they cost time

1. **A full-spectrum onset is the hi-hats.** The bolt rule was first "a full-band onset
   whose strength reaches its peak". The square root that lets a hat be heard at all makes
   33 bins of hat outweigh two bins of kick in both flux and level, so every bolt landed on
   a hat (`--bands`: bolts on 14 42 70 ..., the hat frames). Bolts now come from the low
   band.
2. **An onset's strength read from the envelope is shaved by its own attack.** At 60 fps a
   10 ms attack reaches 81% on the onset frame, so equal kicks read 0.84 of their peak and
   sat on the bolt's 0.8 bar. Strength is now the band's raw level against a peak of raw
   levels: an equal hit is 1.
3. **The host's first bar phase is not a leap.** The transport starts at 0 beats; the first
   `SetBeatInfo` with a phase of 0.3 moved it to 1.2 and "crossed" two eighths on frame 0
   (`--beat` with a host phase: one extra of everything). The first alignment is silent.
4. **A doodle born on a drawing's own frame was invisible in it.** Made at t_k, its age
   was 0, and a write-on at age 0 draws nothing, so a kick exactly on a boundary showed
   a drawing late. The exposure's midpoint fixed it; `--latency` covers every frame a kick
   can land on, and the mutant 0.5 → 0.0 is caught there.
5. **Angle bins lose data.** `--hand` first binned the displaced line's points into 720
   angle bins and took each bin's variance across drawings. 393 points into 720 bins left
   bins empty in some drawings, which wrecked the estimate (ratio 3.5-9.6). The hand
   resamples by arc length, so point i is the same point of the stroke in every drawing:
   the check follows each point instead.
6. **A clean test band hides a dead control.** The sweep found Sensitivity dead: every hit
   in the synthetic band is caught at every setting. `--audio ghost` adds a soft kick
   between the kicks, which only a sensitive detector hears. The test was wrong, not the
   control.
7. **Floor at the boundary.** A clock accumulated from frame deltas lands a hair under
   k / rate at a boundary, and floor() puts the frame in the previous drawing. The clock is
   re-based from the host's own time (exactly n / 60 in the harness) and the index nudged
   by 1e-6.
8. **GL_MAX leaks.** Leaving `glBlendEquation( GL_MAX )` set would make the next effect in
   the chain draw as a union. `--state` sets the host's equation to REVERSE_SUBTRACT and
   checks it comes back.
9. **The music drive must be told apart from the beat drive.** The synthetic band's kick is
   every 28 frames (128.6 bpm) on purpose: at 30 frames it would land on the transport's
   free-running 120 bpm beats and `--bands` could not tell which drove the rings.

## Would this hold on another rasteriser, at another raster?

- `--onset`, `--bands`, `--beat`, `--hand`, `--defaults`, `--names`, `--determinism`,
  `--music`: CPU, no rasteriser. `--hand`'s tolerance (20%) is four times the estimator's
  own error with 400 drawings, from the three sinusoids' spread (the variance of 1 − cos Δ
  is ½): measured 0.4-1.5%. `--music` thresholds on a binomial tail (p < 1e-4 against the
  2.5-frame window's chance rate of 0.356), not on a measured fraction; its worst band is
  5e-6. libm differences could move an onset by a frame, which the window absorbs.
- `--twos`: exact equality of whole frames on one context, 320×180 and 640×360. A
  boundary is integer arithmetic (n × rate / 60), not a float.
- `--boil`: 640×360 and 320×180; ratio 0.982 and 0.974 here, 0.98 and 0.97 on Apple's
  software renderer. The tolerance (27%) is three times the 120-drawing estimate's own
  error (about 9%). The centroid along a ray of a box-filtered line is unbiased to a few
  hundredths of a pixel against a spread of 0.85-1.7 px.
- `--width`: 640×360 and 320×180, three widths, worst 0.0023 px. The sum of a one-pixel
  box ramp is exact for r ≥ ½ px wherever the line lands (the narrowest is 1.08 px wide at
  320×180); the residual is the stroke buffer's half-float coverage (2⁻¹¹ of a few pixels),
  and the tolerance is 0.01 px.
- `--latency`: ink or no ink, at 320×180, at every frame offset within a drawing.
- `--over-check`: exact (0.0) at both rasters: with no stroke and no glow, the composite's
  arithmetic is x + 0 × y and mix( x, x, 1 ), and `mix( x, y, 0 )` at Mix 0.
- `--resize`, `--state`: raster-independent.
- All of the GL checks pass again with `SCTEST_RENDERER=software` (verify.sh runs them).

## Mutation testing

`tools/mutate.sh` changes one character and requires the named check to fail. 8 of 8
caught (2026-10-13):

| file | mutant | caught by |
| --- | --- | --- |
| Shaders.cpp | the coverage ramp's centre, + 0.5 → + 0.6 | `--width` |
| Shaders.cpp | the capsule's quad margin, + 1.5 px → − 1.5 px | `--width` |
| Shaders.cpp | Backdrop None's test, == 0 → == 1 (paper under the clip) | `--over-check` |
| Hand.cpp | the amplitude's 0.003 → 0.004 | `--hand` |
| Audio.cpp | the primed frame's flux 0 → 9 | `--onset` |
| Controls.cpp | On Twos 12 → 13 a second | `--twos` |
| Doodles.cpp | the exposure's midpoint 0.5 → 0.0 | `--latency` |
| Scrawl.cpp | beats in a bar 4 → 3 | `--beat` |

`--negative` runs 14 wrong models (unprimed analyser, rings expected on the snare,
onsets half a sixteenth late, 120 bpm assumed, the same hand twice, a hand 1.3× the law
twice, no taper, On Ones expected, two seeds expected to agree, a drawing every frame
twice, the same hand every drawing, the radius taken for the width); all are detected.

## The sweep's context table

Hit acts only while its doodles live (20 frames); Bolts only after the first downbeat
(frame 120); Centre X/Y only under Placement Centre; the Colour only under Palette Colour;
Bass Bins and Treble From only with music on Drive Audio; Sensitivity only with a ghost
note in the music. `--no-context` makes those nine go dead.

## What is verified, and what is assumed

Verified on this Mac (Apple Silicon, 2026-10-13): everything in the README's Status table,
`verify.sh` green, `oxbow probe` and `selftest` on both universal bundles. Never loaded into
Resolume on macOS.

Windows (2026-10-14): the MSVC x64 build was gated in Resolume Arena 7.27.1 on win-lab (Mesa
llvmpipe, no GPU, no sound device): 15 passed, 0 failed, 2 skipped (audio). Every control as
declared. Live, in both runs (the second, formal, on the
release build): Backdrop and Centre Y (source); Drive and Centre Y (Over), with Opacity
live in run 1 only.
Everything else inconclusive, none dead: the picture churns between any two grabs (noise
floor 7.5 levels on the source, 4.6 on Over), the gate's blind spot for a picture that never
stands still. The expectation is plugin-bench `arena/expect/scrawl.json`. A by-eye
spot-check after it (Arena's thumbnails, one control set at a time over REST): the palettes,
Backdrop Paper, Passes 3, Wobble 0, Line Width 1, Size 1, Placement Centre and the doodle
kinds at 0 all changed the source as described, and the Over drew on the gate's carrier and
gave it back untouched at Mix 0. Over a clip of tiny native size the Over's lines came out
very fat (Line Width is in that clip's frame heights); not looked into.

Assumed, and said so in the README:

- **Resolume's 64 FFT bins.** Never measured in the fleet. `--wav` and `--music` use a
  stand-in: a 2048-sample Hann window ending at the frame's time, mono, magnitudes folded
  into 64 equal bins to Nyquist, a full-scale sine's bin about 1. Bass Bins and Treble From
  exist to re-aim the bands if Resolume's spacing differs.
- **The AV loops are 128 bpm**, from their 7.5 s length.
- **Resolume's bar phase is a downbeat**, as across the fleet.
- **The clip is premultiplied** (Resolume's DXV clips with alpha are), and the composite is
  premultiplied "over" in the clip's own code values, as Resolume's own blending is.

## Not done

- Never loaded into Resolume on macOS.
- Windows: gated in Arena 7.27.1 on win-lab, 15 passed with audio skipped, but only five
  controls separated from the churn (above), and no sound has reached it there.
- No browser demo, no OpenFX port, no presets.
- No lettering, no aura round the clip's subject (see the idea).
