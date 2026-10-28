// The JavaScript half of the browser demo's cross-check: the PORT block of
// demo/plugin.js (between its BEGIN PORT and END PORT markers, sliced out as
// text and run here, so what is checked is exactly what the page runs), driven
// through scenario.mjs's frames and printed as printer.cpp prints the real
// plugin class. compare.py diffs the two.
//
//     node demo/tools/crosscheck/run.mjs SEGMENTS.bin < scenario.txt > events.txt

import { readFileSync, openSync, writeSync, closeSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const here = dirname(fileURLToPath(import.meta.url));
const source = readFileSync(join(here, '..', '..', 'plugin.js'), 'utf8');
const begin = source.indexOf('// ==== BEGIN PORT');
const end = source.indexOf('// ==== END PORT');
if (begin < 0 || end < begin) {
  process.stderr.write('run.mjs: the PORT markers are missing from demo/plugin.js\n');
  process.exit(2);
}
// eslint-disable-next-line no-new-func
const P = new Function(`${source.slice(begin, end)}\nreturn { ScrawlFrames, demoSpectrum, kParamNames, PT, kBins, kDemoPeriod };`)();

const view = new DataView(new ArrayBuffer(8));
const h32 = (v) => (v >>> 0).toString(16).padStart(8, '0');
const F = (v) => { view.setFloat32(0, v); return `f:${h32(view.getUint32(0))}`; };
const D = (v) => { view.setFloat64(0, v); return `d:${h32(view.getUint32(0))}${h32(view.getUint32(4))}`; };

const segmentFile = openSync(process.argv[2], 'w');
const lines = readFileSync(0, 'utf8').split('\n');
const out = [];
let st = null;
let frame = 0;
let spawned = 0;
const bins = new Float32Array(P.kBins);

for (const line of lines) {
  const tokens = line.trim().split(/\s+/);
  if (tokens[0] === 'new') {
    st = new P.ScrawlFrames(tokens[1] !== '0');
    frame = 0;
    spawned = 0;
    out.push(`new ${tokens[1]}`);
    for (let id = 0; id < st.ParamCount(); id += 1) {
      if (id >= P.PT.ABOUT_TEXT && id < P.PT.SOURCE_COUNT) continue;
      out.push(`P ${id} ${P.kParamNames[id]} ${F(st.params[id])}`);
    }
    continue;
  }
  if (tokens[0] !== 'f' || !st) continue;
  const time = Number(tokens[1]);
  const audio = tokens[2];
  const band = Number(tokens[3]);
  const aspect = Number(tokens[4]);
  for (const pair of tokens.slice(5)) {
    const [id, value] = pair.split('=');
    st.SetFloatParameter(Number(id), Number(value));
  }
  if (audio === 'none') bins.fill(0);
  else P.demoSpectrum(band, bins, P.kDemoPeriod, true, audio === 'ghost');
  st.SetAudio(bins);
  st.SetTime(time);
  const due = st.Advance(aspect);
  if (due) st.BuildDrawing(aspect);

  const a = st.analyser;
  const doodles = st.score.doodles;
  out.push(['F', frame, due ? 1 : 0, st.drawing, st.audioDriving ? 1 : 0, doodles.length, st.score.spawned, D(st.clock),
    a.Low().fired ? 1 : 0, F(a.Low().strength), a.Mid().fired ? 1 : 0, F(a.Mid().strength),
    a.High().fired ? 1 : 0, F(a.High().strength), F(a.Level())].join(' '));
  const fresh = Math.min(st.score.spawned - spawned, doodles.length);
  for (let i = doodles.length - fresh; i < doodles.length; i += 1) {
    const d = doodles[i];
    out.push(['N', d.kind, D(d.birth), d.seed, D(d.anchor.x), D(d.anchor.y), D(d.direction), D(d.scale), D(d.strength),
      d.colour, d.fullCircle ? 1 : 0, d.count, d.extra ? 1 : 0, d.variant].join(' '));
  }
  spawned = st.score.spawned;
  if (due) {
    out.push(`D ${st.drawing} ${st.segments.count}`);
    if (st.segments.count) {
      const bytes = new Uint8Array(st.segments.data.buffer, 0, st.segments.count * 40);
      writeSync(segmentFile, bytes);
    }
  }
  frame += 1;
}
closeSync(segmentFile);
process.stdout.write(`${out.join('\n')}\n`);
