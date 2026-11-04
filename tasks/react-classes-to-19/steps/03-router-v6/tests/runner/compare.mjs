// Compares a directory of observations against the recorded baseline.
// One scenario scores 1 only if every compared field matches exactly.
//
// Compared: the named snapshots (canonical DOM and per-chart canvas
// transcript), the set of API paths the page requested, the document title and
// the resulting location hash.
//
// Not compared: console messages, page errors and failed requests.  The
// baseline index.html carries two dead <script> tags that 404 on every load;
// those belong to the starting tree, which is handed to the agent unmodified,
// so they are reported for information and judged by the migrated-ness gates
// instead.
//
// usage: node compare.mjs --expected dir --actual dir [--json out.json]
import fs from 'node:fs';
import path from 'node:path';

const argv = process.argv.slice(2);
const arg = (n, d) => {
  const i = argv.indexOf(`--${n}`);
  return i === -1 ? d : argv[i + 1];
};
const EXPECTED = arg('expected');
const ACTUAL = arg('actual');
const JSON_OUT = arg('json', null);
const MAX_DIFF_LINES = Number(arg('diff-lines', '25'));

const list = (d) => fs.readdirSync(d).filter((f) => f.endsWith('.json')).sort();

function firstDiff(a, b, label, out) {
  const A = String(a).split('\n');
  const B = String(b).split('\n');
  let shown = 0;
  for (let i = 0; i < Math.max(A.length, B.length) && shown < MAX_DIFF_LINES; i++) {
    if (A[i] !== B[i]) {
      out.push(`    ${label} line ${i + 1}:`);
      out.push(`      expected: ${A[i] === undefined ? '<missing>' : A[i]}`);
      out.push(`      actual:   ${B[i] === undefined ? '<missing>' : B[i]}`);
      shown++;
    }
  }
  if (shown === MAX_DIFF_LINES) out.push(`    ${label}: ... more differences suppressed`);
}

function compareSnap(name, exp, act, out) {
  let ok = true;
  if (!act) {
    out.push(`    snapshot "${name}" missing`);
    return false;
  }
  if (exp.dom !== act.dom) {
    ok = false;
    out.push(`    snapshot "${name}" dom differs`);
    firstDiff(exp.dom, act.dom, 'dom', out);
  }
  const ec = JSON.stringify(exp.charts, null, 1);
  const ac = JSON.stringify(act.charts, null, 1);
  if (ec !== ac) {
    ok = false;
    const ek = exp.charts.map((c) => c.key).join(',');
    const ak = (act.charts || []).map((c) => c.key).join(',');
    out.push(`    snapshot "${name}" charts differ (expected keys [${ek}], actual [${ak}])`);
    firstDiff(ec, ac, 'charts', out);
  }
  if (exp.title !== act.title) {
    ok = false;
    out.push(`    snapshot "${name}" title: expected ${JSON.stringify(exp.title)}, actual ${JSON.stringify(act.title)}`);
  }
  if (exp.hash !== act.hash) {
    ok = false;
    out.push(`    snapshot "${name}" hash: expected ${JSON.stringify(exp.hash)}, actual ${JSON.stringify(act.hash)}`);
  }
  return ok;
}

const results = [];
for (const f of list(EXPECTED)) {
  const id = f.replace(/\.json$/, '');
  const exp = JSON.parse(fs.readFileSync(path.join(EXPECTED, f), 'utf8'));
  const actPath = path.join(ACTUAL, f);
  const out = [];
  let ok = true;
  if (!fs.existsSync(actPath)) {
    results.push({ id, ok: false, detail: ['    no observation produced'] });
    continue;
  }
  const act = JSON.parse(fs.readFileSync(actPath, 'utf8'));
  for (const name of Object.keys(exp.snaps)) {
    if (!compareSnap(name, exp.snaps[name], act.snaps[name], out)) ok = false;
  }
  const extra = Object.keys(act.snaps || {}).filter((k) => !(k in exp.snaps));
  if (extra.length) { ok = false; out.push(`    unexpected snapshots: ${extra.join(', ')}`); }
  const ea = exp.apiPaths.join('\n');
  const aa = (act.apiPaths || []).join('\n');
  if (ea !== aa) {
    ok = false;
    out.push('    api paths differ');
    firstDiff(ea, aa, 'apiPaths', out);
  }
  results.push({ id, ok, detail: out });
}

const passed = results.filter((r) => r.ok).length;
for (const r of results) {
  if (r.ok) continue;
  process.stdout.write(`FAIL ${r.id}\n${r.detail.join('\n')}\n`);
}
process.stdout.write(`\nbehavior: ${passed}/${results.length} scenarios match the baseline\n`);
if (JSON_OUT) {
  fs.writeFileSync(JSON_OUT, JSON.stringify({
    total: results.length,
    passed,
    failed: results.filter((r) => !r.ok).map((r) => r.id),
  }, null, 1) + '\n');
}
process.exit(passed === results.length ? 0 : 1);
