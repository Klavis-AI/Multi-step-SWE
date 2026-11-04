import { execFileSync } from 'node:child_process';
import { readFileSync } from 'node:fs';

// Feed every shaping test in shape-tests.json to /app/bin/hb-shape.js through its --batch
// protocol and report how many outputs match the reference exactly.
const cases = JSON.parse(readFileSync(new URL('./shape-tests.json', import.meta.url)));
const only = process.argv[2] || null;            // optional file-name filter
const sel = only ? cases.filter((c) => c.file.includes(only)) : cases;
const lines = sel.map((c) =>
  c.argv.map((a) => (a === 'FONT' ? `/data/shape-fonts/${c.font}` : a)).join(';')).join('\n');
let out = '';
try {
  out = execFileSync('node', ['/app/bin/hb-shape.js', '--batch'],
    { input: lines + '\n', encoding: 'utf8', maxBuffer: 1 << 28, timeout: 900000,
      env: { ...process.env, LC_ALL: 'C' } });
} catch (e) { out = (e.stdout || '') + ''; }
const got = out.split('\n');
const per = {};
let ok = 0;
sel.forEach((c, i) => {
  const k = c.file.replace(/\.tests$/, '');
  per[k] ??= [0, 0];
  per[k][1]++;
  if ((got[i] || '').trim() === c.expected.trim()) { per[k][0]++; ok++; }
});
const worst = Object.entries(per).filter(([, v]) => v[0] < v[1])
  .sort((a, b) => (a[1][0] / a[1][1]) - (b[1][0] / b[1][1])).slice(0, 25);
for (const [k, v] of worst) console.log(`${k.padEnd(34)} ${v[0]}/${v[1]}`);
console.log(`TOTAL ${ok}/${sel.length} (${((100 * ok) / sel.length).toFixed(1)}%)`);
