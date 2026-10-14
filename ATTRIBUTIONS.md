# Attributions

Scrawl is built on other people's work. This file lists what that work is, who did
it, and what it is doing here.

It is generated — the master lists live in the `stoatworks-backend` repo and are
pushed out by `scripts/sync-attributions.py`. Edit it there, not here.

## Code we derived from other people's work

Someone else solved this first, and this project would not exist in its current form without their work.

### The onset detector — Stoatworks bassalt

<https://github.com/stoatworks-labs/bassalt>  
Licence: MIT  
Copyright: Stoatworks Labs

source/Audio.cpp is bassalt's analyser (millpond's, rosette's and macroblock's before it): spectral flux between raw frames against a running mean, a peak-normalised level, the refractory period and the primed first frame. Scrawl splits it into three bands, each with its own detector, and takes an onset's strength from the raw level.

### The source-plus-Over shape, the harness and the tools — Stoatworks jackpot

<https://github.com/stoatworks-labs/jackpot>  
Licence: MIT  
Copyright: Stoatworks Labs

Two bundles from one OBJECT core, CMakeLists.txt, GLState.h (boreal's, vectrix's and resolume-scopes' before it, with the blend equation and the program added here), Diag, the host-clock unit vote, Transport.h (honeydew's bar phase, turned into eighth notes here), the harness's rig, PNG writer, --pipe, --film and cue sheets, tools/verify.sh, tools/sweep.py, tools/mutate.sh and tools/glslc.sh, the CI and release workflows and scripts/release-lib.sh are jackpot's (polyhedral's, boreal's, downpour's and flyback's before it). The GL state round trip in sctest --state is bassalt's.

## Third-party code this project uses

Libraries, SDKs and frameworks the project is built on or bundles.

### Resolume FFGL SDK

<https://github.com/resolume/ffgl>  
Licence: BSD-3-Clause  
Copyright: FreeFrame

Vendored as a git submodule at external/ffgl (third_party/ffgl in oxbow).

The plugin ABI itself. An FFGL effect or source is defined by this SDK's headers — there is no other way to be loadable by Resolume Arena and Avenue.

### GLEW — the OpenGL Extension Wrangler Library

<https://github.com/nigels-com/glew>  
Licence: BSD-3-Clause (with Mesa 3-D and Khronos components)  
Copyright: Milan Ikits, Marcelo E. Magallon and Lev Povalahev

Arrives inside the FFGL submodule at external/ffgl/deps/glew-2.1.0. Not fetched separately.

Resolves OpenGL entry points on Windows, where the system headers stop at OpenGL 1.1.

### libpng

<http://www.libpng.org/pub/png/libpng.html>  
Licence: PNG Reference Library License (libpng)  
Copyright: the PNG Reference Library authors

Arrives inside the FFGL submodule, under the SDK's CustomThumbnail sample.

Part of the upstream SDK tree rather than something these plugins call directly — listed because it is present in the checkout.

## Work we checked ourselves against

No code was taken from these — but they were how we knew we had it right, and that is worth saying out loud.

### Minimum-jerk movement — T. Flash and N. Hogan, J. Neuroscience 5 (1985)

A pen writing a stroke on, a burst popping out and a bolt striking all move along 10t³ − 15t⁴ + 6t⁵, the minimum-jerk profile of an unconstrained reach.

### PCG — Melissa O'Neill (2014)

<https://www.pcg-random.org/>

Each doodle's random choices (where it goes, its size, its colour) come from a PCG32 stream in source/Maths.h, so a seed gives the same drawing on every machine.

### Hash functions for GPU rendering — M. Jarzynski and M. Olano, JCGT 9 (2020)

<https://jcgt.org/published/0009/03/02/>

Each drawing's hand and each doodle's seed come from PCG's output permutation used as a hash, as Jarzynski and Olano give it (source/Maths.h, used by source/Doodles.cpp).

### Fast random integer generation in an interval — Daniel Lemire (2019)

<https://arxiv.org/abs/1805.10941>

Whole-number draws (a doodle's colour, which speaker) use Lemire's multiply-shift with rejection.

## Inspirations

What this set out to be. No code, assets or binaries from any of these were used or examined — the debt is to the idea.

### Scott Pilgrim vs. the World — Edgar Wright (2010)

The hand-drawn sound effects drawn into the air over a band: rings of sound off the amps, impact stars, lines that boil. Scrawl takes the idea and none of the artwork, lettering or footage, and has no connection with the film or its makers.

## Getting this wrong

If your work is here and the description is inaccurate, the licence is wrong, or you would rather not be listed — open an issue and it will be fixed.
