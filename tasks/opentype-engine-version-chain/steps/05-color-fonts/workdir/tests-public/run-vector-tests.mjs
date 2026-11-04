import { execFileSync } from 'node:child_process';
import { readFileSync } from 'node:fs';

// Draw every vector case in vector-tests.json with /app/bin/hb-vector.js and compare the SVG
// to the reference's, character for character.
const cases = JSON.parse(readFileSync(new URL('./vector-tests.json', import.meta.url)));
const only = process.argv[2] || null;
const sel = only ? cases.filter((c) => c.font.includes(only) || (c.setting || '').includes(only)) : cases;
let ok = 0;
const bad = [];
for (const c of sel) {
  const argv = c.argv.map((a) => (a === 'FONT' ? `/data/vector-fonts/${c.font}` : a));
  let out = '';
  try {
    out = execFileSync('node', ['/app/bin/hb-vector.js', ...argv],
      { encoding: 'utf8', timeout: 60000, maxBuffer: 1 << 28,
        env: { ...process.env, LC_ALL: 'C' } });
  } catch (e) { out = (e.stdout || '') + ''; }
  if (out.trim() === c.expected.trim()) ok++;
  else if (bad.length < 20) bad.push(`${c.font} ${c.setting}`);
}
for (const b of bad) console.log('FAIL ' + b);
console.log(`TOTAL ${ok}/${sel.length} (${((100 * ok) / (sel.length || 1)).toFixed(1)}%)`);
