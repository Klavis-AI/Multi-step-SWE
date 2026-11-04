// Combines the two scored families into this step's reward and writes the
// per-step detail file.
//
// The reward is the product of the two families, the same for every step of
// this task (product scoring, 2026-10-13):
//
//     reward = gates x behavior
//
// gates (T1, migrated-ness) is the weighted mean of the gate spec's checks;
// behavior (T2, preservation) is the share of recorded scenarios the rebuilt
// site still reproduces.  A product rather than a weighted sum, so the two
// calibrations land where they should: the previous step's tree untouched
// passes the whole behavior family and no gate at all, and scores 0; an
// automated codemod that rewrites syntax without making the application work
// scores nothing either - the gate spec's own guard withholds the gate family
// from a tree that no longer serves the site, or serves a site its own source
// did not build, so syntax that does not run and a borrowed build both earn 0.
// A tree that does the step's work but breaks some of the application loses in
// proportion to what it broke.
//
// A gate's score is a fraction, not a verdict, so that a partly-migrated tree
// is ordered above an untouched one; a gate with `binary: true` is all or
// nothing because a single remaining site defeats its purpose (one legacy root
// render still renders the whole app the old way).
//
// usage: node score.mjs --step 01 --behavior b.json --gates g.json
//        [--out detail.json]
import fs from 'node:fs';

const argv = process.argv.slice(2);
const arg = (n, d) => {
  const i = argv.indexOf(`--${n}`);
  return i === -1 ? d : argv[i + 1];
};

const STEP = arg('step');
const OUT = arg('out', `/logs/verifier/step${STEP}-detail.json`);

const readJson = (p) => {
  try {
    return JSON.parse(fs.readFileSync(p, 'utf8'));
  } catch (e) {
    return null;
  }
};

const behavior = readJson(arg('behavior'));
const gates = readJson(arg('gates'));

// A missing family file is a harness failure, not a candidate failure, but it
// must never read as a full score for that family.  Zero is the honest value and
// the detail file says which file was absent, so a run that lost a family is
// visible instead of quietly generous.
const problems = [];

let behaviorScore = 0;
let behaviorDetail = { total: 0, passed: 0, failed: [] };
if (!behavior) {
  problems.push('behavior.json missing or unparseable');
} else if (!Number.isInteger(behavior.total) || behavior.total < 1) {
  problems.push(`behavior.json has total=${behavior.total}`);
} else {
  behaviorDetail = behavior;
  behaviorScore = behavior.passed / behavior.total;
}

let gatesScore = 0;
const gateDetail = [];
if (!gates || !Array.isArray(gates.checks) || gates.checks.length === 0) {
  problems.push('gates.json missing, unparseable or empty');
} else {
  let weightSum = 0;
  let weighted = 0;
  for (const c of gates.checks) {
    const weight = c.weight == null ? 1 : Number(c.weight);
    let score = Number(c.score);
    if (!Number.isFinite(score)) {
      problems.push(`gate ${c.id} produced no score`);
      score = 0;
    }
    score = Math.max(0, Math.min(1, score));
    if (c.binary && score < 1) score = 0;
    weightSum += weight;
    weighted += weight * score;
    gateDetail.push({ id: c.id, weight, score, detail: c.detail ?? null });
  }
  if (weightSum > 0) gatesScore = weighted / weightSum;
}

const reward = gatesScore * behaviorScore;

const detail = {
  step: STEP,
  reward: Number(reward.toFixed(6)),
  formula: 'gates x behavior',
  families: {
    behavior: {
      factor: 'T2 preservation',
      score: Number(behaviorScore.toFixed(6)),
      passed: behaviorDetail.passed,
      total: behaviorDetail.total,
      failed: behaviorDetail.failed || [],
    },
    gates: {
      factor: 'T1 migrated-ness',
      score: Number(gatesScore.toFixed(6)),
      checks: gateDetail,
    },
  },
  problems,
};

fs.writeFileSync(OUT, JSON.stringify(detail, null, 1) + '\n');
try {
  fs.chmodSync(OUT, 0o644);
} catch {}

process.stdout.write(`behavior ${behaviorDetail.passed}/${behaviorDetail.total} = ${behaviorScore.toFixed(4)}`
  + `   gates ${gatesScore.toFixed(4)}   reward ${reward.toFixed(6)}\n`);
for (const p of problems) process.stdout.write(`PROBLEM: ${p}\n`);
for (const g of gateDetail) {
  process.stdout.write(`  gate ${g.id.padEnd(28)} ${g.score.toFixed(3)}`
    + (g.detail ? `  ${g.detail}` : '') + '\n');
}
// The reward itself goes to stdout's last line so test.sh can read it without
// parsing JSON in bash.
process.stdout.write(`REWARD ${reward.toFixed(6)}\n`);
