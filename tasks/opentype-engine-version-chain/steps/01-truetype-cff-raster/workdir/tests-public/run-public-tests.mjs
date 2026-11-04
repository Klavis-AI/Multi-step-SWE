import { execFileSync } from 'node:child_process';
import { readFileSync } from 'node:fs';

const cases = JSON.parse(readFileSync(new URL('./expected.json', import.meta.url)));
const only = process.argv[2] || null;          // optional stratum filter
const EMPTY = 'D41D8CD98F00B204E9800998ECF8427E';
const isGlyph = (l) => /^ *\d+ /.test(l);
const acc = {};

for (const c of cases) {
  if (only && c.stratum !== only) continue;
  const path = `/data/fonts/${c.font}`;
  const argv = c.argv.map((a) => (a === 'FONT' ? path : a));
  let out = '';
  try {
    out = execFileSync('node', ['/app/bin/ftlint.js', ...argv],
                       { encoding: 'utf8', timeout: 60000, env: { ...process.env, LC_ALL: 'C' } });
  } catch (e) { out = (e.stdout || '') + ''; }
  out = out.split(path).join('FONT');
  const got = new Map();
  for (const l of out.split('\n')) if (isGlyph(l)) got.set(l.trim().split(/\s+/)[0], l);
  const key = c.flags === '8000' ? 'hinted' : c.stratum;
  acc[key] ??= { matched: 0, total: 0, cases: 0, exact: 0 };
  let matched = 0, total = 0;
  for (const l of c.expected.split('\n')) {
    if (!isGlyph(l) || l.includes(EMPTY)) continue;
    total++;
    if (got.get(l.trim().split(/\s+/)[0]) === l) matched++;
  }
  acc[key].matched += matched; acc[key].total += total; acc[key].cases++;
  if (out.trimEnd() === c.expected.trimEnd()) acc[key].exact++;
}
let m = 0, t = 0;
for (const [k, v] of Object.entries(acc)) {
  m += v.matched; t += v.total;
  console.log(`${k.padEnd(16)} glyph lines ${v.matched}/${v.total}` +
              `  (${((100 * v.matched) / (v.total || 1)).toFixed(1)}%)` +
              `  whole invocations identical ${v.exact}/${v.cases}`);
}
console.log(`TOTAL            glyph lines ${m}/${t} (${((100 * m) / (t || 1)).toFixed(1)}%)`);
