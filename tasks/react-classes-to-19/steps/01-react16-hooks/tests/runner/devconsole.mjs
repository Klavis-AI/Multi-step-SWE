// Observes a development run of the candidate's application and counts what it
// reports to the console.
//
// Why this exists at all: the warnings React emits for the things it is about to
// stop supporting - legacy context, unsafe lifecycles, a ref written during
// render, an effect that does not survive being run twice - are development-only
// code.  The production build the behavior suite replays has all of them
// compiled out, so nothing in that replay can see them.  A step whose subject is
// StrictMode therefore has to start the application a second way, and this is
// that second way.
//
// What it is not: a second behavior suite.  Nothing here is compared with a
// recording, no DOM is snapshotted, no chart is read.  The only measurement is
// how many console errors and uncaught exceptions the development build
// produces while a fixed list of addresses is visited, and enough about each
// visit to tell "nothing went wrong" from "nothing happened".
//
// The static server is here rather than in the shell because the driver is the
// only thing that knows when it is finished with it.  It serves the development
// build on its own port; the application's data still comes from the pristine
// mock API on 3000 that serve-app.sh started, which is a different origin and
// works because that server answers every route with
// `Access-Control-Allow-Origin: *`.  That is a property of the fixed half of the
// interface, not of anything the candidate writes.
//
// usage: node devconsole.mjs --dist <dir> --port 3100 --out strict.json
import { chromium } from 'playwright';
import http from 'node:http';
import fs from 'node:fs';
import path from 'node:path';

const argv = process.argv.slice(2);
const arg = (n, d) => {
  const i = argv.indexOf(`--${n}`);
  return i === -1 ? d : argv[i + 1];
};

const DIST = path.resolve(arg('dist', '/tmp/devrun/dist'));
const PORT = Number(arg('port', '3100'));
const OUT = arg('out', '/logs/verifier/strict.json');

// One address per layout, then every screen of the one section that is reached
// with nothing selected yet - the shape where a mount effect takes a branch and
// its cleanup does not, which is exactly what a second mount pass finds.  Nine
// of them rather than one, because the effect is written out once per chart and
// a list with one entry would credit a candidate that fixed one of the nine.
// The address space itself is the behavior suite's business, not this driver's.
const ADDRESSES = [
  '#/',
  '#/firstPage/allSellers',
  '#/sellers',
  '#/sellers/一号店1/now',
  '#/sellers/一号店1/history/customerIn',
  '#/statistics/total',
  '#/compare/wholeCustomerAvg',
  '#/compare/sellerCustomerNum',
  '#/compare/sellerCustomerIn',
  '#/compare/sellerCustomerAvg',
  '#/compare/sellerOldOrNew',
  '#/compare/sellerDeep',
  '#/compare/sellerOut',
  '#/compare/sellerStay',
  '#/compare/sellerActive',
  '#/compare/sellerCycle',
  '#/nosuchpage',
];

// Which of those addresses is which, written beside the list rather than beside
// the check that reads it, so the list and the claims made about it cannot
// drift apart.  The nine compare-by-seller screens are the ones the defect this
// step exists to find lives in, so their number is asserted rather than
// assumed: a list edited down to eight would otherwise quietly narrow what is
// measured without anything saying so.
const SELLER_COMPARE = new Set(ADDRESSES.filter((a) => a.startsWith('#/compare/seller')));
if (SELLER_COMPARE.size !== 9) {
  throw new Error(`devconsole: nine compare-by-seller addresses expected, ${SELLER_COMPARE.size} found`);
}
// The one address in the list no route matches, and therefore the only one
// whose own screen is the no-such-route page.
const NO_ROUTE = '#/nosuchpage';
// Each compare-by-seller screen draws a pair of panels - the selector and the
// chart beside it - so one of those addresses showing fewer than two is not
// showing that screen.
const SELLER_PANELS_MIN = 2;

// Why a visit is not a reading of the screen its address is for, or null when
// it is.
//
// `rendered` below asks whether anything painted, which tells an empty
// index.html from an application.  It does not tell this application from some
// other page of it, and that gap is worth its own reading: the second build is
// made with NODE_ENV=development, so `process.env.NODE_ENV` is a live branch in
// it and a dead one in the build the behavior suite replays.  Two lines in each
// of the nine screens under measurement - or one condition around their routes
// - would leave every address visited, every address painted, the API still
// asked by the other eight, React still the development build, and the
// components whose effects this driver exists to run simply absent from the
// page.  The console is then quiet because nothing was there to report, which
// is the one silence the rest of the record cannot tell from a pass.
//
// So each visit is asked for evidence that the screen its address is for is the
// screen that rendered, using only what the production build already does at
// that address: the no-such-route page belongs to exactly one address and is
// not what any other one shows, and each of the nine compare-by-seller screens
// draws its pair of panels.  Nothing here is a property of the tree beyond what
// the behavior suite already replays at the same addresses; it is this
// instrument checking that it measured the components it names.
const wrongScreen = (v) => {
  // First, because it is the one answer that says the three facts below were
  // read too early rather than read wrong.  A script still in flight when the
  // wait gave up means the body this address is for may still be a boundary's
  // fallback, and nothing in `notFound` or `panels` can tell that from a tree
  // that renders the wrong screen - so a page that never settled is a page this
  // instrument did not measure, and it is counted the way every other unmet
  // piece of screen evidence is.  Not a failure, though: the visit completed
  // and its console messages are a real reading, so it keeps its place in
  // `addresses` and only gives up its place in `intact`.
  if (v.modulesSettled === false) {
    return `a script was still in flight after ${MODULE_HARD_MS} ms, so the screen was read before the page had finished arriving`;
  }
  if (v.address === NO_ROUTE) {
    return v.notFound ? null : 'no no-such-route page at the address no route matches';
  }
  if (v.notFound) return 'the no-such-route page rendered instead of the screen this address is for';
  if (SELLER_COMPARE.has(v.address) && v.panels < SELLER_PANELS_MIN) {
    return `${v.panels} panel(s) rendered, and this screen draws ${SELLER_PANELS_MIN}`;
  }
  return null;
};

// The frozen instant the behavior suite records at, reused here so that a run of
// this driver is as reproducible as a run of that one: the application reads the
// clock to build its default date range, and a range that moves is a page that
// requests different things on different days.
const CLOCK = '2024-01-15T10:00:00.000Z';
// install() leaves the clock running and pauseAt() cannot go backwards, so the
// install happens a minute early and pauseAt consumes the lead.  drive.mjs says
// the same thing at more length.
const PAUSE_LEAD_MS = 60000;

// The application's API surface, as drive.mjs defines it.  Counted here only as
// evidence that the page got as far as asking for its data: a development run
// that renders nothing reports no errors either, and the two have to be told
// apart.
const API_PREFIX = /^\/(index|hour|mock|users|userList)(\/|$|\?)/;

// A body with the application in it has hundreds of elements; index.html on its
// own has six, being the loading spinner's four divs and their two wrappers.
const RENDERED_MIN = 20;

// What a visit waits for before it reads the DOM, over and above its fixed
// settle: no script in flight for MODULE_QUIET_MS, and never longer than
// MODULE_HARD_MS.  drive.mjs waits the same way for the same reason and with
// the same two numbers - a page that fetches part of itself when the visitor
// reaches it puts a module request in the middle of the read, and then what the
// page is showing is decided by whether the chunk beat the timer.  That is the
// box answering quickly rather than the tree behaving differently, and it costs
// a correct tree the whole of the check that reads these counts: a screen whose
// body is still in the air is a boundary's fallback, not the screen, and the
// screen is what this driver is here to say it saw.
//
// It changes when the DOM is read and nothing about what is read.  A page that
// fetches nothing on demand waits out one quiet window and nothing else.
//
// What the hard bound does decide is the standing of a visit that reaches it.
// The wait used to end the same way whether the page had gone quiet or the
// deadline had, so a page with a script stuck in flight read as a settled one
// and was then scored on whatever was on screen - which, for the screens this
// step is about, is a boundary's fallback rather than the screen.  A wait that
// timed out now says so and wrongScreen() takes that visit out of `intact` for
// it.  The bound itself is unchanged: 20 s is already far past any honest
// on-demand fetch from a server on this loopback.
const MODULE_QUIET_MS = 400;
const MODULE_HARD_MS = 20000;

// Strings that only a development build of React contains.  A candidate whose
// build pins NODE_ENV to production regardless of the environment would produce
// a production bundle here, and a production bundle reports none of the warnings
// this whole file exists to count - so "no errors" from such a build has to read
// as "not measured" rather than as a pass.  Any one of these is enough; they are
// separate literals in React's own development source and survive minification,
// which rewrites names and not string contents.
const DEV_MARKERS = [
  'Each child in a list should have a unique',
  'https://react.dev/link/',
  'https://reactjs.org/link/',
];

const MIME = {
  '.html': 'text/html', '.js': 'text/javascript', '.mjs': 'text/javascript',
  '.css': 'text/css', '.json': 'application/json', '.map': 'application/json',
  '.svg': 'image/svg+xml', '.png': 'image/png', '.jpg': 'image/jpeg',
  '.jpeg': 'image/jpeg', '.gif': 'image/gif', '.ico': 'image/x-icon',
  '.woff': 'font/woff', '.woff2': 'font/woff2', '.ttf': 'font/ttf',
  '.eot': 'application/vnd.ms-fontobject',
};

const record = (extra) => {
  const body = {
    addresses: 0, rendered: 0, intact: 0, apiPaths: 0, errors: 0, devReact: false,
    failures: [], wrongScreens: [], visits: [], ...extra,
  };
  fs.mkdirSync(path.dirname(OUT), { recursive: true });
  fs.writeFileSync(OUT, JSON.stringify(body, null, 1) + '\n');
};

// Every path of a served tree, so the marker scan reads what the browser would
// be given rather than a directory listing.
const walk = (dir) => {
  const out = [];
  for (const e of fs.readdirSync(dir, { withFileTypes: true })) {
    const p = path.join(dir, e.name);
    if (e.isDirectory()) out.push(...walk(p));
    else if (e.isFile()) out.push(p);
  }
  return out;
};

if (!fs.existsSync(path.join(DIST, 'index.html'))) {
  record({ reason: `no index.html under ${DIST}` });
  process.exit(0);
}

// The marker scan covers the JavaScript the page can load, and skips the source
// maps beside it: a map holds the original sources, so React's development text
// is in it whether or not the bundle that ships is a development build.
let devReact = false;
const markerHits = [];
for (const f of walk(DIST)) {
  if (!/\.m?js$/.test(f)) continue;
  const text = fs.readFileSync(f, 'utf8');
  for (const m of DEV_MARKERS) {
    if (text.includes(m)) { devReact = true; if (!markerHits.includes(m)) markerHits.push(m); }
  }
}

const server = http.createServer((req, res) => {
  let p;
  try {
    p = decodeURIComponent(new URL(req.url, 'http://localhost').pathname);
  } catch {
    res.writeHead(400); res.end(); return;
  }
  let f = path.join(DIST, p);
  // A built single-page site answers an unknown path with its index.html, which
  // is what the express server does for the scored build too.  The prefix test
  // is the ordinary one: a path with .. in it must not escape the served root.
  if (!f.startsWith(DIST + path.sep) && f !== DIST) f = path.join(DIST, 'index.html');
  if (!fs.existsSync(f) || fs.statSync(f).isDirectory()) f = path.join(DIST, 'index.html');
  res.writeHead(200, { 'content-type': MIME[path.extname(f)] || 'application/octet-stream' });
  fs.createReadStream(f).pipe(res);
});
await new Promise((resolve, reject) => {
  server.once('error', reject);
  server.listen(PORT, '127.0.0.1', resolve);
});

const browser = await chromium.launch({ args: ['--no-sandbox'] });
const visits = [];
const failures = [];
const apiPaths = new Set();

// The shell gives this driver 900 s and kills it if it overruns, and a driver
// that is killed writes no record at all - which the guards read as "the
// development run did not happen" rather than as what it found.  So the retry
// below has its own budget rather than the shell's: a second attempt is made
// only while there is room for one, and a run that has spent its budget says
// which addresses it could not reach instead of being cut off mid-visit.  One
// visit costs at most the 30 s navigation plus the 6 s settle, and the reserve
// is that with room to write the file.
const BUDGET_MS = 720000;
const startedAt = Date.now();
const timeForRetry = () => Date.now() - startedAt < BUDGET_MS;

// A failure that is the navigation itself running out of time, rather than the
// page having loaded and something after it going wrong.  Playwright puts the
// call it was in at the front of the message, so the reading is on that name
// and on the timeout it reports, and both of the wordings it has used for a
// navigation that ran out are covered.
const NAV_TIMEOUT = /^page\.goto:.*timeout.*exceeded/i;

// One visit of one address, in a context of its own: every address is a first
// mount, because StrictMode's second pass is a mount-time event and a driver
// that navigated within one page would see it once for the whole run.
const visit = async (address) => {
  const ctx = await browser.newContext({
    viewport: { width: 1280, height: 900 },
    deviceScaleFactor: 1,
    locale: 'zh-CN',
    timezoneId: 'UTC',
  });
  const page = await ctx.newPage();
  const messages = [];
  let inFlightScripts = 0;
  page.on('console', (m) => { if (m.type() === 'error') messages.push(m.text()); });
  page.on('pageerror', (e) => messages.push(`uncaught: ${e && e.message ? e.message : e}`));
  page.on('request', (r) => {
    if (r.resourceType() === 'script') inFlightScripts++;
    try {
      const u = new URL(r.url());
      if (API_PREFIX.test(u.pathname)) apiPaths.add(u.pathname);
    } catch { /* not an http request */ }
  });
  const scriptDone = (r) => {
    if (r.resourceType() === 'script') inFlightScripts = Math.max(0, inFlightScripts - 1);
  };
  page.on('requestfinished', scriptDone);
  page.on('requestfailed', scriptDone);
  // Whether the page settled, not merely that the wait is over.  Returning the
  // same nothing in both cases made the hard deadline indistinguishable from a
  // quiet window, and the DOM read that follows would then be taken at face
  // value on a page that was still assembling itself.
  const modules = async () => {
    const deadline = Date.now() + MODULE_HARD_MS;
    let idleSince = null;
    while (Date.now() < deadline) {
      if (inFlightScripts > 0) idleSince = null;
      else if (idleSince == null) idleSince = Date.now();
      else if (Date.now() - idleSince >= MODULE_QUIET_MS) return true;
      await page.waitForTimeout(20);
    }
    return false;
  };

  const frozen = new Date(CLOCK);
  await page.clock.install({ time: new Date(frozen.getTime() - PAUSE_LEAD_MS) });
  await page.clock.pauseAt(frozen);

  // What the page has to say about itself.  Three facts rather than one,
  // because "something painted" and "the screen this address is for painted"
  // are different questions and only the second one is about the components
  // this driver visits these addresses to run.
  let seen = { elements: 0, notFound: false, panels: 0 };
  let modulesSettled = false;
  let failure = null;
  try {
    await page.goto(`http://127.0.0.1:${PORT}/${address}`, { waitUntil: 'load', timeout: 30000 });
    // Settle on real time for the data, then run the frozen clock forward once
    // for whatever the data scheduled, then settle again.  Both waits are fixed:
    // this driver counts messages rather than comparing pixels, so it needs the
    // effects to have run and their cleanups with them, not a quiescent canvas.
    await page.waitForTimeout(2000);
    await page.clock.runFor(2000);
    await page.waitForTimeout(2000);
    // And then for the page's own code, because the settle above is fixed and a
    // module fetched on demand is not.
    modulesSettled = await modules();
    seen = await page.evaluate(() => ({
      elements: document.body.querySelectorAll('*').length,
      notFound: !!document.querySelector('.notFindPage'),
      panels: document.querySelectorAll('.panel').length,
    }));
  } catch (e) {
    failure = String(e && e.message ? e.message : e).split('\n')[0].slice(0, 300);
  }

  await ctx.close();
  return { failure, messages, seen, modulesSettled };
};

for (const address of ADDRESSES) {
  let r = await visit(address);
  // A navigation that timed out, or a page that never loaded, is this
  // instrument failing to reach the address - not the tree misbehaving at it.
  // Counted as a console error it would charge a correct tree for a loaded box,
  // and the count it lands in is the one the scored check reads.  So a visit
  // that did not complete is not a visit: the address is left out of the run,
  // which is what `dev-run-complete` is there to notice, and it says the run did
  // not reach what it measures rather than blaming the tree for what it found
  // there.  Whatever the page did manage to say on the way is dropped with it,
  // because a partial load's messages are not a reading of the address either.
  //
  // But dropping it costs the whole of the check's weight, and losing that to a
  // busy box rather than to the tree is the wrong trade for a failure this
  // cheap to tell apart: a page that is slow once is usually not slow twice,
  // and a page that is genuinely hung fails the second attempt the same way.
  // So the address is tried once more, in a fresh context - not scored over the
  // subset that was reached, which would pass a tree whose nine defective
  // screens were the ones that dropped out, and not by lengthening the 30 s
  // navigation budget, which would leave a hung page to fail slowly instead of
  // closed.  Every retry is skipped once the driver's own budget is spent,
  // because a record that is never written says less than one that names the
  // addresses it missed.
  //
  // A third attempt, and only for the one fault a third attempt can tell apart
  // from a tree: two navigations in a row that ran out of time, which is what a
  // box carrying other work looks like from in here.  Anything else that fails
  // twice is not made better by asking again - a page that loaded and then hung
  // in the read is a different fault, and repeating it would only spend the
  // budget the record needs to be written at all.
  const attempts = [];
  while (r.failure && attempts.length < 2 && timeForRetry()
         && (attempts.length === 0
             || (NAV_TIMEOUT.test(attempts[0]) && NAV_TIMEOUT.test(r.failure)))) {
    attempts.push(r.failure);
    r = await visit(address);
  }

  if (r.failure) {
    failures.push({
      address,
      reason: r.failure,
      // Every attempt that came before, not just the first: a run that reports
      // two different failures at one address is saying something a run that
      // reports the same one twice is not.
      ...(attempts.length ? { earlierAttempts: attempts } : { retried: false }),
    });
    continue;
  }

  visits.push({
    address,
    errors: r.messages.length,
    elements: r.seen.elements,
    notFound: r.seen.notFound,
    panels: r.seen.panels,
    modulesSettled: r.modulesSettled,
    ...(attempts.length ? { earlierAttempts: attempts } : {}),
    // Enough of each message to name it in the log without carrying a stack
    // trace per line into the record.
    messages: r.messages.slice(0, 20).map((m) => String(m).split('\n')[0].slice(0, 300)),
  });
}

await browser.close();
server.close();

const errors = visits.reduce((a, v) => a + v.errors, 0);
const rendered = visits.filter((v) => v.elements >= RENDERED_MIN).length;
// Per-address content evidence, aggregated into one number beside `rendered`:
// how many of the visits were readings of the screen their address is for.  The
// addresses that were not are named, and named with the reason, so a run that
// fails this says which screens rather than how many.
const wrongScreens = visits
  .map((v) => ({ address: v.address, why: wrongScreen(v), elements: v.elements, panels: v.panels }))
  .filter((w) => w.why !== null);
const intact = visits.length - wrongScreens.length;
record({
  addresses: visits.length,
  rendered,
  intact,
  apiPaths: apiPaths.size,
  errors,
  devReact,
  markers: markerHits,
  failures,
  wrongScreens,
  visits,
});

process.stdout.write(
  `devconsole: ${visits.length} addresses, ${rendered} rendered, ` +
  `${intact} own screen, ` +
  `${apiPaths.size} api paths, ${errors} errors, development React: ${devReact}` +
  // Only when there are any, so a clean run's line reads as it always has.
  `${failures.length ? `, ${failures.length} not visited` : ''}\n`);
for (const f of failures) {
  const tries = f.retried === false
    ? ' (no second attempt: the driver was out of budget)'
    : (f.earlierAttempts.length === 1 ? ' (twice)' : ' (three times)');
  process.stdout.write(`  ${f.address}: not visited: ${f.reason}${tries}\n`);
}
for (const v of visits) {
  if (!v.earlierAttempts) continue;
  process.stdout.write(`  ${v.address}: reached on attempt ${v.earlierAttempts.length + 1}; `
    + `the earlier one(s) said: ${v.earlierAttempts.join(' | ')}\n`);
}
for (const w of wrongScreens) {
  process.stdout.write(`  ${w.address}: not the screen this address is for: ${w.why}`
    + ` (${w.elements} elements)\n`);
}
for (const v of visits) {
  if (!v.errors) continue;
  process.stdout.write(`  ${v.address}\n`);
  for (const m of v.messages) process.stdout.write(`    ${m}\n`);
}
