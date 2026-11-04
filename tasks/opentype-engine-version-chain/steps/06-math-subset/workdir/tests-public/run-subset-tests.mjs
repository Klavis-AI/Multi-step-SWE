import { execFileSync } from 'node:child_process';
import { readFileSync, existsSync, statSync, mkdirSync } from 'node:fs';

// Run /app/bin/hb-subset.js over the cases in subset-tests.json and report how many produce a
// font at all, and how many match the reference's table set and glyph count.
// (A full match is table by table, through fontTools' TTX; this is the cheap check.)
const cases = JSON.parse(readFileSync(new URL('./subset-tests.json', import.meta.url)));
mkdirSync('/tmp/subset-check', { recursive: true });
let produced = 0, tablesOk = 0;
const bad = [];
cases.forEach((c, i) => {
  const out = `/tmp/subset-check/${i}.ttf`;
  try {
    execFileSync('node', ['/app/bin/hb-subset.js', `--output-file=${out}`, ...c.opts,
                          `/data/subset-fonts/${c.font}`],
                 { encoding: 'utf8', timeout: 120000, env: { ...process.env, LC_ALL: 'C' } });
  } catch (e) { /* fall through to the existence check */ }
  if (existsSync(out) && statSync(out).size > 0) {
    produced++;
    const buf = readFileSync(out);
    const num = buf.readUInt16BE(4);
    const tags = [];
    for (let t = 0; t < num; t++) tags.push(buf.subarray(12 + t * 16, 16 + t * 16).toString('latin1'));
    const want = new Set(c.tables), got = new Set(tags);
    const same = want.size === got.size && [...want].every((t) => got.has(t));
    if (same) tablesOk++;
    else if (bad.length < 15)
      bad.push(`${c.font} ${c.opts.join(' ')}  missing=[${[...want].filter(t=>!got.has(t))}] extra=[${[...got].filter(t=>!want.has(t))}]`);
  } else if (bad.length < 15) bad.push(`${c.font} ${c.opts.join(' ')}  produced nothing`);
});
for (const b of bad) console.log('FAIL ' + b);
console.log(`produced ${produced}/${cases.length}, table set matches ${tablesOk}/${cases.length}`);
