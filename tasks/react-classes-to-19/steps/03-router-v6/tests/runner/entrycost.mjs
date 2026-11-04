// Measures what a visitor actually waits for before the application's first
// screen appears: every piece of javascript the browser fetched between opening
// the page and that screen being on it, summed gzipped.
//
// Why a browser rather than a walk of the build output.  The property §3 of the
// instruction is about - what a visitor downloads before anything renders - was
// read statically: the script index.html names, plus everything it reaches
// through a static import, and nothing behind a dynamic one.  That is the right
// number for every honest tree and the wrong number for one particular
// dishonest one.  A five-line root module whose whole body is
// `lazy(() => import('./components/app.js'))` under a boundary, with index.html
// pointed at it, statically reaches no module of the tree at all: the static
// entry reads 46,628 gzipped bytes, the source-side guard counts zero chart
// panels in the entry's static reach, and all twelve panels are still ordinary
// static imports of the module behind the stub, so echarts is still the first
// thing a visitor downloads.  Nothing was split; sixteen weight was paid for it.
//
// The browser is the one instrument that cannot be talked out of it, because it
// is the thing doing the waiting.  A trampoline pays for the stub and for the
// chunk behind it - both are in front of the first screen - while the golden
// pays for its entry chunk and nothing else.  Measured with this file, gzip
// level 9 in both columns:
//
//   dist under test             static entry.gzip   fetched before the screen
//   golden tree-05                        111,290                     111,325
//   tree-04, the tree step 5 starts from  242,149                     242,149
//   the trampoline above                   46,628                     242,607
//
// The two honest trees agree to within a rounding error, which is the point:
// on a tree that did not lie these are the same measurement, so nothing about
// grading moves and only the trampoline stops working.
//
// What this does not do is serve anything.  The step's own server is already up
// on PORT with the candidate's dist/ as its static root and the mock API on the
// same origin, exactly as the recordings were made, and serve-app.sh started it
// before the replay.  This driver visits that server; devconsole.mjs stands up
// its own only because the build it runs is not the one being served.
//
// usage: node entrycost.mjs --base http://127.0.0.1:3000 --dist /app/dist
//                           --out entrycost.json
//                           [--address '#/nosuchpage']
//                           [--selector '.notFindPage']
import { chromium } from 'playwright';
import fs from 'node:fs';
import path from 'node:path';
import zlib from 'node:zlib';

const argv = process.argv.slice(2);
const arg = (n, d) => {
  const i = argv.indexOf(`--${n}`);
  return i === -1 ? d : argv[i + 1];
};

const BASE = String(arg('base', 'http://127.0.0.1:3000')).replace(/\/+$/, '');
const DIST = path.resolve(arg('dist', '/app/dist'));
const OUT = arg('out', '/logs/verifier/entrycost.json');

// The address the measurement is taken at, and the screen that ends it.
//
// The application's own no-such-route page rather than one of its screens: every
// tree in this lineage has it, it draws no chart, it asks the API for nothing,
// and it is not a screen a correct migration could legitimately put behind a
// boundary of its own - so the first thing painted there is the first thing
// painted, and a boundary's fallback is not mistakable for it.  It is also the
// address devconsole.mjs already visits for the same reason, and the address
// the behavior suite already records at, so it is a screen the recordings hold
// this tree to rather than one this file invented.
//
// Both are arguments because the address space belongs to the behavior suite
// and not to this driver; the defaults are what the shell passes.
const ADDRESS = arg('address', '#/nosuchpage');
const SELECTOR = arg('selector', '.notFindPage');

// How long the screen is waited for, how often it is looked for, and how long
// the page is left running afterwards.  The cap is generous because it is not a
// threshold - a run that reaches it says the screen was never seen, and the
// guard that reads that refuses the family rather than scoring the tree - and
// the poll is short because the number being cut is a byte count at an instant,
// so a coarse poll would let a chunk that arrived after the screen be charged to
// a visitor who never waited for it.
const SCREEN_TIMEOUT_MS = 30000;
const SCREEN_POLL_MS = 25;
// The settle is for the record's second pair of numbers only.  Nothing is scored
// on what arrives after the screen; it is here so that a run whose before and
// after totals are the same can be told from one where the page went on
// fetching, which is the difference between a tree that split nothing and a tree
// that split everything.
const SETTLE_MS = 2500;

// The same compression the static metric uses - gates/artifacts.mjs:68 calls
// zlib.gzipSync(buf, { level: 9 }) - so that this number and entry.gzip are
// comparable rather than merely similar.  A different level would put the two
// measurements a few per cent apart for no reason a reader could chase down.
const GZIP_LEVEL = 9;

const VIEWPORT = { width: 1280, height: 800 };

// The audit trail is bounded: a page that asked for a thousand chunks has
// already said what it had to say in the first two hundred, and the record is
// read by a human looking for which file cost what.
const REQUESTS_MAX = 200;

// What counts as javascript the visitor waited for.  The resource type is what
// the browser itself calls the request, which covers a module fetched by
// `import()` with no extension in its url; the suffix test covers the reverse,
// a `.js` the browser classified as something else because of how it was asked
// for.  Either is enough.
const JS_SUFFIX = /\.m?js$/;

// The record, with every field present whatever happened.  The defaults are the
// invariant this file exists to hold: an unmeasurable run must not read as a
// small one.  Both byte counts default to null rather than 0, because json-flag
// scores a non-finite value 0 and a 0 would be a perfect score for a run that
// measured nothing at all - which is exactly the shape of failure the static
// metric had.
const record = (extra) => {
  const body = {
    ok: false,
    base: BASE,
    address: ADDRESS,
    selector: SELECTOR,
    screenSeen: false,
    msToScreen: null,
    jsGzipBeforeScreen: null,
    jsCountBeforeScreen: 0,
    complete: false,
    unreadable: [],
    offOrigin: [],
    jsGzipTotal: null,
    jsCountTotal: 0,
    requests: [],
    ...extra,
  };
  fs.mkdirSync(path.dirname(OUT), { recursive: true });
  fs.writeFileSync(OUT, JSON.stringify(body, null, 1) + '\n');
};

const originOf = (url) => {
  try {
    return new URL(url).origin;
  } catch {
    return null;
  }
};
const BASE_ORIGIN = originOf(BASE);

// The gzipped size of the file the server would have answered this request
// with.  --dist is the directory serve-app.sh copied into the server's static
// root, so the request's own pathname is the path under it; a pathname that
// climbs out of the served root is not a file this build emitted and is sized
// as if it could not be read.
const gzipFromDist = (url) => {
  let p;
  try {
    p = decodeURIComponent(new URL(url).pathname);
  } catch {
    return null;
  }
  const f = path.resolve(DIST, `.${p}`);
  if (f !== DIST && !f.startsWith(DIST + path.sep)) return null;
  try {
    const st = fs.statSync(f);
    if (!st.isFile()) return null;
    return zlib.gzipSync(fs.readFileSync(f), { level: GZIP_LEVEL }).length;
  } catch {
    return null;
  }
};

const main = async () => {
  const browser = await chromium.launch({ args: ['--hide-scrollbars'] });
  const ctx = await browser.newContext({ viewport: VIEWPORT, deviceScaleFactor: 1 });
  const page = await ctx.newPage();

  // Every request the page makes, in the order it made them, with the instant it
  // was made.  Every request rather than the javascript ones: what counts is
  // decided below, off the same list, so the audit trail and the tally cannot
  // disagree about what the page asked for.
  //
  // No clock is installed here, unlike the behavior suite and the development
  // run.  This stage measures bytes, and the application's polling changes when
  // it asks for data and not which javascript it is made of.
  const requests = [];
  page.on('request', (r) => {
    requests.push({ url: r.url(), type: r.resourceType(), at: Date.now() });
  });
  // Response headers, kept only so that a request this driver cannot size from
  // the build output has somewhere else to be sized from.  Keyed by url, last
  // answer winning, which is the right reading for a url asked for twice.
  const headers = new Map();
  page.on('response', (r) => {
    try {
      headers.set(r.url(), r.headers());
    } catch { /* the response went away before its headers could be read */ }
  });

  const navStart = Date.now();
  let screenAt = null;
  let reason = null;
  try {
    // `commit` rather than `load`: the navigation resolves as soon as the
    // document starts arriving, so the poll below starts before the page has
    // fetched anything and nothing in front of the first screen is missed.
    await page.goto(`${BASE}/${ADDRESS}`, { waitUntil: 'commit' });

    const deadline = navStart + SCREEN_TIMEOUT_MS;
    while (Date.now() < deadline) {
      let there = false;
      try {
        there = await page.evaluate((s) => !!document.querySelector(s), SELECTOR);
      } catch { /* the document was replaced mid-poll; ask again */ }
      if (there) { screenAt = Date.now(); break; }
      await page.waitForTimeout(SCREEN_POLL_MS);
    }

    await page.waitForTimeout(SETTLE_MS);
  } catch (e) {
    reason = String(e && e.message ? e.message : e).split('\n')[0].slice(0, 300);
  }

  await browser.close();

  // The cut is the moment the screen appeared, not the moment the page settled,
  // and a request counts against it by when it *started*.  A chunk asked for
  // before the screen was on the page is something the visitor was waiting on
  // whether or not it had finished arriving, and a tree that split nothing but
  // delivered its entry slowly must not be credited for the delivery.  When the
  // screen was never seen there is no such instant, so the cut is where the poll
  // gave up - the record says `screenSeen: false` beside it and the guard that
  // reads that refuses the family, so the number is diagnosis rather than a
  // score.
  const cutAt = screenAt == null ? Math.min(Date.now(), navStart + SCREEN_TIMEOUT_MS) : screenAt;

  let beforeGzip = 0;
  let beforeCount = 0;
  let totalGzip = 0;
  let totalCount = 0;
  let complete = true;
  const unreadable = [];
  const offOrigin = [];
  const trail = [];

  for (const r of requests) {
    let pathname = null;
    try {
      pathname = new URL(r.url).pathname;
    } catch { /* not an http request; the type test below still applies */ }
    const isJs = r.type === 'script' || (pathname != null && JS_SUFFIX.test(pathname));
    if (!isJs) continue;

    const same = BASE_ORIGIN != null && originOf(r.url) === BASE_ORIGIN;
    // Sized from the build output when it can be, because that is the number
    // entry.gzip is made of and the two have to be comparable.  Otherwise from
    // what the server said it was sending: another origin's file is not under
    // --dist at all, and a same-origin path that names nothing there is a file
    // this walk cannot see.  Both land in `offOrigin` - the list of counted
    // requests whose size came from the wire rather than from the build output -
    // and which of the two it was is in the trail's `origin` beside it.
    let gzip = same ? gzipFromDist(r.url) : null;
    let sizedFrom = 'dist';
    if (gzip == null) {
      const len = Number((headers.get(r.url) || {})['content-length']);
      gzip = Number.isFinite(len) ? len : null;
      sizedFrom = 'content-length';
      if (gzip != null) offOrigin.push(r.url);
    }
    // Neither: this request has no size, so the run has a hole in it.  A byte
    // count with a hole in it is not a byte count, and `complete` is what says
    // so - `entry-cost-measured` reads it and refuses the family rather than
    // letting the missing bytes read as bytes that were never fetched.
    if (gzip == null) {
      unreadable.push(r.url);
      complete = false;
      sizedFrom = 'none';
    }

    const before = r.at <= cutAt;
    totalCount += 1;
    totalGzip += gzip || 0;
    if (before) {
      beforeCount += 1;
      beforeGzip += gzip || 0;
    }
    if (trail.length < REQUESTS_MAX) {
      trail.push({
        // The pathname is enough to name a file of this build; another origin's
        // request is named in full, because which origin it was is the fact.
        path: same && pathname != null ? pathname : r.url,
        gzip,
        origin: same ? 'same' : 'other',
        beforeScreen: before,
        ...(sizedFrom === 'dist' ? {} : { sizedFrom }),
      });
    }
  }

  record({
    ok: reason == null,
    ...(reason == null ? {} : { reason }),
    screenSeen: screenAt != null,
    msToScreen: screenAt == null ? null : screenAt - navStart,
    // Null rather than the running sum when the run has a hole in it: a total
    // that is missing a chunk is smaller than the truth, and smaller is the
    // direction that pays.
    jsGzipBeforeScreen: complete ? beforeGzip : null,
    jsCountBeforeScreen: beforeCount,
    complete,
    unreadable,
    offOrigin,
    jsGzipTotal: complete ? totalGzip : null,
    jsCountTotal: totalCount,
    // What arrived *after* the screen the wait ended on, which is the only
    // honest way to say "the cut was not moved".  The guard beside the budget
    // used to test the settled total against the same wall the cut is scored
    // against, and that cannot see the case it exists for: a tree that paints
    // something matching the selector early and then goes on fetching is inside
    // the wall on both readings and passes.  A tree that reached its screen
    // honestly reads zero here - the reference solution does, at both addresses
    // - so this costs an in-budget tree nothing and costs a moved cut exactly
    // the bytes it moved.  Null when the run has a hole in it, for the same
    // reason the two sums are: a difference of two numbers one of which is
    // missing a chunk is not a measurement.
    jsGzipAfterScreen: complete ? totalGzip - beforeGzip : null,
    requests: trail,
  });

  process.stdout.write(
    `entrycost: ${complete ? beforeGzip : 'unmeasured'} gzipped bytes of javascript in `
    + `${beforeCount} request(s) before ${SELECTOR} appeared`
    + `${screenAt == null ? ' (it never did)' : ` (${screenAt - navStart} ms)`}`
    + `; settled at ${complete ? totalGzip : 'unmeasured'} in ${totalCount}`
    + `${unreadable.length ? `, ${unreadable.length} unsized` : ''}`
    + `${offOrigin.length ? `, ${offOrigin.length} sized from the wire` : ''}`
    + `${reason == null ? '' : `; ${reason}`}\n`);
};

try {
  await main();
} catch (e) {
  // A driver that dies writes a record saying so rather than none: the guards
  // read "this did not happen" off the fields, and a file that is not there
  // leaves json-flag deciding what a missing path means.
  record({ reason: String(e && e.message ? e.message : e).split('\n')[0].slice(0, 300) });
  process.stdout.write('entrycost: the measurement did not run\n');
}
