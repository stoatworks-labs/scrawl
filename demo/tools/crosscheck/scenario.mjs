// The cross-check's scenario: what a host does to the plugin, frame by frame.
// printer.cpp (the real plugin class) and run.mjs (the page's port) both read
// it from stdin, so they are driven by the same text and nothing else.
//
//     node demo/tools/crosscheck/scenario.mjs > scenario.txt
//
// Lines:
//   new <effect 0|1>                              a new instance (the constructor's defaults)
//   f <time> <audio> <band> <aspect> [id=value…]  one frame: set the parameters (SetFloatParameter,
//                                                 by ParamId), feed the Audio buffer (none, or the
//                                                 harness's band at frame <band>, demo or ghost),
//                                                 SetTime( time ), Advance( aspect ), and BuildDrawing
//                                                 when a drawing is due.
//
// Numbers print as JavaScript's shortest round-trip form, which strtod reads back
// to the same double.

const PT = {
  DRIVE: 0, HIT: 1, DRAWINGS: 2, WOBBLE: 3, LINE_WIDTH: 4, PASSES: 5, SIZE: 6, LINGER: 7, SEED: 8,
  RINGS: 9, BURSTS: 10, BOLTS: 11, CURLS: 12, WAVE: 13, PLACEMENT: 14, CENTRE_X: 15, CENTRE_Y: 16,
  PALETTE: 17, COLOUR_R: 18, COLOUR_G: 19, COLOUR_B: 20, GLOW: 21, BACKDROP: 22,
  SENSITIVITY: 24, BASS_BINS: 25, TREBLE_FROM: 26, MIX: 32,
};

const out = [];
const frame = (time, audio, band, aspect, sets = {}) => {
  const pairs = Object.entries(sets).map(([k, v]) => `${PT[k] ?? k}=${v}`);
  out.push(['f', time, audio, band, aspect, ...pairs].join(' '));
};
const band = (t) => Math.floor(t * 60 + 1e-6);

// A. The source at its defaults on the synthetic band, five seconds at 60 fps.
out.push('new 0');
for (let n = 0; n < 300; n += 1) frame(n / 60, 'demo', n, 16 / 9);

// B. The Over: Neon, three passes, round a moved Centre, a new Seed; Hit pressed
//    and released on frame 60 (two sets in one frame, as a host's press is), and
//    Drawings changed mid-run.
out.push('new 1');
for (let n = 0; n < 240; n += 1) {
  const sets = n === 0 ? { PALETTE: 2, PASSES: 3, PLACEMENT: 1, CENTRE_X: 0.3, CENTRE_Y: 0.62, SEED: 42, MIX: 0.7 } : {};
  if (n === 60) out.push(`f ${n / 60} demo ${n} ${16 / 9} 1=1 1=0`);
  else frame(n / 60, 'demo', n, 16 / 9, n === 150 ? { DRAWINGS: 0 } : sets);
}

// C. Silence, so Auto follows the plugin's own 120 bpm: the Speakers, on ones,
//    a shorter Linger, smaller doodles, a shakier hand.
out.push('new 0');
for (let n = 0; n < 240; n += 1) {
  frame(n / 60, 'none', n, 16 / 9, n === 0 ? { PLACEMENT: 2, DRAWINGS: 0, LINGER: 0.3, SIZE: 0.35, WOBBLE: 0.9 } : {});
}

// D. The ghost band at 50 fps (the band sampled at the page's clock, as the
//    page does), Sensitivity up, the bands moved, on threes, every bolt and two
//    curls a hat.
out.push('new 0');
for (let n = 0; n < 250; n += 1) {
  const t = n / 50;
  frame(t, 'ghost', band(t), 16 / 9, n === 0 ? { SENSITIVITY: 1, BASS_BINS: 3, TREBLE_FROM: 20, DRAWINGS: 2, BOLTS: 1, CURLS: 0.9 } : {});
}

// E. Clock jumps (a restart back to 0 and a leap of three seconds), Drive
//    switched Auto -> Beat -> Audio, a control moved between drawings (the same
//    drawing made again), Ink on Paper, 4:3.
out.push('new 0');
let t = 0;
for (let n = 0; n < 260; n += 1) {
  if (n === 100) t = 0;              // Restart: a step back
  if (n === 180) t += 3;             // a stall: more than a second forward
  const sets = {};
  if (n === 0) Object.assign(sets, { PALETTE: 1, BACKDROP: 2, PASSES: 2 });
  if (n === 70) sets.DRIVE = 2;
  if (n === 140) sets.DRIVE = 1;
  if (n === 43) sets.WOBBLE = 0;
  if (n === 44) sets.WOBBLE = 0.73;
  frame(t, 'demo', band(t), 4 / 3, sets);
  t += 1 / 60;
}

// F. The Over on Drive Audio in silence (nothing), then the band arrives; Rings
//    at 0 (none at all), the Wave at 1, Palette Colour, the Seed's upper end,
//    and a Hit in silence.
out.push('new 1');
for (let n = 0; n < 200; n += 1) {
  const sets = n === 0
    ? { DRIVE: 1, RINGS: 0, WAVE: 1, PALETTE: 3, COLOUR_R: 0.2, COLOUR_G: 0.9, COLOUR_B: 0.4, SEED: 9999, SIZE: 0.8 }
    : {};
  if (n === 20) out.push(`f ${n / 60} none ${n} ${16 / 9} 1=1 1=0`);
  else frame(n / 60, n < 60 ? 'none' : 'demo', n, 16 / 9, sets);
}

// G. Every Placement under the beat at the extremes of Size, with Hits, so the
//    Centre's and the Speakers' random draws are all exercised.
for (const placement of [0, 1, 2]) {
  out.push('new 0');
  for (let n = 0; n < 130; n += 1) {
    const sets = n === 0 ? { PLACEMENT: placement, SIZE: placement === 1 ? 1 : 0, BURSTS: 1, BOLTS: 0.6, CURLS: 0.7 } : {};
    if (n % 25 === 5) out.push(`f ${n / 60} none ${n} ${16 / 9} 1=1 1=0`);
    else frame(n / 60, 'none', n, 16 / 9, sets);
  }
}

process.stdout.write(`${out.join('\n')}\n`);
