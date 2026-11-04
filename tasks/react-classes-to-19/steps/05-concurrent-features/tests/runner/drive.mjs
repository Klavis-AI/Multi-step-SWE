// Runs a list of scenarios against a served build of the application and writes
// one JSON observation per scenario.  The same driver is used to record the
// baseline and to observe a candidate tree, so any difference in the output is a
// difference in the application, not in the harness.
//
// usage: node drive.mjs --base http://127.0.0.1:3000 --scenarios s.json --out dir
//                      [--workers 4] [--filter regex] [--repeat 1]
import { chromium } from 'playwright';
import fs from 'node:fs';
import path from 'node:path';

const argv = process.argv.slice(2);
const arg = (name, dflt) => {
  const i = argv.indexOf(`--${name}`);
  return i === -1 ? dflt : argv[i + 1];
};

const BASE = arg('base', 'http://127.0.0.1:3000').replace(/\/$/, '');
const SCENARIOS = arg('scenarios', 'scenarios.json');
const OUT = arg('out', 'out');
const WORKERS = Number(arg('workers', '4'));
const FILTER = arg('filter', null);
// Author-side only: when the served build is the istanbul-instrumented one,
// dump each scenario's counters so the suite's reach over src/ can be measured.
const COVERAGE_OUT = arg('coverage-out', null);
// Author-side only: keep the whole draw-operation stream next to its digest, so
// that a scenario whose digest moves can be diffed operation by operation.  The
// verifier never passes this; the recordings it compares hold digests only.
const FULL = argv.includes('--full');
const TOOLS = path.dirname(new URL(import.meta.url).pathname);

const DEFAULT_CLOCK = '2024-01-15T10:00:00.000Z';
const DEFAULT_VIEWPORT = [1280, 900];
const DEFAULT_SETTLE = 2500;
const DEFAULT_ADVANCE = 2000;
// echarts drives its transitions off requestAnimationFrame, which the installed
// clock fakes along with everything else - so an animation only progresses while
// the clock is being run forward, and stops dead the moment it is paused again.
// A setOption that lands after the last runFor therefore leaves the canvas on an
// intermediate frame for good.  This is the fixed slice run after the network has
// gone quiet, long enough to retire echarts' default 1000ms transition.
const ANIM_TAIL_MS = 2000;
// Budget for the quiet() that guards that tail.  It is not a timeout anybody is
// expected to reach: it exists so a box slow enough to still be answering after
// five seconds waits rather than recording a half-drawn chart.
const QUIET_HARD_MS = 20000;
// How many times the animation tail above may be re-run before a scenario is
// declared unsettleable.  The application's shortest polling interval is 5s and
// a slice is 2s, so a page doing nothing but polling needs at most two rounds to
// land on a slice with no tick in it; the rest is headroom.
const TAIL_ROUNDS = 8;
// How far before the frozen instant the fake clock is installed, so that
// pauseAt() always has a positive interval to consume.  Nothing is loaded yet
// when it is consumed, so no timer fires in the lead.
const PAUSE_LEAD_MS = 60000;
// How long an action whose settle is suppressed waits for the page's own code
// to arrive: the window the page has to ask for a module, and the window a
// module that has just arrived has to ask for the next one.  See modules().
const MODULE_QUIET_MS = 400;
// Budget for the whole of that wait, for the same reason quiet() has one.
const MODULE_HARD_MS = 20000;

// Any request to one of these path prefixes is part of the application's API
// surface.  Recorded as a set of paths: request count and order are an
// implementation detail that a caching data layer is allowed to change.
const API_PREFIX = /^\/(index|hour|mock|users|userList)(\/|$|\?)/;

const probeScript = fs.readFileSync(path.join(TOOLS, 'probe.js'), 'utf8');
const snapshotScript = fs.readFileSync(path.join(TOOLS, 'snapshot.js'), 'utf8');

let scenarios = JSON.parse(fs.readFileSync(SCENARIOS, 'utf8'));
if (FILTER) {
  const re = new RegExp(FILTER);
  scenarios = scenarios.filter((s) => re.test(s.id));
}
fs.mkdirSync(OUT, { recursive: true });

function stable(v) {
  // JSON with object keys sorted, so two observations of the same state are
  // byte-identical regardless of insertion order.
  if (Array.isArray(v)) return v.map(stable);
  if (v && typeof v === 'object') {
    const o = {};
    for (const k of Object.keys(v).sort()) o[k] = stable(v[k]);
    return o;
  }
  return v;
}

async function runScenario(browser, sc) {
  const [w, h] = sc.viewport || DEFAULT_VIEWPORT;
  const ctx = await browser.newContext({
    viewport: { width: w, height: h },
    deviceScaleFactor: 1,
    locale: 'zh-CN',
    timezoneId: 'UTC',
    bypassCSP: true,
  });
  const page = await ctx.newPage();

  const consoleMsgs = [];
  const pageErrors = [];
  const apiPaths = new Set();
  const failedRequests = [];
  let inFlight = 0;
  let inFlightScripts = 0;
  let reqStarted = 0;
  let lastQuiet = Date.now();

  page.on('console', (m) => {
    const t = m.type();
    if (t === 'error' || t === 'warning') consoleMsgs.push(`${t}: ${m.text()}`);
  });
  page.on('pageerror', (e) => pageErrors.push(String(e && e.message ? e.message : e)));
  page.on('request', (r) => {
    inFlight++;
    reqStarted++;
    if (r.resourceType() === 'script') inFlightScripts++;
    try {
      const u = new URL(r.url());
      if (API_PREFIX.test(u.pathname)) apiPaths.add(u.pathname + (u.search || ''));
    } catch { /* non-http request */ }
  });
  const done = (r) => {
    inFlight = Math.max(0, inFlight - 1);
    if (r.resourceType() === 'script') inFlightScripts = Math.max(0, inFlightScripts - 1);
    if (inFlight === 0) lastQuiet = Date.now();
  };
  page.on('requestfinished', done);
  page.on('requestfailed', (r) => {
    done(r);
    failedRequests.push(`${r.method()} ${r.url()} ${r.failure() ? r.failure().errorText : ''}`);
  });

  await page.addInitScript(probeScript);
  await page.addInitScript(snapshotScript);
  if (FULL) await page.addInitScript(() => { window.__snapshotFull = true; });
  // install() leaves the clock *running* against real time, and pauseAt() can
  // only jump forward - it throws "Cannot fast-forward to the past" when the
  // target instant has already gone by.  Installing at the target itself is
  // therefore a race with the round trip between the two calls, lost whenever
  // the box is busy enough for a millisecond to elapse.  Install a minute early
  // instead and let pauseAt() consume the lead: the frozen instant is then
  // exactly sc.clock no matter how long the round trip took.
  const frozen = new Date(sc.clock || DEFAULT_CLOCK);
  await page.clock.install({ time: new Date(frozen.getTime() - PAUSE_LEAD_MS) });
  await page.clock.pauseAt(frozen);

  const snaps = {};
  // Wait, on real time, until nothing has been in flight for 400ms.  Responses
  // arrive on real time however the page's own clock is set, so this is the
  // only way to know the application has everything it asked for.
  const quiet = async (budget) => {
    const deadline = Date.now() + budget;
    while (Date.now() < deadline) {
      if (inFlight === 0 && Date.now() - lastQuiet > 400) return;
      await page.waitForTimeout(100);
    }
  };
  // The shortest polling interval in the application is 5s (firstPage/allSellers).
  // Stepping the fake clock by less than that fires at most one tick per chart
  // per slice, so a tick's response is always absorbed before the next tick is
  // due.  Running a long advance in one go instead lets responses arrive on real
  // time *between* ticks, where a component that re-installs its interval on new
  // props (sellers/singleSellerChartCurrent) shifts the remaining ticks' phase -
  // and then the number of polls in the transcript depends on the network.
  const SLICE_MS = 4000;
  const advance = async (ms, budget) => {
    // Advancing the fake clock fires the polling charts' intervals and the
    // resize debounce, which start requests of their own; quiet the network
    // again afterwards so the snapshot never lands mid-flight.
    for (let left = ms; left > 0; left -= SLICE_MS) {
      await page.clock.runFor(Math.min(left, SLICE_MS));
      await page.waitForTimeout(150);
      await quiet(budget);
    }
    await page.waitForTimeout(150);
    // The loop above ends on a quiet(), so the last thing that happens before a
    // snapshot is a response arriving - and the animation that response starts
    // has no clock left to run on.  That is a coin flip on the network: the same
    // scenario records the settled chart when the payload is early and a frame
    // part of the way there when it is late.  Wait for the page to have
    // everything it asked for, then run one more fixed slice, so the last thing
    // that happens is the clock moving rather than the network landing.
    //
    // One slice is not enough, because running the clock is itself a way of
    // starting requests: a polling interval or a resize debounce fires inside
    // the slice, and the response to what it asks for arrives on real time,
    // after the slice has finished.  Whatever that response repaints is then
    // scheduled on a frame that never comes, and the canvas keeps what was
    // already on it - a loading mask that had not been taken down yet, or a
    // transition stopped part of the way - until the scenario ends.  Which of
    // those a run records depends on how quickly the box answered, and that is
    // the coin flip this tail exists to remove.
    //
    // So the tail runs to a fixed point: settle the network, run a slice, and
    // if that slice asked for anything, go round again.  The loop leaves on a
    // slice during which the page started no request, so every response it has
    // is one the clock has since had the chance to draw.
    //
    // Reading the network here does not make the result depend on it.  What a
    // slice starts is decided by the fake clock, which nothing but this loop
    // moves, and the quiet() at the top of each round has already waited for
    // the previous round's answers - so the same scenario takes the same number
    // of rounds on a fast box and a slow one.  A page that never goes still is
    // a scenario to fix rather than one to wait out, so the rounds are bounded
    // and running out of them is an error, not a quietly shorter tail.
    for (let round = 1; ; round++) {
      await quiet(QUIET_HARD_MS);
      const started = reqStarted;
      await page.clock.runFor(ANIM_TAIL_MS);
      await page.waitForTimeout(150);
      if (reqStarted === started) break;
      if (round === TAIL_ROUNDS) {
        throw new Error(`clock tail still starting requests after ${TAIL_ROUNDS} rounds`);
      }
    }
  };
  const settle = async (ms) => {
    const budget = ms == null ? DEFAULT_SETTLE : ms;
    await quiet(budget);
    await advance(sc.advance == null ? DEFAULT_ADVANCE : sc.advance, budget);
  };
  // What an action with its settle suppressed waits for instead of settling.
  //
  // A burst of those actions is adversarial about what the application does
  // when it is given no time to think.  It is not adversarial about how fast a
  // file arrives.  A build that ships as one bundle has every module before the
  // burst starts, so nothing in the burst ever waits on the network and the
  // transcript is a record of what a page that already has its code does.  A
  // build that fetches a panel when the visitor reaches it puts a module
  // request in the middle of the burst instead, and then whether that panel
  // mounts at all - whether its effects run, and so whether it asks the API for
  // anything - is decided by whether the chunk beat the next action.  That is
  // the box answering quickly, not the tree behaving differently, and it is the
  // same coin flip the clock tail in advance() exists to remove.
  //
  // So a suppressed settle still waits for the page's own code: until nothing
  // has been in flight for script for MODULE_QUIET_MS.  The quiet window is
  // what makes a chunk that imports another chunk one wait rather than two, and
  // it is also the window in which the request gets made at all, since the
  // navigation has only just been handed to the application.  A page that
  // fetches nothing on demand waits out that window and nothing else, which is
  // why the burst still arrives at the next action with the page mid-stride.
  const modules = async () => {
    const deadline = Date.now() + MODULE_HARD_MS;
    let idleSince = null;
    while (Date.now() < deadline) {
      if (inFlightScripts > 0) idleSince = null;
      else if (idleSince == null) idleSince = Date.now();
      else if (Date.now() - idleSince >= MODULE_QUIET_MS) return;
      await page.waitForTimeout(20);
    }
  };

  const snap = async (name) => {
    snaps[name] = await page.evaluate(() => window.__snapshot());
  };

  await page.goto(`${BASE}/${sc.route || '#/'}`, { waitUntil: 'domcontentloaded', timeout: 60000 });
  // The frozen clock is load-bearing for every date the application renders, so
  // check it rather than assume it: a drifted or unpaused clock is exactly the
  // failure that produced a wrong recording before, and it is invisible in the
  // transcript until some scenario happens to straddle a day boundary.
  const observed = await page.evaluate(() => Date.now());
  if (observed !== frozen.getTime()) {
    throw new Error(
      `${sc.id}: page clock is ${new Date(observed).toISOString()}, expected ${frozen.toISOString()}`);
  }
  await settle(sc.settleMs);

  for (const action of sc.actions || []) {
    if (action.click != null) {
      const loc = page.locator(action.click);
      await loc.nth(action.nth || 0).click({ timeout: 15000, force: !!action.force });
    } else if (action.fill != null) {
      await page.locator(action.fill).nth(action.nth || 0).fill(String(action.value));
    } else if (action.press != null) {
      await page.locator(action.press).nth(action.nth || 0).press(action.key);
    } else if (action.hover != null) {
      await page.locator(action.hover).nth(action.nth || 0).hover({ timeout: 15000 });
    } else if (action.goto != null) {
      await page.goto(`${BASE}/${action.goto}`, { waitUntil: 'domcontentloaded', timeout: 60000 });
    } else if (action.hash != null) {
      await page.evaluate((hsh) => { window.location.hash = hsh; }, action.hash);
    } else if (action.back) {
      await page.goBack({ timeout: 30000 });
    } else if (action.forward) {
      await page.goForward({ timeout: 30000 });
    } else if (action.resize) {
      await page.setViewportSize({ width: action.resize[0], height: action.resize[1] });
    } else if (action.advance != null) {
      await advance(action.advance, action.settleMs == null ? DEFAULT_SETTLE : action.settleMs);
      continue;
    } else if (action.snap != null) {
      await snap(action.snap);
      continue;
    } else {
      throw new Error(`unknown action: ${JSON.stringify(action)}`);
    }
    // `snap: false` on an action suppresses the settle after it, so a burst of
    // actions can be driven without letting the page catch its breath in
    // between; `snap: "<name>"` settles and then records under that name.
    if (action.snap === false) await modules();
    else await settle(action.settleMs);
    if (typeof action.snap === 'string') await snap(action.snap);
  }

  await snap('final');
  if (COVERAGE_OUT) {
    const cov = await page.evaluate(() => window.__coverage__ || null);
    if (cov) {
      fs.mkdirSync(COVERAGE_OUT, { recursive: true });
      fs.writeFileSync(path.join(COVERAGE_OUT, `${sc.id}.json`), JSON.stringify(cov));
    }
  }
  const result = {
    id: sc.id,
    route: sc.route || '#/',
    clock: sc.clock || DEFAULT_CLOCK,
    viewport: [w, h],
    snaps,
    apiPaths: [...apiPaths].sort(),
    consoleMsgs,
    pageErrors,
    failedRequests: failedRequests.sort(),
  };
  await ctx.close();
  return result;
}

const browser = await chromium.launch({
  args: ['--font-render-hinting=none', '--disable-lcd-text', '--hide-scrollbars'],
});

let next = 0;
const failures = [];
async function worker() {
  for (;;) {
    const i = next++;
    if (i >= scenarios.length) return;
    const sc = scenarios[i];
    try {
      const res = await runScenario(browser, sc);
      fs.writeFileSync(path.join(OUT, `${sc.id}.json`), JSON.stringify(stable(res), null, 1) + '\n');
      process.stdout.write(`ok   ${sc.id}\n`);
    } catch (e) {
      failures.push(`${sc.id}: ${e && e.message ? e.message : e}`);
      process.stdout.write(`FAIL ${sc.id}: ${e && e.message ? e.message : e}\n`);
    }
  }
}
await Promise.all(Array.from({ length: Math.max(1, WORKERS) }, worker));
await browser.close();

if (failures.length) {
  process.stdout.write(`\n${failures.length} scenario(s) failed:\n${failures.join('\n')}\n`);
  process.exit(1);
}
process.stdout.write(`\nall ${scenarios.length} scenario(s) recorded to ${OUT}\n`);
