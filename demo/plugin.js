/**
 * Scrawl — browser demo.
 *
 * Hand-drawn doodles to the music. The one idea, from `AGENTS.md`: **a hand
 * that cannot hold still, drawing every doodle again for every drawing.** The
 * intended path belongs to the doodle and changes only with its age; the error
 * belongs to the drawing. SW Scrawl (SK01) draws the doodles over nothing (or
 * black, or paper); SW Scrawl Over (SK02) draws them over the clip.
 *
 * Two halves, and they are not equally faithful here:
 *
 *   **The GPU half is the plugin's own GLSL.** `shaders.js` is the six
 *   `R"( ... )"` pieces of `source/Shaders.cpp` plus kVersion, spliced by
 *   `demo/tools/check_shaders.py --write` and never typed; the same script
 *   compares them character for character, and checks they are joined as
 *   `Programs()` joins them, and `tools/verify.sh` runs it. They run as the
 *   plugin's passes: the capsules (one instanced quad each, GL_MAX blending,
 *   into RGBA16F), the reduce to a quarter, the two blur passes, and the
 *   composite into the canvas.
 *
 *   **The CPU half is a port.** The plugin decides everything on the CPU: the
 *   three onset detectors, the transport, the score (what is spawned, where,
 *   and how each doodle's path changes with its age) and the hand that turns
 *   every path into capsules, in double; the shaders only rasterise. Here that
 *   is JavaScript, whose numbers are doubles too, between the PORT markers
 *   below: Maths.h, Controls.cpp, Hand.cpp, Doodles.cpp, Audio.cpp (in float,
 *   as the plugin keeps it), Transport.h and the CPU half of Scrawl.cpp (the
 *   clock, Listen, Advance and BuildDrawing). demo/tools/crosscheck/ (verify.sh
 *   runs it) drives the REAL plugin class without GL through a scenario and
 *   requires this block, run under node, to spawn the same doodles on the same
 *   frames and make the same capsules: against the plugin built with
 *   -ffp-contract=off they are identical to the bit (2026-10-28: 1,880 frames,
 *   292 doodles, 2,167,049 segments); the arm64 build fuses multiply-adds and
 *   lands within ~1e-7 frame heights. Four deliberate mutants of this block
 *   (the hand's amplitude, the band's kick, the snare's timing, a doodle's
 *   colour draw) each fail it.
 *
 *   **Pixels.** `window.scrawlDemo` exposes the renderer, so a headless page
 *   can run a fresh instance through N frames at n/60 and read the canvas
 *   back: against `sctest --frames N --size 960x540 --audio demo --out x.png`
 *   at the same settings, Chrome on Apple's GPU (--use-angle=metal) gave
 *   99.996-100% identical pixels, worst one level, in seven cases; SwiftShader
 *   gave line edges up to ten levels apart (the rasteriser, not the port).
 *
 * ---------------------------------------------------------------------------
 * Decisions this page made, and why
 * ---------------------------------------------------------------------------
 *
 * **The music is a synthetic band, said so.** The plugin listens to Resolume's
 * 64-bin FFT buffer, and a browser has no Resolume and no FFT parameter. Asking
 * a visitor for a microphone to show a video effect is not a trade worth
 * making, so the page feeds the plugin's Audio input with the band its own
 * harness feeds it (`sctest --audio demo`, `demoSpectrum` in
 * tools/sctest/main.cpp, ported below): a kick every 28 frames, a snare on
 * every other one, hats between, over a quiet bed. It is not anybody's music
 * and not an FFT of anything, and the page says so where it is chosen. "Audio
 * in" can also be the harness's ghost band (a soft kick between the kicks,
 * which only a sensitive detector hears: Sensitivity's context) or silence, in
 * which Drive Auto falls back to the beat after two seconds, as the plugin does.
 * The band is sampled at the page's clock, frame floor(t x 60), so at 60 fps
 * the plugin sees exactly what the harness gives it.
 *
 * **No host transport.** The plugin takes Resolume's BPM and bar phase through
 * SetBeatInfo; nothing here calls it, so the beat drive runs on the plugin's
 * own free-running 120 bpm, which is what it does under a host that sends no
 * beat.
 *
 * **Both plugins, one page.** One class with a flag in the plugin; the Over
 * declares one more control (Mix, in its own Over group). The Plugin dropdown is
 * a new instance, as it would be in Resolume: the score starts empty and every
 * control goes back to its default. `?plugin=over` opens the effect; Copy link
 * carries it, and the Audio in choice.
 *
 * **Integers and events.** Passes, Seed, Bass Bins and Treble From are
 * FF_TYPE_INTEGER, which the kit has no control for: they are dropdowns of
 * their values (Seed of 0 to 99 of its 0 to 9999). Hit is FF_TYPE_EVENT: a
 * toggle the page reads on the rising edge, as the plugin does, and releases
 * after one frame, which is what a host does with an event.
 *
 * **The clock.** The kit's: seconds accumulated from frame deltas, capped at a
 * tenth of a second a frame, handed to the port as the host's time already in
 * seconds, so the plugin's seconds-or-milliseconds vote (and its wall-clock
 * fallback while it votes) is not ported; the harness skips it the same way
 * (SetClockScaleForTest). The jump rule is the plugin's: a step back or over a
 * second is a jump that counts as a sixtieth, so Restart carries the doodles
 * on. Paused, no new frames are drawn; a control moved while paused redraws the
 * held drawing, as the plugin does when a control changes.
 *
 * **The harness's test hooks are not ported** (the frozen age, the fixed hand,
 * the flat wave, the hand's scale, a drawing every frame, the unprimed
 * analyser): they exist so `sctest --negative` has wrong models to catch, and
 * the plugin never sets them in a host.
 *
 * **The About block is not a parameter here**, as on every page in the suite,
 * and the Audio buffer is not a slider: it is what "Audio in" feeds.
 */

import { mountDemo } from './vendor/demo.js';
import { Program, PassBuffer, bindTexture } from './vendor/gl.js';
import * as S from './shaders.js';

// ==== BEGIN PORT ===========================================================
// Maths.h, Controls.cpp, Hand.cpp, Doodles.cpp, Audio.cpp, Transport.h, the
// harness's demoSpectrum and the CPU half of Scrawl.cpp, ported. No DOM and no
// GL between the two markers: demo/tools/crosscheck/run.mjs slices this block
// out by its markers, runs it under node and compares what it makes with the
// real plugin class's (printer.cpp).

const f32 = Math.fround;
/** std::clamp, std::min and std::max exactly: comparisons, so -0 stays -0. */
const clampV = (v, lo, hi) => (v < lo ? lo : hi < v ? hi : v);
const min2 = (a, b) => (b < a ? b : a);
const max2 = (a, b) => (a < b ? b : a);
/** std::lround: half away from zero (Math.round alone rounds -2.5 to -2). */
const lround = (v) => (v < 0 ? -Math.round(-v) : Math.round(v));
const kPi = 3.14159265358979323846;
const kTwo32 = 4294967296;

//---------------------------------------------------------------------------
// Maths.h
//---------------------------------------------------------------------------

const V = (x, y) => ({ x, y });
const add = (a, b) => V(a.x + b.x, a.y + b.y);
const sub = (a, b) => V(a.x - b.x, a.y - b.y);
const mul = (a, s) => V(a.x * s, a.y * s);
const Dot = (a, b) => a.x * b.x + a.y * b.y;
const Length = (a) => Math.sqrt(Dot(a, a));
const Perp = (a) => V(-a.y, a.x);
const FromAngle = (r) => V(Math.cos(r), Math.sin(r));
const Lerp = (a, b, t) => add(a, mul(sub(b, a), t));
function Rotate(a, r) {
  const c = Math.cos(r);
  const s = Math.sin(r);
  return V(a.x * c - a.y * s, a.x * s + a.y * c);
}

/** PCG's output permutation as a hash (Jarzynski and Olano, 2020). uint32 in, uint32 out. */
function Hash1(v) {
  const state = (Math.imul(v, 747796405) + 2891336453) >>> 0;
  const word = Math.imul(((state >>> ((state >>> 28) + 4)) ^ state) >>> 0, 277803737) >>> 0;
  return ((word >>> 22) ^ word) >>> 0;
}
const Hash2 = (a, b) => Hash1((a ^ Hash1((b + 0x9E3779B9) >>> 0)) >>> 0);
const Hash3 = (a, b, c) => Hash2(Hash2(a, b), c);

const M64 = (1n << 64n) - 1n;
/** PCG32 (O'Neill), with Lemire's unbiased multiply-shift for ranges. The state is a BigInt. */
class Rng {
  constructor(seed) {
    this.state = 0n;
    this.Next();
    this.state = (this.state + ((0x853C49E6748FEA9Bn ^ (BigInt(Hash1(seed)) << 32n)) ^ BigInt(seed >>> 0))) & M64;
    this.Next();
  }

  Next() {
    const old = this.state;
    this.state = (old * 6364136223846793005n + 1442695040888963407n) & M64;
    const xorshifted = Number((((old >> 18n) ^ old) >> 27n) & 0xFFFFFFFFn) >>> 0;
    const rot = Number(old >> 59n);
    return ((xorshifted >>> rot) | (xorshifted << ((32 - rot) & 31))) >>> 0;
  }

  Uniform() {
    return this.Next() * (1.0 / kTwo32);
  }

  Range(lo, hi) {
    return lo + (hi - lo) * this.Uniform();
  }

  /** Uniform in [0, n), unbiased. Exact in doubles while n < 2^21 (here n is at most 5). */
  Below(n) {
    if (n === 0) return 0;
    let m = this.Next() * n;
    let low = m % kTwo32;
    if (low < n) {
      const threshold = (kTwo32 - n) % n;
      while (low < threshold) {
        m = this.Next() * n;
        low = m % kTwo32;
      }
    }
    return Math.floor(m / kTwo32);
  }
}

function MinimumJerk(t) {
  if (t <= 0.0) return 0.0;
  if (t >= 1.0) return 1.0;
  return t * t * t * (10.0 + t * (-15.0 + 6.0 * t));
}

const Clamp = (v, lo, hi) => (v < lo ? lo : (v > hi ? hi : v));

function Step(a, b, x) {
  if (a === b) return x < a ? 0.0 : 1.0;
  const t = Clamp((x - a) / (b - a), 0.0, 1.0);
  return t * t * (3.0 - 2.0 * t);
}

//---------------------------------------------------------------------------
// Controls.h / Controls.cpp
//---------------------------------------------------------------------------

const PT = {
  DRIVE: 0, HIT: 1, DRAWINGS: 2, WOBBLE: 3, LINE_WIDTH: 4, PASSES: 5, SIZE: 6, LINGER: 7, SEED: 8,
  RINGS: 9, BURSTS: 10, BOLTS: 11, CURLS: 12, WAVE: 13,
  PLACEMENT: 14, CENTRE_X: 15, CENTRE_Y: 16,
  PALETTE: 17, COLOUR_R: 18, COLOUR_G: 19, COLOUR_B: 20, GLOW: 21, BACKDROP: 22,
  AUDIO: 23, SENSITIVITY: 24, BASS_BINS: 25, TREBLE_FROM: 26,
  ABOUT_TEXT: 27, ABOUT_BUTTON_1: 28, ABOUT_BUTTON_2: 29, ABOUT_BUTTON_3: 30, ABOUT_BUTTON_4: 31,
  SOURCE_COUNT: 32,
  MIX: 32,
  COUNT_ALL: 33,
};

/**
 * The names the constructor declares, by ParamId (the About block's left out:
 * it is not on this page). The crosscheck compares them with the plugin's.
 */
const kParamNames = [
  'Drive', 'Hit', 'Drawings', 'Wobble', 'Line Width', 'Passes', 'Size', 'Linger', 'Seed',
  'Rings', 'Bursts', 'Bolts', 'Curls', 'Wave',
  'Placement', 'Centre X', 'Centre Y',
  'Palette', 'Colour', 'Colour_Green', 'Colour_Blue', 'Glow', 'Backdrop',
  'Audio', 'Sensitivity', 'Bass Bins', 'Treble From',
  null, null, null, null, null,
  'Mix',
];

const Drive = { Auto: 0, Audio: 1, Beat: 2, Count: 3 };
const Drawings = { Ones: 0, Twos: 1, Threes: 2, Count: 3 };
const Placement = { Scatter: 0, Centre: 1, Speakers: 2, Count: 3 };
const Palette = { White: 0, Ink: 1, Neon: 2, Colour: 3, Count: 4 };
const Backdrop = { None: 0, Black: 1, Paper: 2, Count: 3 };

const kDriveNames = ['Auto', 'Audio', 'Beat'];
const kDrawingsNames = ['On Ones', 'On Twos', 'On Threes'];
const kPlacementNames = ['Scatter', 'Centre', 'Speakers'];
const kPaletteNames = ['White', 'Ink', 'Neon', 'Colour'];
const kBackdropNames = ['None', 'Black', 'Paper'];

const Clamp01f = (v) => clampV(f32(v), 0.0, 1.0);
const Geometric = (v, lo, hi) => lo * Math.pow(hi / lo, Clamp01f(v));
const InverseGeometric = (x, lo, hi) => f32(clampV(Math.log(x / lo) / Math.log(hi / lo), 0.0, 1.0));

function DrawingsPerSecond(d) {
  switch (d) {
    case Drawings.Ones: return 24.0;
    case Drawings.Threes: return 8.0;
    default: return 12.0;
  }
}
const WobbleFromParam = (v) => Clamp01f(v);
const LineWidthFromParam = (v) => Geometric(v, 0.0015, 0.02);
const ParamFromLineWidth = (x) => InverseGeometric(x, 0.0015, 0.02);
const SizeFromParam = (v) => Geometric(v, 0.5, 2.0);
const ParamFromSize = (x) => InverseGeometric(x, 0.5, 2.0);
const LingerFromParam = (v) => Geometric(v, 0.5, 2.0);
const AmountFromParam = (v) => Clamp01f(v);
const GlowFromParam = (v) => Clamp01f(v);
const SensitivityFromParam = (v) => Clamp01f(v);

function OptionIndex(value, count) {
  if (count <= 1 || !Number.isFinite(value)) return 0;
  return clampV(lround(value), 0, count - 1);
}

//---------------------------------------------------------------------------
// Hand.h / Hand.cpp: a stroke's intended centreline -> capsules, with this
// drawing's boil, registration and pressure.
//---------------------------------------------------------------------------

/** A growable list of Segments, ten floats each, as the plugin uploads them. */
class Segments {
  constructor() {
    this.data = new Float32Array(10 * 4096);
    this.count = 0;
  }

  clear() {
    this.count = 0;
  }

  push(x0, y0, x1, y1, r0, r1, red, green, blue, alpha) {
    if ((this.count + 1) * 10 > this.data.length) {
      const grown = new Float32Array(this.data.length * 2);
      grown.set(this.data);
      this.data = grown;
    }
    const d = this.data;
    const at = this.count * 10;
    // A Float32Array store is static_cast< float >: round to nearest even.
    d[at] = x0; d[at + 1] = y0; d[at + 2] = x1; d[at + 3] = y1;
    d[at + 4] = r0; d[at + 5] = r1;
    d[at + 6] = red; d[at + 7] = green; d[at + 8] = blue; d[at + 9] = alpha;
    this.count += 1;
  }
}

const kSpacing = 0.004;
const kRegistration = 0.5;
const kWavelength = [0.30, 0.13, 0.06];
const kPressureWander = 0.15;
const kPressureWavelength = 0.21;
const kRawWeight = [0.78, 0.52, 0.34];

const handUnit = (a, b) => Hash2(a, b) * (1.0 / kTwo32);

function Amplitude(wobble, extent) {
  return Clamp(wobble, 0.0, 1.0) * (0.003 + 0.02 * max2(0.0, extent));
}

function Weight(k) {
  const norm = Math.sqrt(kRawWeight[0] * kRawWeight[0] + kRawWeight[1] * kRawWeight[1] + kRawWeight[2] * kRawWeight[2]);
  return kRawWeight[k] / norm;
}

/** A stroke as Hand.h declares it, with its defaults. */
function newStroke() {
  return {
    points: [], widthScale: 1.0, from: 0.0, to: 1.0, landing: 0.04, lift: 0.08,
    dashes: 1.0, dashPeriod: 0.05, extent: 0.1, alpha: 1.0,
  };
}

function Resample(input, spacing, out, s) {
  out.length = 0;
  s.length = 0;
  if (input.length === 0) return;
  let total = 0.0;
  for (let i = 1; i < input.length; i += 1) total += Length(sub(input[i], input[i - 1]));
  if (total <= 0.0) {
    out.push(input[0]);
    s.push(0.0);
    return;
  }
  const steps = max2(1, Math.ceil(total / spacing));
  const step = total / steps;
  out.push(input[0]);
  s.push(0.0);
  let travelled = 0.0;
  let next = step;
  for (let i = 1; i < input.length; i += 1) {
    const a = input[i - 1];
    const b = input[i];
    const len = Length(sub(b, a));
    if (len <= 0.0) continue;
    while (next < travelled + len - 1e-12 && next < total - 1e-12) {
      out.push(Lerp(a, b, (next - travelled) / len));
      s.push(next);
      next += step;
    }
    travelled += len;
    if (i + 1 < input.length && Math.abs(s[s.length - 1] - travelled) > 1e-9) {
      out.push(b);
      s.push(travelled);
    }
  }
  if (Math.abs(s[s.length - 1] - total) > 1e-12) {
    out.push(input[input.length - 1]);
    s.push(total);
  } else {
    out[out.length - 1] = input[input.length - 1];
  }
}

/** hand::Draw. `o` is hand::Options: wobble, pen { width, red, green, blue, alpha }, character, drawing, pivot. */
function Draw(stroke, o, out) {
  const p = [];
  const s = [];
  Resample(stroke.points, kSpacing, p, s);
  if (p.length < 2) return 0;
  const L = s[s.length - 1];
  const from = Clamp(stroke.from, 0.0, 1.0) * L;
  const to = Clamp(stroke.to, 0.0, 1.0) * L;
  if (to - from <= 1e-9) return 0;

  const wobble = Clamp(o.wobble, 0.0, 1.0);
  const A = Amplitude(wobble, stroke.extent) * 1.0;
  const lambda = [0, 0, 0];
  const phase = [0, 0, 0];
  for (let k = 0; k < 3; k += 1) {
    lambda[k] = kWavelength[k] * (0.8 + 0.4 * handUnit(o.character, 11 + k));
    phase[k] = 2.0 * kPi * handUnit(o.drawing, 21 + k);
  }
  const regR = kRegistration * A * Math.sqrt(handUnit(o.drawing, 31));
  const regAngle = 2.0 * kPi * handUnit(o.drawing, 32);
  const offset = mul(FromAngle(regAngle), regR);
  const twist = (2.0 * handUnit(o.drawing, 33) - 1.0) * 0.25 * A / max2(stroke.extent, 0.05);
  const wander = kPressureWander * min2(1.0, 2.0 * wobble);
  const pPhase = 2.0 * kPi * handUnit(o.drawing, 34);
  const dashPhase = handUnit(o.character, 35);

  const landing = min2(stroke.landing, 0.25 * L);
  const lift = min2(stroke.lift, 0.35 * L);

  const displaced = (i) => {
    const a = p[i === 0 ? 0 : i - 1];
    const b = p[i + 1 < p.length ? i + 1 : i];
    let t = sub(b, a);
    const l = Length(t);
    t = l > 0.0 ? mul(t, 1.0 / l) : V(1.0, 0.0);
    let d = 0.0;
    for (let k = 0; k < 3; k += 1) d += Weight(k) * Math.sin(2.0 * kPi * s[i] / lambda[k] + phase[k]);
    const q = add(add(o.pivot, Rotate(sub(p[i], o.pivot), twist)), offset);
    return add(q, mul(Perp(t), A * d));
  };
  const pressure = (at) => {
    const land = landing > 0.0 ? Step(0.0, landing, at) : 1.0;
    const off = lift > 0.0 ? Step(L, L - lift, at) : 1.0;
    return land * off * (1.0 + wander * Math.sin(2.0 * kPi * at / kPressureWavelength + pPhase));
  };

  const q = [];
  const qs = [];
  for (let i = 0; i + 1 < p.length; i += 1) {
    const s0 = s[i];
    const s1 = s[i + 1];
    if (s1 < from || s0 > to) continue;
    const d0 = displaced(i);
    const d1 = displaced(i + 1);
    const span = s1 - s0;
    if (q.length === 0) {
      const t = span > 0.0 ? Clamp((from - s0) / span, 0.0, 1.0) : 0.0;
      q.push(Lerp(d0, d1, t));
      qs.push(s0 + span * t);
    }
    if (s1 <= to) {
      q.push(d1);
      qs.push(s1);
    } else {
      const t = span > 0.0 ? Clamp((to - s0) / span, 0.0, 1.0) : 0.0;
      q.push(Lerp(d0, d1, t));
      qs.push(s0 + span * t);
      break;
    }
  }

  let added = 0;
  const halfWidth = 0.5 * o.pen.width * stroke.widthScale;
  const alpha = f32(Clamp(o.pen.alpha * stroke.alpha, 0.0, 1.0));
  for (let i = 0; i + 1 < q.length; i += 1) {
    if (stroke.dashes < 1.0) {
      const mid = 0.5 * (qs[i] + qs[i + 1]) / max2(stroke.dashPeriod, 1e-6) + dashPhase;
      if (mid - Math.floor(mid) >= stroke.dashes) continue;
    }
    const r0 = halfWidth * pressure(qs[i]);
    const r1 = halfWidth * pressure(qs[i + 1]);
    if (r0 < 1e-7 && r1 < 1e-7) continue;
    out.push(q[i].x, q[i].y, q[i + 1].x, q[i + 1].y, max2(0.0, r0), max2(0.0, r1),
      o.pen.red, o.pen.green, o.pen.blue, alpha);
    added += 1;
  }
  return added;
}

//---------------------------------------------------------------------------
// Doodles.h / Doodles.cpp: the vocabulary and the score.
//---------------------------------------------------------------------------

const Kind = { Rings: 0, Burst: 1, Bolt: 2, Curl: 3 };
const kKinds = 4;
const kKindNames = ['rings', 'burst', 'bolt', 'curl'];
const kMaxDoodles = 96;
const kLife = [0.9, 0.42, 0.32, 1.0];
const kPassWidth = [1.0, 0.72, 0.55];
const kPassAlpha = [f32(1.0), f32(0.8), f32(0.65)];
const kNeon = [
  [1.0, 0.25, 0.65], [0.2, 0.9, 1.0], [1.0, 0.92, 0.2], [0.55, 1.0, 0.3], [0.68, 0.45, 1.0],
].map((c) => c.map(f32));

const Speaker = (side, aspect) => V((side !== 0 ? 0.34 : -0.34) * aspect, -0.14);

function EaseOut(u) {
  u = Clamp(u, 0.0, 1.0);
  const v = 1.0 - u;
  return 1.0 - v * v * v;
}

const Life = (kind, linger) => kLife[kind] * linger;

function RingsStrokes(d, age, s, out) {
  const u = age / Life(Kind.Rings, s.linger);
  const Sc = d.scale;
  const spread = EaseOut(u);
  const sides = (!d.fullCircle && d.extra) ? 2 : 1;
  for (let arc = 0; arc < d.count * sides; arc += 1) {
    const j = arc % d.count;
    const facing = d.direction + (arc >= d.count ? kPi : 0.0);
    const r = Sc * (0.025 + 0.035 * j) + Sc * (0.12 + 0.1 * d.strength) * spread;
    const lobes = 11.0 + 2.0 * j + (d.seed % 5);
    const wig = 0.045;
    const psi = 2.0 * kPi * handUnit(d.seed, 40 + arc);
    const st = newStroke();
    let start = 0.0;
    let span = 0.0;
    let drift = 0.0;
    if (d.fullCircle) {
      span = 2.0 * kPi + 0.35 + 0.3 * handUnit(d.seed, 50 + j);
      start = d.direction + 1.3 * j;
      drift = 0.06;
    } else {
      span = (sides === 2 ? 1.5 : 1.9) + 0.35 * handUnit(d.seed, 50 + arc);
      start = facing - 0.5 * span;
    }
    const n = max2(24, Math.ceil(span * r / 0.004));
    for (let i = 0; i <= n; i += 1) {
      const along = span * i / n;
      const th = start + along;
      const rr = r * (1.0 + drift * along / (2.0 * kPi)) * (1.0 + wig * Math.sin(lobes * th + psi));
      st.points.push(add(d.anchor, mul(FromAngle(th), rr)));
    }
    st.widthScale = Math.pow(max2(0.0, 1.0 - u), 0.8);
    if (u > 0.55) {
      st.dashes = 1.0 - 0.75 * (u - 0.55) / 0.45;
      st.dashPeriod = 0.05 * Sc;
    }
    st.to = MinimumJerk(age / 0.08);
    st.landing = 0.03;
    st.lift = 0.06;
    st.extent = r;
    out.push(st);
  }
}

function BurstStrokes(d, age, s, out) {
  const u = age / Life(Kind.Burst, s.linger);
  const R = d.scale * (0.1 + 0.08 * d.strength);
  const pop = MinimumJerk(age / 0.07);
  for (let i = 0; i < d.count; i += 1) {
    const k = i;
    const th = d.direction + 2.0 * kPi * i / d.count + (handUnit(d.seed, 60 + k) - 0.5) * 0.6 * 2.0 * kPi / d.count;
    const len = ((i & 1) !== 0 ? 0.6 : 1.0) * (0.85 + 0.3 * handUnit(d.seed, 90 + k));
    const b = R * len * (0.5 + 0.5 * pop);
    const a = R * len * (0.42 + 0.58 * Math.pow(Clamp(u, 0.0, 1.0), 1.6));
    if (b - a < 1e-4) continue;
    const st = newStroke();
    const dir = FromAngle(th);
    st.points = [add(d.anchor, mul(dir, a)), add(d.anchor, mul(dir, b))];
    st.widthScale = 1.15;
    st.landing = 0.004;
    st.lift = 0.025;
    st.extent = R;
    out.push(st);
  }
  if (d.extra) {
    const K = 8 + (d.seed % 3);
    const st = newStroke();
    const vertex = (i) => {
      const rad = ((i & 1) === 0 ? 0.36 : 0.22) * R * (0.4 + 0.6 * pop);
      return add(d.anchor, mul(FromAngle(d.direction + kPi * i / K), rad));
    };
    for (let i = 0; i <= 2 * K + 1; i += 1) st.points.push(vertex(i));
    st.points.push(Lerp(vertex(2 * K + 1), vertex(2 * K + 2), 0.4));
    st.from = MinimumJerk((u - 0.45) / 0.55);
    st.landing = 0.01;
    st.lift = 0.03;
    st.extent = 0.36 * R;
    out.push(st);
  }
}

function BoltStrokes(d, age, index, s, out) {
  const u = age / Life(Kind.Bolt, s.linger);
  // ( uint64( index ) + seed ) & 1: the parity of the sum.
  if (u > 0.45 && (((index % 2) + (d.seed & 1)) & 1) !== 0) return;
  const L = d.scale * (0.3 + 0.25 * d.strength);
  const amp = 0.05 * d.scale;
  const k = 6 + (d.seed % 3);
  const dir = FromAngle(d.direction);
  const nrm = Perp(dir);
  const st = newStroke();
  for (let i = 0; i <= k; i += 1) {
    const off = (i === 0 || i === k) ? 0.0 : ((i & 1) !== 0 ? 1.0 : -1.0) * amp * (0.5 + 0.5 * handUnit(d.seed, 70 + i));
    st.points.push(add(add(d.anchor, mul(dir, L * i / k)), mul(nrm, off)));
  }
  st.to = MinimumJerk(age / 0.04);
  st.widthScale = 1.3;
  st.landing = 0.008;
  st.lift = 0.05;
  st.extent = L;
  if (d.extra) {
    const f = 2 + (d.seed % (k - 3));
    const fdir = FromAngle(d.direction + ((d.seed & 4) !== 0 ? 0.6 : -0.6));
    const fnrm = Perp(fdir);
    const fork = newStroke();
    const root = st.points[f];
    for (let i = 0; i <= 3; i += 1) {
      fork.points.push(add(add(root, mul(fdir, 0.35 * L * i / 3.0)), mul(fnrm, i === 0 ? 0.0 : ((i & 1) !== 0 ? 0.6 : -0.6) * amp)));
    }
    fork.to = MinimumJerk((age - 0.02) / 0.04);
    fork.widthScale = 0.9;
    fork.landing = 0.004;
    fork.lift = 0.04;
    fork.extent = 0.35 * L;
    out.push(st);
    if (fork.to > 0.0) out.push(fork);
    return;
  }
  out.push(st);
}

function CurlStrokes(d, age, s, out) {
  const life = Life(Kind.Curl, s.linger);
  const Sc = d.scale;
  const origin = add(d.anchor, mul(FromAngle(d.direction), 0.1 * Sc * age));
  const handed = (d.seed & 8) !== 0 ? 1.0 : -1.0;
  const st = newStroke();
  const N = 180;
  if (d.variant === 0) {
    const n = d.count;
    const Lc = 0.3 * Sc;
    const b = 0.05 * Sc;
    const a = Lc / (2.0 * kPi * n);
    for (let i = 0; i <= N; i += 1) {
      const phi = 2.0 * kPi * n * i / N;
      const local = V(a * phi - b * Math.sin(phi), handed * b * (1.0 - Math.cos(phi)));
      st.points.push(add(origin, Rotate(local, d.direction)));
    }
  } else {
    const rmax = 0.09 * Sc;
    const phi0 = 2.0 * kPi * handUnit(d.seed, 80);
    for (let i = 0; i <= N; i += 1) {
      const v = i / N;
      st.points.push(add(origin, mul(FromAngle(phi0 + handed * 2.0 * kPi * 2.25 * v), rmax * (1.0 - 0.88 * v))));
    }
  }
  st.to = MinimumJerk(age / (0.3 * s.linger));
  st.from = MinimumJerk((age - 0.55 * life) / (0.45 * life));
  st.widthScale = 0.85;
  st.landing = 0.02;
  st.lift = 0.05;
  st.extent = 0.15 * Sc;
  out.push(st);
}

function Strokes(d, age, index, s, out) {
  switch (d.kind) {
    case Kind.Rings: RingsStrokes(d, age, s, out); break;
    case Kind.Burst: BurstStrokes(d, age, s, out); break;
    case Kind.Bolt: BoltStrokes(d, age, index, s, out); break;
    default: CurlStrokes(d, age, s, out); break;
  }
}

const kCycles = [1.5, 2.5, 4.0, 6.5, 10.0, 15.0];
const kDrift = [1.1, -1.7, 2.3, -2.9, 3.7, -4.3];

/** Score::WaveStroke: the wave's intended stroke, or null. */
function WaveStroke(s, wave, time) {
  if (!(s.wave > 0.0)) return null;
  const pressure = Step(0.02, 0.12, wave.level);
  if (pressure <= 0.0) return null;
  const H = 0.14 * s.wave * s.size;
  const y0 = s.placement === Placement.Centre ? s.centre.y : (s.placement === Placement.Speakers ? -0.14 : 0.0);
  const x0 = -0.5 * s.aspect - 0.06;
  const x1 = 0.5 * s.aspect + 0.06;
  const n = Math.ceil((x1 - x0) / 0.005);
  const out = newStroke();
  for (let i = 0; i <= n; i += 1) {
    const x = x0 + (x1 - x0) * i / n;
    let y = 0.0;
    for (let j = 0; j < 6; j += 1) {
      y += wave.levels[j]
        * Math.sin(2.0 * kPi * kCycles[j] * x / s.aspect + 2.0 * kPi * handUnit(s.seed, 100 + j) + kDrift[j] * time);
    }
    out.points.push(V(x, y0 + 0.5 * H * y));
  }
  out.widthScale = pressure;
  out.landing = 0.03;
  out.lift = 0.03;
  out.extent = 0.15;
  return out;
}

/** Score::Ink, as floats. */
function Ink(s, colour) {
  switch (s.palette) {
    case Palette.Ink: return [f32(0.05), f32(0.05), f32(0.06)];
    case Palette.Neon: return kNeon[((colour % 5) + 5) % 5];
    case Palette.Colour: return [s.colour[0], s.colour[1], s.colour[2]];
    default: return [1.0, 1.0, 1.0];
  }
}

class Score {
  constructor() {
    this.doodles = [];
    this.spawned = 0;
    this.ringsSide = 0;
  }

  Spawn(kind, now, strength, s) {
    const amount = s.amount[kind];
    if (!(amount > 0.0)) return;
    strength = Clamp(strength, 0.0, 1.0);
    const rng = new Rng(Hash3(s.seed, this.spawned, 0x5C4A3));

    const d = {
      kind, birth: now, seed: Hash2(s.seed, this.spawned), anchor: V(0.0, 0.0), direction: 0.0,
      scale: 1.0, strength, colour: 0, fullCircle: true, count: 1, extra: false, variant: 0,
    };
    d.scale = s.size * (0.65 + 0.5 * strength) * (0.7 + 0.5 * amount);
    d.colour = rng.Below(5);

    const ax = 0.4 * s.aspect;
    const ay = 0.36;
    const sx = rng.Range(-ax, ax);
    const scatter = V(sx, rng.Range(-ay, ay));
    const turn = rng.Range(0.0, 2.0 * kPi);
    const side = rng.Below(2);
    switch (kind) {
      case Kind.Rings:
        d.count = 1 + lround(2.0 * amount);
        d.direction = turn;
        if (s.placement === Placement.Speakers) {
          const from = this.ringsSide & 1;
          this.ringsSide = (this.ringsSide + 1) >>> 0;
          d.anchor = Speaker(from, s.aspect);
          d.direction = from !== 0 ? kPi : 0.0;
          d.fullCircle = false;
        } else if (s.placement === Placement.Centre) {
          d.anchor = s.centre;
        } else {
          d.anchor = scatter;
          d.fullCircle = false;
          d.extra = true;
        }
        break;
      case Kind.Burst:
        d.count = 8 + lround(6.0 * amount);
        d.extra = amount >= 0.5 && strength >= 0.5;
        d.direction = turn;
        if (s.placement === Placement.Centre) {
          const angle = rng.Range(0.0, 2.0 * kPi);
          d.anchor = add(s.centre, mul(FromAngle(angle), rng.Range(0.08, 0.2) * s.size));
        } else if (s.placement === Placement.Speakers) {
          const dx = rng.Range(-0.06, 0.06);
          d.anchor = add(Speaker(side, s.aspect), V(dx, rng.Range(0.1, 0.2)));
        } else {
          d.anchor = scatter;
        }
        break;
      case Kind.Bolt:
        d.extra = amount >= 0.5;
        if (s.placement === Placement.Speakers) {
          d.anchor = add(Speaker(side, s.aspect), V(0.0, 0.1));
          d.direction = (side !== 0 ? kPi - 0.25 : 0.25) + rng.Range(-0.45, 0.45) * (side !== 0 ? -1.0 : 1.0);
        } else if (s.placement === Placement.Centre) {
          d.direction = turn;
          d.anchor = add(s.centre, mul(FromAngle(turn), 0.05 * s.size));
        } else {
          d.direction = turn;
          d.anchor = scatter;
        }
        break;
      default:
        d.count = 2 + rng.Below(2);
        d.variant = rng.Below(3) === 0 ? 1 : 0;
        d.direction = 0.5 * kPi + rng.Range(-0.7, 0.7);
        if (s.placement === Placement.Centre) {
          const angle = rng.Range(0.0, 2.0 * kPi);
          d.anchor = add(s.centre, mul(FromAngle(angle), rng.Range(0.05, 0.18) * s.size));
        } else if (s.placement === Placement.Speakers) {
          d.anchor = add(Speaker(side, s.aspect), V(rng.Range(-0.05, 0.05), 0.12));
        } else {
          d.anchor = scatter;
        }
        break;
    }

    this.spawned = (this.spawned + 1) >>> 0;
    this.doodles.push(d);
    if (this.doodles.length > kMaxDoodles) this.doodles.shift();
  }

  Expire(now, s) {
    this.doodles = this.doodles.filter((d) => !(now - d.birth >= Life(d.kind, s.linger)));
  }

  /** Every segment of drawing `index`, made at `time` and shown for `exposure` seconds. */
  Build(time, exposure, index, s, wave, out) {
    out.clear();
    const passes = clampV(s.passes, 1, 3);
    const draw = (st, identity, pivot, colour) => {
      const rgb = Ink(s, colour);
      for (let p = 0; p < passes; p += 1) {
        const pass = Hash2(identity, p);
        Draw(st, {
          wobble: s.wobble,
          pen: { width: s.lineWidth * kPassWidth[p], alpha: kPassAlpha[p], red: rgb[0], green: rgb[1], blue: rgb[2] },
          character: identity,
          drawing: Hash2(pass, index >>> 0),
          pivot,
        }, out);
      }
    };

    const strokes = [];
    for (const d of this.doodles) {
      if (d.birth > time + 1e-6) continue;
      const age = max2(0.0, time - d.birth) + 0.5 * exposure;
      if (age >= Life(d.kind, s.linger)) continue;
      strokes.length = 0;
      Strokes(d, age, index, s, strokes);
      for (let i = 0; i < strokes.length; i += 1) draw(strokes[i], Hash2(d.seed, i), d.anchor, d.colour);
    }

    const line = WaveStroke(s, wave, time);
    if (line) draw(line, Hash2(s.seed, 0x7A7E), V(0.0, line.points[0].y), s.seed % 5);
  }
}

//---------------------------------------------------------------------------
// Audio.h / Audio.cpp: three-band spectral-flux onsets, in float as the
// plugin keeps them.
//---------------------------------------------------------------------------

const kBins = 64;
const kBands = 3;
const kWaveBands = 6;
const kPeakFloor = f32(0.02);
const kPeakTau = f32(3.0);
const kRefractory = f32(0.08);
const kFluxFloor = f32(0.004);
const kNoiseFloor = f32(1e-5);
const kWaveEdges = [0, 1, 2, 4, 8, 16, 64];
const kAttackSeconds = f32(0.010);
const kReleaseSeconds = f32(0.200);

function coefficient(dt, tau) {
  if (tau <= f32(1e-4) || dt <= 0.0) return 1.0;
  return f32(1.0 - f32(Math.exp(f32(-dt / tau))));
}
const clamp01f = (v) => (v < 0.0 ? 0.0 : (v > 1.0 ? 1.0 : v));

const newBand = () => ({ level: 0, raw: 0, peak: 0, flux: 0, fluxMean: 0, refractory: 0, fired: false, strength: 0, onsets: 0 });

class Analyser {
  constructor() {
    this.binLevel = new Float32Array(kBins);
    this.binPrevious = new Float32Array(kBins);
    this.bands = [newBand(), newBand(), newBand()];
    this.full = newBand();
    this.wave = new Float32Array(kWaveBands);
    this.sinceSound = f32(1e9);
    this.everHeard = false;
    this.primed = false;
  }

  Low() { return this.bands[0]; }
  Mid() { return this.bands[1]; }
  High() { return this.bands[2]; }

  Level() {
    return clamp01f(f32(this.full.level / max2(kPeakFloor, this.full.peak)));
  }

  WaveLevels() { return this.wave; }

  Present() { return this.sinceSound < 2.0; }

  UpdateBand(band, sum, rawSum, rise, count, dt, settings) {
    const n = max2(count, 1);
    band.level = f32(sum / n);
    band.raw = f32(rawSum / n);
    band.flux = f32(rise / n);
    if (!this.primed && settings.prime) band.flux = 0.0;

    band.peak = max2(band.raw, f32(band.peak - f32(f32(band.peak - kPeakFloor) * coefficient(dt, kPeakTau))));
    band.peak = max2(band.peak, kPeakFloor);
    band.fluxMean = f32(band.fluxMean + f32(f32(band.flux - band.fluxMean) * coefficient(dt, 1.0)));

    const margin = f32(f32(2.5) - f32(f32(2.35) * clamp01f(settings.sensitivity)));
    const bar = max2(kFluxFloor, f32(band.fluxMean * f32(1.0 + margin)));

    band.fired = false;
    band.refractory = max2(0.0, f32(band.refractory - dt));
    if (band.refractory <= 0.0 && band.flux > bar) {
      band.fired = true;
      band.refractory = kRefractory;
      band.strength = clamp01f(f32(band.raw / band.peak));
      band.onsets += 1;
    }
  }

  /** `bins` as the host handed them over (Float32Array), `dt` a float. */
  Update(bins, count, dt, settings) {
    const n = clampV(count, 0, kBins);
    const attack = coefficient(dt, kAttackSeconds);
    const release = coefficient(dt, kReleaseSeconds);
    const lowEnd = clampV(settings.bassBins, 1, kBins - 2);
    const highFrom = clampV(settings.trebleFrom, lowEnd + 1, kBins - 1);

    const sum = [0, 0, 0];
    const rawSum = [0, 0, 0];
    const rise = [0, 0, 0];
    const counted = [0, 0, 0];
    let total = 0.0;
    let totalRaw = 0.0;
    let totalRise = 0.0;
    let loudest = 0.0;
    for (let i = 0; i < kBins; i += 1) {
      const value = i < n && bins ? bins[i] : 0.0;
      loudest = max2(loudest, value);
      const raw = f32(Math.sqrt(max2(0.0, value)));

      const up = max2(0.0, f32(raw - this.binPrevious[i]));
      this.binPrevious[i] = raw;

      const level = this.binLevel[i];
      this.binLevel[i] = f32(level + f32(f32(raw - level) * (raw >= level ? attack : release)));
      const lv = this.binLevel[i];

      const b = i < lowEnd ? 0 : (i < highFrom ? 1 : 2);
      sum[b] = f32(sum[b] + lv);
      rawSum[b] = f32(rawSum[b] + raw);
      rise[b] = f32(rise[b] + up);
      counted[b] += 1;
      total = f32(total + lv);
      totalRaw = f32(totalRaw + raw);
      totalRise = f32(totalRise + up);
    }

    for (let b = 0; b < kBands; b += 1) this.UpdateBand(this.bands[b], sum[b], rawSum[b], rise[b], counted[b], dt, settings);
    this.UpdateBand(this.full, total, totalRaw, totalRise, kBins, dt, settings);

    for (let g = 0; g < kWaveBands; g += 1) {
      let s = 0.0;
      for (let i = kWaveEdges[g]; i < kWaveEdges[g + 1]; i += 1) s = f32(s + this.binLevel[i]);
      const mean = f32(s / (kWaveEdges[g + 1] - kWaveEdges[g]));
      this.wave[g] = clamp01f(f32(mean / max2(kPeakFloor, this.full.peak)));
    }

    if (loudest > kNoiseFloor) {
      this.sinceSound = 0.0;
      this.everHeard = true;
    } else {
      this.sinceSound = f32(this.sinceSound + dt);
    }
    this.primed = true;
  }
}

//---------------------------------------------------------------------------
// Transport.h: the BPM and bar phase -> the eighth notes crossed this frame.
//---------------------------------------------------------------------------

const kBeatsPerBar = 4.0;

class Transport {
  constructor() {
    this.bpm = 120.0;
    this.position = 0.0;
    this.hostBarPhase = 0.0;
    this.hasHostPhase = false;
    this.hostAligned = false;
  }

  SetBeatInfo(bpm, barPhase) {
    if (bpm > 0.0 && Number.isFinite(bpm)) this.bpm = bpm;
    if (Number.isFinite(barPhase)) {
      this.hostBarPhase = barPhase - Math.floor(barPhase);
      this.hasHostPhase = true;
    }
  }

  Advance(dt, eighths) {
    eighths.length = 0;
    const before = this.position;
    if (this.hasHostPhase) {
      const bar = Math.floor(this.position / kBeatsPerBar);
      let aligned = (bar + this.hostBarPhase) * kBeatsPerBar;
      if (aligned < this.position - 0.5 * kBeatsPerBar) aligned += kBeatsPerBar;
      else if (aligned > this.position + 0.5 * kBeatsPerBar) aligned -= kBeatsPerBar;
      if (!this.hostAligned) {
        this.hostAligned = true;
        this.position = aligned;
        this.hasHostPhase = false;
        return;
      }
      this.position = aligned;
      this.hasHostPhase = false;
    } else if (dt > 0.0) {
      this.position += dt * this.bpm / 60.0;
    }

    if (this.position > before) {
      let first = Math.floor(before * 2.0) + 1;
      const last = Math.floor(this.position * 2.0);
      if (last - first >= 8) first = last - 7;
      for (let e = first; e <= last; e += 1) eighths.push(e);
    }
  }

  SinceBeat() {
    return (this.position - Math.floor(this.position)) * 60.0 / this.bpm;
  }
}

//---------------------------------------------------------------------------
// The harness's synthetic band: demoSpectrum() in tools/sctest/main.cpp, what
// `sctest --audio demo` (and `--audio ghost`) feeds the Audio input. A kick
// every `period` frames (bins 0-1), a snare on every second one (bins 3-12), a
// hi-hat half-way between kicks (bins 18-50), over a quiet bed in every bin.
// 28 frames is 128.6 bpm at 60 fps, on purpose not the transport's 120.
//---------------------------------------------------------------------------

const kFps = 60.0;
const kDemoPeriod = 28;

function demoSpectrum(frame, out, period = kDemoPeriod, bed = true, ghost = false) {
  for (let i = 0; i < kBins; i += 1) out[i] = bed ? 0.0004 : 0.0;
  if (frame < 0) return;
  const sinceKick = (frame % period) / kFps;
  const sinceSnare = ((frame + period) % (2 * period)) / kFps;
  const sinceHat = ((frame + Math.trunc(period / 2)) % period) / kFps;
  const kick = f32(Math.exp(-sinceKick / 0.12));
  const snare = f32(Math.exp(-sinceSnare / 0.1));
  const hat = f32(Math.exp(-sinceHat / 0.04));
  // `out` is a Float32Array, so each += rounds to float on the store, as the
  // C++'s float += float does; each product is rounded first, as 0.5f * kick is.
  out[0] += f32(f32(0.5) * kick);
  out[1] += f32(f32(0.35) * kick);
  if (ghost) {
    const at = Math.trunc((2 * period) / 3);
    if (frame % period >= at) {
      const g = f32(f32(0.05) * f32(Math.exp(-((frame % period) - at) / kFps / 0.12)));
      out[0] += f32(f32(0.5) * g);
      out[1] += f32(f32(0.35) * g);
    }
  }
  for (let i = 3; i <= 12; i += 1) out[i] += f32(f32(0.06) * snare);
  for (let i = 18; i <= 50; i += 1) out[i] += f32(f32(0.012) * hat);
}

//---------------------------------------------------------------------------
// Scrawl.h / Scrawl.cpp: the plugin's CPU half. params[] by ParamId, the
// clock, Listen, Advance and BuildDrawing. ProcessOpenGL's GL calls are below
// the END PORT marker.
//---------------------------------------------------------------------------

const kMaxJump = 1.0;
const kFrameGuess = 1.0 / 60.0;
const kBoundaryNudge = 1e-6;
const kGlowSigma = 0.012;
const kGlowGain = f32(3.0);

/** The constructor's defaults, as the floats params[] holds. */
function DefaultParams() {
  const p = new Float32Array(PT.COUNT_ALL);
  p[PT.DRIVE] = Drive.Auto;
  p[PT.DRAWINGS] = Drawings.Twos;
  p[PT.WOBBLE] = 0.5;
  p[PT.LINE_WIDTH] = ParamFromLineWidth(0.0065);
  p[PT.PASSES] = 1.0;
  p[PT.SIZE] = ParamFromSize(1.0);
  p[PT.LINGER] = 0.5;
  p[PT.SEED] = 0.0;
  p[PT.RINGS] = 0.7;
  p[PT.BURSTS] = 0.6;
  p[PT.BOLTS] = 0.4;
  p[PT.CURLS] = 0.5;
  p[PT.WAVE] = 0.5;
  p[PT.PLACEMENT] = Placement.Scatter;
  p[PT.CENTRE_X] = 0.5;
  p[PT.CENTRE_Y] = 0.5;
  p[PT.PALETTE] = Palette.White;
  p[PT.COLOUR_R] = 1.0;
  p[PT.COLOUR_G] = 0.3;
  p[PT.COLOUR_B] = 0.7;
  p[PT.GLOW] = 0.35;
  p[PT.BACKDROP] = Backdrop.None;
  p[PT.SENSITIVITY] = 0.6;
  p[PT.BASS_BINS] = 2.0;
  p[PT.TREBLE_FROM] = 16.0;
  p[PT.MIX] = 1.0;
  return p;
}

class ScrawlFrames {
  constructor(isEffect) {
    this.isEffect = isEffect;
    this.params = DefaultParams();
    this.bins = new Float32Array(kBins);

    this.hostTime = -1.0;
    this.now = 0.0;
    this.lastNow = -1.0;
    this.clock = 0.0;
    this.clockBase = 0.0;
    this.nowBase = 0.0;
    this.lastClock = -1.0;

    this.transport = new Transport();
    this.eighths = [];
    this.analyser = new Analyser();
    this.audioDriving = false;
    this.hitPresses = 0;
    this.hitsTaken = 0;
    this.hitHeld = false;
    this.lastBolt = -1e9;

    this.score = new Score();
    this.drawing = -1;
    this.drawingsMade = 0;
    this.dirty = true;
    this.framesSeen = 0;
    this.waveNow = { levels: new Float32Array(6), level: 0.0 };
    this.waveDrawn = { levels: new Float32Array(6), level: 0.0 };
    this.segments = new Segments();
  }

  ParamCount() {
    return this.isEffect ? PT.COUNT_ALL : PT.SOURCE_COUNT;
  }

  /** SetFloatParameter, without the About block (not on this page). */
  SetFloatParameter(index, value) {
    if (index >= this.ParamCount()) return;
    const v = f32(value);
    if (index === PT.HIT) {
      const down = v >= 0.5;
      if (down && !this.hitHeld) this.hitPresses += 1;
      this.hitHeld = down;
    }
    const previous = this.params[index];
    this.params[index] = v;
    if (v !== previous && index !== PT.HIT && index !== PT.MIX) this.dirty = true;
  }

  /** SetParamElementValue( PT_AUDIO, i, bins[ i ] ) for every bin. */
  SetAudio(bins) {
    for (let i = 0; i < kBins; i += 1) this.bins[i] = bins[i];
  }

  /** SetTime. The host's unit is seconds here, so this is UpdateClock with clockScale 1. */
  SetTime(time) {
    this.hostTime = time;
  }

  Settings(aspect) {
    const P = this.params;
    return {
      aspect,
      size: SizeFromParam(P[PT.SIZE]),
      linger: LingerFromParam(P[PT.LINGER]),
      amount: [AmountFromParam(P[PT.RINGS]), AmountFromParam(P[PT.BURSTS]), AmountFromParam(P[PT.BOLTS]), AmountFromParam(P[PT.CURLS])],
      wave: AmountFromParam(P[PT.WAVE]),
      placement: OptionIndex(P[PT.PLACEMENT], Placement.Count),
      centre: V((Clamp(P[PT.CENTRE_X], 0.0, 1.0) - 0.5) * aspect, Clamp(P[PT.CENTRE_Y], 0.0, 1.0) - 0.5),
      seed: clampV(lround(P[PT.SEED]), 0, 9999) >>> 0,
      palette: OptionIndex(P[PT.PALETTE], Palette.Count),
      colour: [clampV(P[PT.COLOUR_R], 0.0, 1.0), clampV(P[PT.COLOUR_G], 0.0, 1.0), clampV(P[PT.COLOUR_B], 0.0, 1.0)],
      wobble: WobbleFromParam(P[PT.WOBBLE]),
      lineWidth: LineWidthFromParam(P[PT.LINE_WIDTH]),
      passes: clampV(lround(P[PT.PASSES]), 1, 3),
    };
  }

  Listen(dt, aspect) {
    const P = this.params;
    const s = this.Settings(aspect);
    const analyser = this.analyser;
    const score = this.score;
    const clock = this.clock;

    analyser.Update(this.bins, kBins, f32(dt), {
      sensitivity: f32(SensitivityFromParam(P[PT.SENSITIVITY])),
      bassBins: clampV(lround(P[PT.BASS_BINS]), 1, 8),
      trebleFrom: clampV(lround(P[PT.TREBLE_FROM]), 4, 48),
      prime: true,
    });

    this.transport.Advance(dt, this.eighths);

    const drive = OptionIndex(P[PT.DRIVE], Drive.Count);
    this.audioDriving = drive === Drive.Audio || (drive === Drive.Auto && analyser.Present());
    const curlsTwice = s.amount[Kind.Curl] >= 0.67 ? 1 : 0;

    if (this.audioDriving) {
      if (analyser.Low().fired) score.Spawn(Kind.Rings, clock, analyser.Low().strength, s);
      if (analyser.Mid().fired) score.Spawn(Kind.Burst, clock, analyser.Mid().strength, s);
      if (analyser.High().fired) {
        for (let i = 0; i <= curlsTwice; i += 1) score.Spawn(Kind.Curl, clock, analyser.High().strength, s);
      }
      const boltAmount = s.amount[Kind.Bolt];
      const bar = 1.0 - 0.5 * boltAmount;
      const gap = 0.4 + 1.6 * (1.0 - boltAmount);
      if (analyser.Low().fired && analyser.Low().strength >= bar && clock - this.lastBolt >= gap) {
        score.Spawn(Kind.Bolt, clock, analyser.Low().strength, s);
        this.lastBolt = clock;
      }
      const levels = analyser.WaveLevels();
      for (let j = 0; j < 6; j += 1) this.waveNow.levels[j] = levels[j];
      this.waveNow.level = analyser.Level();
    } else {
      for (const e of this.eighths) {
        if (e % 2 === 0) {
          const inBar = Math.floor(e / 2) % 4;
          score.Spawn(Kind.Rings, clock, inBar === 0 ? 0.9 : 0.6, s);
          if (inBar === 1 || inBar === 3) score.Spawn(Kind.Burst, clock, 0.75, s);
          if (inBar === 0) score.Spawn(Kind.Bolt, clock, 0.85, s);
        } else {
          for (let i = 0; i <= curlsTwice; i += 1) score.Spawn(Kind.Curl, clock, 0.5, s);
        }
      }
      const env = f32(Math.exp(-this.transport.SinceBeat() / 0.18));
      const L = this.waveNow.levels;
      L[0] = env;
      L[1] = f32(0.75) * env;
      L[2] = f32(0.35) + f32(f32(0.35) * env);
      L[3] = f32(0.25) + f32(f32(0.2) * env);
      L[4] = 0.18;
      L[5] = 0.12;
      this.waveNow.level = f32(f32(0.4) + f32(f32(0.6) * env));
    }

    while (this.hitsTaken < this.hitPresses) {
      this.hitsTaken += 1;
      for (let k = 0; k < kKinds; k += 1) score.Spawn(k, clock, 1.0, s);
    }
    score.Expire(clock, s);
  }

  /** The frame's events, without GL. True when a new drawing is due. */
  Advance(aspect) {
    this.now = this.hostTime >= 0.0 ? this.hostTime : 0.0;
    if (this.lastNow < 0.0 || this.now < this.lastNow || this.now - this.lastNow > kMaxJump) {
      this.clockBase = this.lastClock < 0.0 ? 0.0 : this.lastClock + kFrameGuess;
      this.nowBase = this.now;
    }
    this.clock = this.clockBase + (this.now - this.nowBase);
    const dt = this.lastClock < 0.0 ? 0.0 : max2(0.0, this.clock - this.lastClock);
    this.lastNow = this.now;
    this.lastClock = this.clock;

    this.Listen(dt, aspect);

    const rate = DrawingsPerSecond(OptionIndex(this.params[PT.DRAWINGS], Drawings.Count));
    const k = Math.floor(this.clock * rate + kBoundaryNudge);
    this.framesSeen += 1;
    const changed = k !== this.drawing;
    if (changed) {
      this.drawing = k;
      this.waveDrawn = { levels: Float32Array.from(this.waveNow.levels), level: this.waveNow.level };
    }
    return changed || this.dirty;
  }

  BuildDrawing(aspect) {
    const rate = DrawingsPerSecond(OptionIndex(this.params[PT.DRAWINGS], Drawings.Count));
    const time = this.drawing / rate;
    this.score.Build(time, 1.0 / rate, this.drawing, this.Settings(aspect), this.waveDrawn, this.segments);
    this.dirty = false;
    this.drawingsMade += 1;
  }

  /** The composite's uniforms that come from params[] (ProcessOpenGL). */
  Composite() {
    const P = this.params;
    return {
      backdrop: OptionIndex(P[PT.BACKDROP], Backdrop.Count),
      glowGain: f32(kGlowGain * f32(GlowFromParam(P[PT.GLOW]))),
      glowOn: GlowFromParam(P[PT.GLOW]) > 0.0,
      strength: this.isEffect ? clampV(P[PT.MIX], 0.0, 1.0) : 1.0,
    };
  }
}

// ==== END PORT =============================================================

//===========================================================================
// The GL half: the plugin's buffers, passes and draws (RenderDrawing and
// ProcessOpenGL's GL calls), with what the PORT block decided.
//===========================================================================

function createRenderer(gl, screenQuad) {
  // Programs(): every stage is Assemble( { kVersion, piece } ), and the three
  // full-frame passes share the quad's vertex stage.
  const quad = S.VERSION + S.QUAD_VERTEX;
  const programs = {
    strokes: new Program(gl, S.VERSION + S.STROKE_VERTEX, S.VERSION + S.STROKE_FRAGMENT, 'strokes', { attribs: { Ends: 0, Radii: 1, Ink: 2 } }),
    reduce: new Program(gl, quad, S.VERSION + S.REDUCE, 'reduce'),
    blur: new Program(gl, quad, S.VERSION + S.BLUR, 'blur'),
    composite: new Program(gl, quad, S.VERSION + S.COMPOSITE, 'composite'),
  };

  // InitGL: no per-vertex data (the corner is gl_VertexID), three per-instance
  // attributes from ten packed floats.
  const segmentVao = gl.createVertexArray();
  const segmentVbo = gl.createBuffer();
  gl.bindVertexArray(segmentVao);
  gl.bindBuffer(gl.ARRAY_BUFFER, segmentVbo);
  gl.bufferData(gl.ARRAY_BUFFER, 4096 * 40, gl.DYNAMIC_DRAW);
  const stride = 40;
  gl.enableVertexAttribArray(0);
  gl.vertexAttribPointer(0, 4, gl.FLOAT, false, stride, 0);
  gl.vertexAttribDivisor(0, 1);
  gl.enableVertexAttribArray(1);
  gl.vertexAttribPointer(1, 2, gl.FLOAT, false, stride, 16);
  gl.vertexAttribDivisor(1, 1);
  gl.enableVertexAttribArray(2);
  gl.vertexAttribPointer(2, 4, gl.FLOAT, false, stride, 24);
  gl.vertexAttribDivisor(2, 1);
  gl.bindVertexArray(null);
  gl.bindBuffer(gl.ARRAY_BUFFER, null);
  let segmentCapacity = 4096;

  // EnsureTargets: the strokes at full size (nearest), the glow's two at a
  // quarter (linear), all RGBA16F.
  const strokeTarget = new PassBuffer(gl, { filter: 'nearest' });
  const glowA = new PassBuffer(gl, { filter: 'linear' });
  const glowB = new PassBuffer(gl, { filter: 'linear' });

  let instance = null;
  let instanceVariant = null;
  let drawn = false;
  let drawnWidth = 0;
  let drawnHeight = 0;
  let stats = null;
  const bins = new Float32Array(kBins);

  function renderDrawing(st, width, height) {
    const aspect = width / height;
    const seg = st.segments;

    gl.bindBuffer(gl.ARRAY_BUFFER, segmentVbo);
    if (seg.count > segmentCapacity) {
      segmentCapacity = seg.count + Math.floor(seg.count / 2);
      gl.bufferData(gl.ARRAY_BUFFER, segmentCapacity * 40, gl.DYNAMIC_DRAW);
    }
    if (seg.count > 0) gl.bufferSubData(gl.ARRAY_BUFFER, 0, seg.data, 0, seg.count * 10);
    gl.bindBuffer(gl.ARRAY_BUFFER, null);

    strokeTarget.bind();
    gl.clearColor(0, 0, 0, 0);
    gl.clear(gl.COLOR_BUFFER_BIT);
    if (seg.count > 0) {
      const p = programs.strokes.use();
      gl.uniform2f(p.location('Canvas'), f32(aspect), 1.0);
      gl.uniform1f(p.location('PixelSize'), 1.0 / height);
      gl.enable(gl.BLEND);
      gl.blendEquation(gl.MAX);
      gl.bindVertexArray(segmentVao);
      gl.drawArraysInstanced(gl.TRIANGLE_STRIP, 0, 4, seg.count);
      gl.bindVertexArray(null);
      gl.blendEquation(gl.FUNC_ADD);
      gl.disable(gl.BLEND);
    }

    if (st.Composite().glowOn) {
      const qw = glowA.width;
      const qh = glowA.height;
      glowA.bind();
      const r = programs.reduce.use();
      gl.uniform1i(r.location('Source'), 0);
      gl.uniform2i(r.location('SourceSize'), width, height);
      bindTexture(gl, 0, strokeTarget.texture);
      screenQuad.draw();

      const sigma = f32(Clamp(kGlowSigma * qh, 0.5, 8.0));
      const b = programs.blur.use();
      gl.uniform1i(b.location('Source'), 0);
      gl.uniform2i(b.location('SourceSize'), qw, qh);
      gl.uniform1f(b.location('Sigma'), sigma);
      glowB.bind();
      gl.uniform2f(b.location('Direction'), 1.0, 0.0);
      bindTexture(gl, 0, glowA.texture);
      screenQuad.draw();
      glowA.bind();
      gl.uniform2f(b.location('Direction'), 0.0, 1.0);
      bindTexture(gl, 0, glowB.texture);
      screenQuad.draw();
      bindTexture(gl, 0, null);
    }
    gl.useProgram(null);
    drawn = true;
    drawnWidth = width;
    drawnHeight = height;
  }

  /** One ProcessOpenGL. `input` is the clip (the Over), premultiplied, as the kit hands it. */
  function processFrame(st, input, width, height, target = null) {
    const isEffect = st.isEffect;
    const w = isEffect ? input.width : width;
    const h = isEffect ? input.height : height;
    const aspect = w / h;
    gl.disable(gl.BLEND);

    const due = st.Advance(aspect);

    strokeTarget.ensure(w, h, gl.RGBA16F);
    const qw = Math.floor((w + 3) / 4);
    const qh = Math.floor((h + 3) / 4);
    glowA.ensure(qw, qh, gl.RGBA16F);
    glowB.ensure(qw, qh, gl.RGBA16F);
    if (due || !drawn || drawnWidth !== w || drawnHeight !== h) {
      st.BuildDrawing(aspect);
      renderDrawing(st, w, h);
    }

    const c = st.Composite();
    gl.bindFramebuffer(gl.FRAMEBUFFER, target);
    gl.viewport(0, 0, w, h);
    const p = programs.composite.use();
    gl.uniform1i(p.location('Clip'), 0);
    gl.uniform1i(p.location('Strokes'), 1);
    gl.uniform1i(p.location('Glow'), 2);
    gl.uniform2f(p.location('ClipMax'), 1.0, 1.0);
    gl.uniform2f(p.location('GlowMax'), f32(w) / f32(4 * glowA.width), f32(h) / f32(4 * glowA.height));
    gl.uniform1i(p.location('HasClip'), isEffect ? 1 : 0);
    gl.uniform1i(p.location('BackdropMode'), c.backdrop);
    gl.uniform1f(p.location('GlowGain'), c.glowGain);
    gl.uniform1f(p.location('Strength'), c.strength);
    bindTexture(gl, 0, isEffect ? input.texture : strokeTarget.texture);
    bindTexture(gl, 1, strokeTarget.texture);
    bindTexture(gl, 2, glowA.texture);
    screenQuad.draw();
    for (let unit = 2; unit >= 0; unit -= 1) bindTexture(gl, unit, null);
    gl.activeTexture(gl.TEXTURE0);
    gl.useProgram(null);
  }

  return {
    get stats() { return stats; },
    get instance() { return instance; },
    programs,
    processFrame,
    newInstance: (isEffect) => new ScrawlFrames(isEffect),

    render({ input, params, width, height, time, variant }) {
      const isEffect = variant === 'over';
      if (!instance || instanceVariant !== variant) {
        instance = new ScrawlFrames(isEffect);
        instanceVariant = variant;
        drawn = false;
      }
      const st = instance;

      // What a host does before ProcessOpenGL: every parameter, the FFT buffer
      // and the time. A parameter that has not changed changes nothing.
      for (const p of PARAMS) {
        if (p.only && p.only !== variant) continue;
        st.SetFloatParameter(p.slot, hostValue(p, params.get(p.id)));
      }
      const hitDown = params.get('hit') > 0.5;

      const feed = audioIn.current;
      if (feed === 'none') bins.fill(0);
      else demoSpectrum(Math.floor(time * kFps + 1e-6), bins, kDemoPeriod, true, feed === 'ghost');
      st.SetAudio(bins);
      st.SetTime(time);

      processFrame(st, input, width, height);

      // A host lets go of an event; the kit has none, so the page does.
      if (hitDown) params.set('hit', 0);

      const counts = [0, 0, 0, 0];
      for (const d of st.score.doodles) counts[d.kind] += 1;
      stats = {
        drawing: st.drawing,
        made: st.drawingsMade,
        doodles: st.score.doodles.length,
        counts,
        spawned: st.score.spawned,
        segments: st.segments.count,
        audioDriving: st.audioDriving,
        present: st.analyser.Present(),
        clock: st.clock,
      };
    },
  };
}

//===========================================================================
// The parameters: the constructor's, in ParamId order and groups, with the
// plugin's names and defaults. `only` says which plugin declares it.
//===========================================================================

/** "Audio in": what the page feeds the plugin's Audio buffer. Not a plugin control. */
const AUDIO_IN = [
  { id: 'demo', name: 'Synthetic band', hint: 'The harness’s synthetic band (sctest --audio demo): a kick every 28 frames at 60 fps, a snare on every other one, hats between, over a quiet bed. Not your music, and not an FFT of anything.' },
  { id: 'ghost', name: 'Synthetic band + ghost kick', hint: 'The same band with a soft kick between the kicks (sctest --audio ghost), which only a sensitive detector hears: this is where Sensitivity shows.' },
  { id: 'none', name: 'Silence', hint: 'Nothing in the Audio input. Drive Auto falls back to the beat two seconds later (the plugin’s own 120 bpm here; there is no host transport); Drive Audio draws nothing.' },
];
const query = new URLSearchParams(window.location.search);
const audioIn = { current: AUDIO_IN.some((a) => a.id === query.get('audio')) ? query.get('audio') : 'demo' };
const initialVariant = query.get('plugin') === 'over' ? 'over' : 'source';

const kDefaults = DefaultParams();
const range = (lo, hi) => Array.from({ length: hi - lo + 1 }, (_, i) => lo + i);
/** FF_TYPE_INTEGER values on the page: the dropdown's slot -> the host's integer. */
const INTEGERS = {
  passes: range(1, 3),
  seed: range(0, 99),
  bassBins: range(1, 8),
  trebleFrom: range(4, 48),
};

const fix = (n, d = 2) => n.toFixed(d);

/** id, ParamId, FFGL type, display, hint, elements, only. Hints from docs/USER-GUIDE.md. */
const SPEC = [
  ['drive', PT.DRIVE, 'option', null, 'What sets the doodles off. Audio: the three onset detectors on the Audio input. Beat: the tempo (rings every beat, a burst on two and four, a bolt on the one, curls on the “and”s). Auto: the music while any sound has reached the input in the last two seconds, the beat otherwise.', kDriveNames],
  ['hit', PT.HIT, 'event', null, 'One of each doodle at full strength, on the press, whatever Drive is doing. FF_TYPE_EVENT in the plugin; a toggle the page releases after one frame here.'],
  ['drawings', PT.DRAWINGS, 'option', null, '24, 12 or 8 new drawings a second. Each drawing is held, the same picture to the bit, until the next.', kDrawingsNames],
  ['wobble', PT.WOBBLE, 'standard', (v) => fix(WobbleFromParam(v)), 'How unsteady the hand is. It sets the boil and the registration error, A = Wobble × (0.003 + 0.02 × extent) frame heights, and the pressure’s wander. At 0 the hand is a plotter.'],
  ['lineWidth', PT.LINE_WIDTH, 'standard', (v) => `${fix(1080 * LineWidthFromParam(v), 1)} px@1080`, 'The pen’s full width, 0.0015 to 0.02 of the frame’s height, geometrically.'],
  ['passes', PT.PASSES, 'integer', null, 'Each extra pass goes over every line again with its own hand error, thinner and lighter. FF_TYPE_INTEGER 1–3 in the plugin; a dropdown here.'],
  ['size', PT.SIZE, 'standard', (v) => `×${fix(SizeFromParam(v))}`, '0.5× to 2× every doodle and the wave’s height, geometrically. It does not change Line Width.'],
  ['linger', PT.LINGER, 'standard', (v) => `×${fix(LingerFromParam(v))}`, '0.5× to 2× every doodle’s lifetime. At 1× rings live 0.9 s, a burst 0.42 s, a bolt 0.32 s, a curl 1 s.'],
  ['seed', PT.SEED, 'integer', null, 'Where each doodle lands, which way it faces, its shape, its colour under Neon and its hand’s character. FF_TYPE_INTEGER 0–9999 in the plugin; 0–99 here.'],

  ['rings', PT.RINGS, 'standard', (v) => fix(AmountFromParam(v)), 'How many rings, set off by the kick (the low band). One line a set below 0.25, two from 0.25, three from 0.75.'],
  ['bursts', PT.BURSTS, 'standard', (v) => fix(AmountFromParam(v)), 'Impact spikes, set off by the snare (the middle band); from 0.5, a hard hit gets a jagged star round it.'],
  ['bolts', PT.BOLTS, 'standard', (v) => fix(AmountFromParam(v)), 'Zigzag bolts on a kick as hard as the hardest lately: a strength of 1 − 0.5 × Bolts, no sooner than 0.4 + 1.6 × (1 − Bolts) s after the last. A fork from 0.5.'],
  ['curls', PT.CURLS, 'standard', (v) => fix(AmountFromParam(v)), 'Loop-the-loops and spirals off the hi-hats (the high band); two a hat from 0.67.'],
  ['wave', PT.WAVE, 'standard', (v) => fix(AmountFromParam(v)), 'The line across the frame whose wiggle is the spectrum: six sines, one per slice of the bins. Gone in silence.'],

  ['placement', PT.PLACEMENT, 'option', null, 'Scatter: anywhere in the middle of the frame, rings as (( )) arcs. Centre: everything from Centre X/Y, rings as full circles that never quite close. Speakers: two stacks either side and low, rings as arcs facing the room.', kPlacementNames],
  ['centreX', PT.CENTRE_X, 'standard', (v) => fix(v), 'Where Centre is across the frame, 0 to 1. Placement Centre only.'],
  ['centreY', PT.CENTRE_Y, 'standard', (v) => fix(v), 'Where Centre is up the frame, 0 to 1. Placement Centre only.'],

  ['palette', PT.PALETTE, 'option', null, 'White, Ink (a near-black, for paper), Neon (each doodle one of five colours) or Colour (everything in the Colour below). Crossing lines take the brighter of each channel.', kPaletteNames],
  ['colour', PT.COLOUR_R, 'colour', null, 'The ink under Palette Colour.'],
  ['colourGreen', PT.COLOUR_G, 'colour', null, 'The ink under Palette Colour.'],
  ['colourBlue', PT.COLOUR_B, 'colour', null, 'The ink under Palette Colour.'],
  ['glow', PT.GLOW, 'standard', (v) => fix(GlowFromParam(v)), 'A soft halo under the lines: a quarter-size copy of the drawing, blurred by 1.2% of the frame’s height. 0 switches it off.'],
  ['backdrop', PT.BACKDROP, 'option', null, 'None (nothing behind the lines: the source is transparent), Black or Paper. On the Over it goes under the clip.', kBackdropNames],

  ['sensitivity', PT.SENSITIVITY, 'standard', (v) => `×${fix(1.0 + (2.5 - 2.35 * SensitivityFromParam(v)))} the mean`, 'How easily an onset fires: a band’s flux has to beat its running mean by 3.5× at 0 down to 1.15× at 1. On the clean synthetic band every hit fires at any setting; the ghost band’s soft kick only fires when it is high.'],
  ['bassBins', PT.BASS_BINS, 'integer', null, 'How many of the lowest bins are the low band (rings and bolts). FF_TYPE_INTEGER 1–8 in the plugin; a dropdown here.'],
  ['trebleFrom', PT.TREBLE_FROM, 'integer', null, 'The bin the high band starts at (curls); between the two is the middle band (bursts). FF_TYPE_INTEGER 4–48 in the plugin; a dropdown here.'],

  ['mix', PT.MIX, 'standard', (v) => fix(v), 'The doodles against the clip. At 0 the clip comes back as it went in.', null, 'over'],
];

const GROUPS = [
  [PT.DRIVE, PT.SEED, 'Draw'], [PT.RINGS, PT.WAVE, 'Doodles'], [PT.PLACEMENT, PT.CENTRE_Y, 'Place'],
  [PT.PALETTE, PT.BACKDROP, 'Look'], [PT.AUDIO, PT.TREBLE_FROM, 'Audio'], [PT.MIX, PT.MIX, 'Over'],
];
const GroupOf = (id) => GROUPS.find(([lo, hi]) => id >= lo && id <= hi)?.[2] ?? '?';
const NameOf = (id) => kParamNames[id] ?? '?';

/** The host's float for a page value: an integer's slot is its value's index. */
function hostValue(p, value) {
  if (p.integers) return p.integers[Math.round(value)];
  return value;
}

const PARAMS = SPEC.map(([id, slot, ffType, display, hint, elements, only]) => {
  const p = { id, slot, name: NameOf(slot), group: GroupOf(slot), hint, only };
  if (ffType === 'option') Object.assign(p, { type: 'option', elements });
  else if (ffType === 'integer') {
    const values = INTEGERS[id];
    Object.assign(p, { type: 'option', elements: values.map(String), integers: values });
  } else if (ffType === 'event') p.type = 'boolean';
  else if (ffType === 'colour') p.type = 'colour';
  else Object.assign(p, { type: 'standard', display });
  p.default = p.integers ? p.integers.indexOf(kDefaults[slot]) : kDefaults[slot];
  if (p.default < 0) throw new Error(`${id}: the default ${kDefaults[slot]} is not one of its values`);
  return p;
});

let renderer = null;

const mounted = mountDemo({
  name: 'Scrawl',
  pluginId: 'SK01 · SK02',
  kind: ['source', 'effect'],
  tagline:
    'Hand-drawn doodles to the music: rings of sound off the speakers, impact stars on the snare, a zigzag bolt on the big kicks, little curls off the hi-hats, and one long wobbly line across the frame that is the music itself. All redrawn twelve times a second by a hand that cannot hold still, so every line boils. SW Scrawl draws them over nothing; SW Scrawl Over draws them over your clip.',
  repo: 'https://github.com/stoatworks-labs/scrawl',
  page: 'https://stoatworks-labs.com/software/scrawl/',

  blurb:
    'It is Scrawl’s own GLSL (the capsules, the glow’s reduce and blur, and the composite), ported from the repository to WebGL2 unedited, drawing what a JavaScript port of the plugin’s CPU half decides: the onset detectors, the score, and the hand that makes every line, which a check in the repository holds to the plugin’s C++. There is no Resolume here, so there is no music either: the Audio input is fed a synthetic band, the same one the plugin’s test harness uses, not your music.',

  showBackdrop: true,
  needFloat: true,

  variants: {
    label: 'Plugin',
    default: initialVariant,
    options: [
      { id: 'source', name: 'SW Scrawl (source)', hint: 'SK01, FF_SOURCE: the doodles over nothing (Backdrop None), black or paper.' },
      { id: 'over', name: 'SW Scrawl Over (effect)', hint: 'SK02, FF_EFFECT: the same, drawn over your clip, with Mix.' },
    ],
  },

  // For the Over only. The scene is a picture to draw over; lights on black and
  // the geometry card show the glow and the lines' union against something busy.
  sources: ['scene', 'spot', 'grid', 'bars', 'ramp', 'detail', 'alpha'],

  params: PARAMS,

  differences: [
    'The music is not music. The plugin listens to the 64-bin spectrum Resolume hands every FFGL plugin, and a browser has neither Resolume nor that parameter, so the page feeds the plugin’s Audio input with the synthetic band its own test harness uses (sctest --audio demo): a kick every 28 frames at 60 fps, a snare on every other one, hats half-way between, over a quiet bed. It is a function of the frame number, not a recording and not an FFT of anything, and it is not your music; the page never asks for a microphone. “Audio in” switches it for the harness’s ghost band (a soft kick between the kicks, which only a high Sensitivity hears) or for silence, under which Drive Auto falls back to the beat after two seconds, as it does in the plugin.',
    'There is no host transport. In Resolume the beat drive takes the composition’s BPM and lines its bars up with Resolume’s bar phase. Nothing here sends one, so Beat (and Auto in silence) runs on the plugin’s own free-running 120 bpm, which is what the plugin does under a host that sends no beat.',
    'The CPU half is a PORT, not the plugin’s code. Everything Scrawl decides it decides on the CPU, in double: the three onset detectors (Audio.cpp, in float as the plugin keeps them), the transport, the score (what is spawned, where, and how each doodle’s path changes with its age), the hand that turns every path into capsules with this drawing’s boil, registration and pressure, and the clock that says when a new drawing is due. Here all of that is JavaScript. A check in the repository drives the real plugin class without GL and this port through the same 1,880 frames of both plugins (the synthetic band, the ghost band and silence, Hit, every placement, palette and drawing rate, a moved control, a restart and a stall, 50 fps as well as 60), and requires the same doodles spawned on the same frames from the same seeds, the same onsets and the same capsules. Against the plugin built without fused multiply-adds (its Intel slice’s arithmetic) all 292 doodles and all 2,167,049 capsules in 434 drawings are identical to the bit; against the Apple Silicon build, which fuses them, every doodle and onset is the same and the capsules are within about a ten-millionth of a frame height.',
    'Frames drawn by this page were compared with the plugin’s own harness at the same settings (960 × 540, the source, seven cases across the palettes, placements, passes, drives and audio feeds). In Chrome on an Apple GPU they came out 99.996% to 100% identical, every pixel within one level of 255. On a software renderer (SwiftShader) the lines’ edges came out up to ten levels apart: that is the rasteriser’s arithmetic, not the port’s, and your GPU will have its own.',
    'The GPU half is not a port: the capsule, reduce, blur and composite passes are the plugin’s own GLSL, drawn as the plugin draws them (one instanced quad a capsule with GL_MAX blending into a half-float buffer, a quarter-size copy blurred twice for the glow), and demo/tools/check_shaders.py fails the repository’s verify script if a character of them, or the way they are joined, drifts. WebGL2 needs EXT_color_buffer_float to render into half floats; without it the page stops and says so.',
    'Hit is FF_TYPE_EVENT in the plugin: here it is a toggle the page releases after one frame, one rising edge, which is what the plugin counts. Passes, Seed, Bass Bins and Treble From are FF_TYPE_INTEGER: here dropdowns of their values, Seed of 0 to 99 of its 0 to 9999. The Audio buffer is not a control here (it is what “Audio in” feeds) and the About block is not on the panel.',
    'Switching the Plugin dropdown is a new instance, as it would be in Resolume: the page starts with nothing on it and every control goes back to its default. SW Scrawl Over declares one more control, Mix, shown only when it is chosen.',
    'The clock is the page’s: seconds from frame deltas, capped at a tenth of a second a frame, handed to the plugin as the host’s time. The plugin’s vote on whether the host sends seconds or milliseconds is not ported, because this host’s unit is known. Restart is a backwards jump, which the plugin counts as a sixtieth of a second, so the doodles carry on. The synthetic band is sampled at the page’s clock (frame t × 60), so at 60 fps the plugin hears exactly what the harness gives it, and at other rates it hears the same band at different frames.',
    'Over: the clip is the kit’s generated one (or your own file), premultiplied and 8-bit as Resolume hands an effect its input. The harness’s card is not here.',
    'Nothing here is measured. The plugin’s harness holds the boil to the hand’s law to 2–3% read back out of the picture, a line’s cross-section to its width to 0.0023 px, a held drawing to the same picture to the bit and a kick to the first drawing at or after its frame, with fourteen deliberately wrong models and eight mutants all caught — and that harness, not this page, is the reason to believe the picture. The plugin has never been loaded into Resolume on a Mac, and no real music has reached it in a host.',
  ],

  createRenderer: (gl, screenQuad) => {
    renderer = createRenderer(gl, screenQuad);
    return renderer;
  },
});

//---------------------------------------------------------------------------
// Which controls belong to which plugin; the Audio in picker; the status line.
//
// By inline style, not the `hidden` attribute: kit.css gives these elements a
// `display` of their own, which beats the attribute's user-agent rule.
//---------------------------------------------------------------------------
if (mounted) {
  const params = mounted.params;
  const embed = query.has('embed') && query.get('embed') !== '0';
  let shownVariant = mounted.state.variant;

  // Copy link carries the plugin and the audio, since neither is a parameter.
  const toQuery = params.toQuery.bind(params);
  params.toQuery = () => {
    const q = toQuery();
    if (mounted.state.variant === 'over') q.set('plugin', 'over');
    if (audioIn.current !== 'demo') q.set('audio', audioIn.current);
    return q;
  };

  const show = (node, on) => { if (node) node.style.display = on ? '' : 'none'; };
  const rowOf = (p) => document.getElementById(`p-${p.id}`)?.closest('.prow')
    ?? [...document.querySelectorAll('.prow')].find((row) => row.querySelector('.prow__name')?.textContent === p.name);

  function showForVariant(variant) {
    for (const p of PARAMS) {
      if (p.only) show(rowOf(p), p.only === variant);
    }
    for (const heading of document.querySelectorAll('.pgroup__name')) {
      if (heading.textContent === 'Over') show(heading.closest('.pgroup'), variant === 'over');
    }
    const effect = variant === 'over';
    for (const field of document.querySelectorAll('.transport__field')) {
      if (field.querySelector('.transport__label')?.textContent === 'Clip') show(field, effect);
    }
    show(document.querySelector('.transport__file'), effect);
  }

  document.addEventListener('demo:state', () => {
    const variant = mounted.state.variant;
    if (variant !== shownVariant) {
      shownVariant = variant;
      params.reset();
    }
    if (!embed) showForVariant(variant);
  });

  if (!embed) {
    showForVariant(shownVariant);

    const stage = document.querySelector('.stage');
    if (stage) {
      //---------------------------------------------------------------------
      // Audio in: what the page puts in the plugin's Audio buffer.
      //---------------------------------------------------------------------
      const bar = document.createElement('div');
      bar.className = 'transport';
      const field = document.createElement('label');
      field.className = 'transport__field';
      const label = document.createElement('span');
      label.className = 'transport__label';
      label.textContent = 'Audio in';
      label.title = 'Not a plugin control: what this page feeds the plugin’s Audio input, since a browser has no Resolume FFT.';
      const select = document.createElement('select');
      select.className = 'transport__select';
      select.id = 'scrawl-audio';
      for (const a of AUDIO_IN) {
        const option = new Option(a.name, a.id);
        option.title = a.hint;
        select.append(option);
      }
      select.value = audioIn.current;
      select.addEventListener('change', () => {
        audioIn.current = select.value;
        mounted.redraw();
      });
      field.append(label, select);
      bar.append(field);
      const hit = document.createElement('button');
      hit.type = 'button';
      hit.className = 'btn';
      hit.textContent = 'Hit';
      hit.title = 'The plugin’s Hit event (also on the panel): one of each doodle at full strength.';
      hit.addEventListener('click', () => params.set('hit', 1));
      bar.append(hit);
      stage.querySelector('.transport')?.after(bar);

      //---------------------------------------------------------------------
      // A status line: reports what the port decided, measures nothing.
      //---------------------------------------------------------------------
      const line = document.createElement('p');
      line.className = 'stage__status';
      line.id = 'scrawl-stats';
      line.setAttribute('aria-live', 'off');
      stage.append(line);
      const note = document.createElement('p');
      note.className = 'stage__status';
      note.id = 'scrawl-note';
      stage.append(note);
      const kinds = ['rings', 'bursts', 'bolts', 'curls'];
      setInterval(() => {
        const s = renderer?.stats;
        if (!s) return;
        const mix = s.counts.map((n, i) => (n ? `${n} ${kinds[i]}` : null)).filter(Boolean).join(', ') || 'none';
        line.textContent =
          `Drawing ${s.drawing} · ${s.doodles} doodles on the page (${mix}) · ${s.segments.toLocaleString('en-GB')} capsules`
          + ` · following ${s.audioDriving ? 'the audio' : 'the beat (120 bpm, the plugin’s own)'}`;
        const feed = AUDIO_IN.find((a) => a.id === audioIn.current);
        note.textContent = audioIn.current === 'none'
          ? 'Audio in: silence. Nothing reaches the plugin’s Audio input.'
          : `Audio in: ${feed.name.toLowerCase()} — the plugin’s own test harness’s, not your music.`;
      }, 250);
    }
  }

  // For the headless checks in AGENTS.md: the page's pieces, read-only by convention.
  window.scrawlDemo = { mounted, get renderer() { return renderer; }, PARAMS, PT, audioIn, ScrawlFrames, demoSpectrum };
}
