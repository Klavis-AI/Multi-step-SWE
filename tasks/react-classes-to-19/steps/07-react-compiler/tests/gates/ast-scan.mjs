// Parses every source file in the repository once and answers every AST-level
// migrated-ness question from that one parse.
//
// Scope: everything inside the repository except installed dependencies
// (node_modules), version-control metadata, and the two directories the
// interface defines as build output - `dist/`, where the build writes, and
// `server/demo1/public`, which the README describes as a copy of the packaged
// dist.  Both hold generated bundles of third-party code; the artifact-level
// gates read them deliberately, with queries written for bundled code.
// Vendored *source* is in scope like any other source.
//
// Every query reports individual *sites* - a file and a line - never files or
// blocks, so consolidating ten class components into one file does not turn ten
// findings into one.  Comment-based suppressions are collected as sites too: a
// comment that hides a finding from a tool does not change the property.
//
// Every count is reported once, per file, over the whole repository.  "Is this
// construct gone?" is a question about the repository - a surviving class in a
// file nothing imports is still a surviving class.  "Did the new idiom replace
// the old one?" is a question about the code that runs, and this scan does not
// answer it: which files the served page is built from is measured by rebuilding
// the tree (see provenance.mjs), and run-gates.mjs restricts these per-file
// sites to that set.  Answering it here too, with a resolver that guesses what
// the bundler would have done, would be a second and weaker answer to a question
// already answered by the bundler itself.
//
// Usage: node ast-scan.mjs <repo-root> [--json <path>]
import fs from 'node:fs';
import path from 'node:path';
import { parse } from '@babel/parser';
import { htmlScripts, inlineScripts, VITE_NOT_JS } from './reach.mjs';

const ROOT = path.resolve(process.argv[2] || '/app');
const flag = (name) => {
  const i = process.argv.indexOf(name);
  return i === -1 ? null : process.argv[i + 1];
};
const JSON_OUT = flag('--json');

// Installed packages and version-control metadata, and nothing else: the
// instruction puts every JavaScript file in the repository in scope. Tool-cache
// names (`.vite`, `.cache`) used to be skipped too, at any depth, and a class
// component parked under `src/.cache/` was then compiled into the page while
// no source gate read it; provenance.mjs already marks those directories as
// this tree's JavaScript. A nested `node_modules` stays unread here (a package
// install the candidate made for the mock server is not their source), and
// provenance.mjs fails `built-from-source` if anything under one reaches the
// page. (wave D 2026-10-12, final review C1)
// `.git` is skipped only at the repository root, where it is version-control
// metadata. A `.git` directory below the root used to be skipped by name too,
// and a class component parked under `src/.git/` was then compiled into the
// page while no source gate read it; it is read like any other directory now,
// and provenance.mjs marks JavaScript under any `.git` - the root one included -
// as foreign, so none of it reaches the page unread. (wave H 2026-10-13, C-1)
const SKIP = new Set(['node_modules']);
// Build output, by the interface's own definition of where the build lands,
// and the repository root's own version-control directory.
const SKIP_PATHS = new Set(['dist', '.git', path.join('server', 'demo1', 'public')]);
// `.mts`/`.cts` too: Vite compiles them with no configuration, and a module
// the build compiles is in scope whatever it is called. (wave F 2026-10-13)
const SOURCE = /\.(js|jsx|mjs|cjs|ts|tsx|mts|cts)$/;
// HTML files, for the scripts written inline in them (see the main
// section).  Every one in the scanned tree, not only the ones the build takes as
// entries: a module script in any HTML file of the repository is JavaScript in
// the repository.  (wave H 2026-10-14, H-CR7)
const HTML_NAME = /\.html$/i;

const PLUGINS = [
  'jsx', 'typescript', 'classProperties', 'classPrivateProperties',
  'classPrivateMethods', 'classStaticBlock', 'decorators-legacy',
  'objectRestSpread', 'asyncGenerators', 'dynamicImport', 'exportDefaultFrom',
  'exportNamespaceFrom', 'functionBind', 'importAssertions', 'optionalChaining',
  'nullishCoalescingOperator', 'topLevelAwait', 'importMeta',
];

// Three dialects, tried in order: TypeScript with JSX (the list above), JSX
// alone, then plain JavaScript.  Babel's TypeScript dialect rejects some valid
// JavaScript - `a ? (b) : c => d` reads as an arrow with a return type - and a
// `.js` file it rejected was neither read here nor marked by provenance.mjs
// while Vite compiled it into the page (final review G2-R1c).  The first
// dialect that parses with no error wins; failing that, the first that did not
// throw; and if every one throws, the first dialect's error is the one raised.
// A file the first dialect parses cleanly is parsed exactly as before.
// provenance.mjs carries the same function.  (wave H 2026-10-14, H-CR6)
// Two more are appended, plain JavaScript and then TypeScript with JSX, each
// with the current decorators proposal in place of the legacy one and with the
// two import-phase proposals.  Rollup 4.63 (the version every reference lock
// resolves) accepts `export @dec class` and `import defer * as ns`, and the
// three dialects above reject both, so such a file was unmarked when its name
// was not SOURCE's and unread when it was (final review G2-R1d).  Appended
// rather than merged: "first clean dialect wins", so every file the first three
// read is read exactly as before.  (wave H 2026-10-14, H-CR7)
const PROPOSALS = (list) => [
  ...list.filter((p) => p !== 'decorators-legacy'),
  ['decorators', { version: '2023-11' }], 'deferredImportEvaluation', 'sourcePhaseImports',
];
const DIALECTS = [
  PLUGINS,
  PLUGINS.filter((p) => p !== 'typescript'),
  PLUGINS.filter((p) => p !== 'typescript' && p !== 'jsx'),
  PROPOSALS(PLUGINS.filter((p) => p !== 'typescript' && p !== 'jsx')),
  PROPOSALS(PLUGINS),
];
function parseAnyDialect(src, options) {
  let recovered = null;
  let firstError = null;
  for (const plugins of DIALECTS) {
    let ast;
    try {
      ast = parse(src, { ...options, plugins });
    } catch (e) {
      if (!firstError) firstError = e;
      continue;
    }
    if (!ast.errors || ast.errors.length === 0) return ast;
    if (!recovered) recovered = ast;
  }
  if (recovered) return recovered;
  throw firstError;
}

// ---------------------------------------------------------------- collection

// Every gate this scanner can report, declared rather than accumulated.  A gate
// that found nothing has to appear in the output with a count of 0: if the map
// only held the ids that happened to match, then a renamed query and a genuinely
// clean tree would be the same absent key, and a consumer reading the absence as
// zero would score full marks for a gate that stopped being asked.  run-gates
// rejects an id that is not in this list for the same reason, from the other
// side.
const GATE_IDS = [
  'action-hooks', 'class-components', 'clone-element', 'concurrent-hooks',
  'connect-hoc', 'console-suppressions', 'context-provider-member',
  'create-root', 'default-props',
  'find-dom-node', 'forward-ref', 'hand-rolled-polling', 'legacy-context',
  'legacy-render',
  'manual-memo', 'pkg-immutable', 'pkg-legacy-test-tools', 'pkg-prop-types',
  'pkg-recompose', 'pkg-redux-thunk', 'pkg-transition-group', 'prop-types',
  'prop-types-standalone',
  'hook-suppressions',
  'react-hooks', 'react-icons-deep-import', 'react-proptypes',
  'react-router-dom', 'redux-legacy-api', 'retired-packages',
  'retired-router-api', 'root-render-unwrapped', 'router-modern-api',
  'rtk-api', 'string-refs',
  'strict-mode', 'strict-mode-root', 'suspense', 'unsafe-lifecycles', 'use-hook',
  // The state step's surface: what a hand-rolled data layer looks like
  // (`raw-fetch`, `thunk-creator`, `hand-built-action`, `switch-reducer`,
  // `timer-driven-fetch`) and what a declarative one looks like
  // (`rtk-query-def`, `rtk-query-hooks`, `polling-config`).
  'hand-built-action', 'polling-config', 'raw-fetch', 'rtk-query-def',
  'rtk-query-hooks', 'switch-reducer', 'thunk-creator', 'timer-driven-fetch',
  // The router step's surface.  `retired-router-api` above already counts what
  // a react-router 3 route table imports and the `activeClassName` it hangs on
  // its links; `route-get-component` is the one v3 construct left, and the rest
  // are the v6 pieces that replace them, each its own id because a step can
  // require the outlet without requiring the link and a single router count
  // could not be allocated to either.
  'outlet-context', 'route-get-component', 'route-lazy', 'router-data-api',
  'router-navigate', 'router-navlink', 'router-outlet', 'router-params-hook',
  // `route-loader` is the v6 surface that fetches *for* a route, which is the
  // one piece of the new major the router step must not adopt: the step before
  // it put every request behind one data layer, and a loader moves requests
  // back out of it.  Its own id rather than a member of `router-modern-api`,
  // because every other name in that set is something a step asks for.
  'route-loader',
  // Step 5's surface.  `route-lazy` above counts both of the code splits this
  // application has - the `lazy` property on a `<Route>` and a call to React's
  // own `lazy` - because step 3's question was whether the routes are split at
  // all, and either spelling answers it.  Step 5's question is narrower and the
  // two are not interchangeable there: a route's `lazy` is awaited as part of
  // the navigation, React's commits the shell and fills the body in, and the
  // step is about the second.  So `react-lazy` is the React call on its own,
  // beside the count it is a subset of rather than instead of it - `route-lazy`
  // keeps its meaning and its value on every tree steps 1-4 already score.
  //
  // `transition-start` and `deferred-value-used` are the same idea applied to
  // `concurrent-hooks`: that id counts the two hooks being *called*, which a
  // `useDeferredValue(x)` nothing reads and a `useTransition()` never started
  // both satisfy while deferring nothing.  These two say the result is used.
  'react-lazy', 'transition-start', 'deferred-value-used',
  // And their union, which is the marker for "this step has begun" rather than
  // a floor of its own.  `concurrent-hooks` cannot be that marker: it counts
  // the calls, so one `useTransition()` whose tuple is thrown away - a line
  // that defers nothing, in any module of an otherwise untouched tree - opened
  // every check that only asks the tree to have broken nothing.  This id says
  // the same thing the two above say separately, in one number a guard can
  // read: at least one of the hooks is not merely called, its result is read or
  // its transition is started.
  'concurrent-hooks-used',
  // And the one thing about step 5 that is not a construct at all.
  // `entry-echarts` counts the modules that import the chart library *and* are
  // reached from the entry through a chain of static imports - the sentence the
  // instruction opens the section with, read off the source: "every module
  // app.js still imports statically is in the entry chunk with it ... each of
  // them imports echarts, so echarts is in the entry chunk".  It is here
  // because the constructs alone cannot say whether this step happened on a
  // tree that arrived at step 5 having already adopted React's `lazy` for
  // something else: `react-lazy` counts a call, and a call that was there
  // before step 5 began counts the same as one this step added.  What no tree
  // can have before this step is the twelve chart panels out of the entry's
  // static reach.
  'entry-echarts',
  // And one more thing that is not a construct of a module at all.
  // `console-suppressions` above counts the code writing over a console method;
  // this one counts the *build* being configured to take the calls out before
  // they ever run - `drop: ['console']` and its relatives `pure` and `define` in
  // the bundler's config, which this build tool applies to the development
  // transform as well as to the production one, so a development run reports
  // nothing because there is nothing left to report.  Its own id rather than a
  // member of `console-suppressions` because the two are answerable separately:
  // that one is a property of a module's code and this one of the build's
  // configuration, and a tree can do either without doing the other.
  'build-console-dropped',
  // And back to a construct of a module again, though not one of its calls.
  // The React Compiler reads directives out of the source of the function they
  // sit in: `use no memo` and `use no forget` ask it to leave that function
  // alone, `use memo` and `use forget` ask it to take one it would have passed
  // over, and `use memo if(flag)` puts the choice behind a run-time flag.  Its
  // own id rather than a member of anything that reads how the compiler was
  // turned on, for the same reason `build-console-dropped` is not a member of
  // `console-suppressions` and read the same way round: that id is the build's
  // configuration and this one is what a module says about itself, and a tree
  // can switch the compiler on for the whole build and still hold modules that
  // have written themselves out of it.
  'compiler-opt-out',
  // The effects step's one construct that is an *absence*.  `exhaustive-deps`
  // asks whether a dependency list names everything the body reads, and it has
  // nothing to ask of an effect that has no list: no list means "after every
  // render", which is never wrong about its dependencies.  So the rule is
  // silent on `useEffect(fn)` and loud on `useEffect(fn, [a])` that also reads
  // `b`, and a tree can empty the rule out by deleting the lists rather than by
  // fixing them.  This counts the effects that would have nothing to be asked:
  // a call to one of the effect hooks with no second argument.
  //
  // One shape is excluded, because it is the case where no list is the right
  // answer rather than the evasion: an effect whose whole body is assignments
  // to `something.current`, which is how a component keeps a ref pointing at
  // the latest value of a prop or a callback.  That effect has to run after
  // every render - that is its entire purpose - and React's own guidance
  // writes it with no list.  Anything else in the body and it counts.
  'effect-every-render',
  // The other half of the same evasion, and the one the lint rule cannot see.
  // `set-state-in-effect` asks whether an effect's body writes state, and it
  // reads the body: a write moved into a callback the body merely schedules is
  // not in the body any more, so the rule goes quiet while the derived state
  // stays exactly where it was.  This counts a state setter reached from an
  // effect through a deferral that carries no outside event with it - a timer
  // set to fire immediately, a microtask, a frame, an idle callback, a promise
  // the same line resolved.  Those are the render's own work put off until
  // after the render, which is what the derived-state effect already was.
  //
  // An effect that writes state because something outside React reported is
  // not this and is not counted: a response, a subscription, or a timer that
  // is doing the waiting the component exists to do, which is why the interval
  // that advances a carousel reads here the same as it always did.  The
  // separation is by what the deferral is, not by whether a deferral is there.
  'effect-defers-set-state',
  // Not a construct of React at all: a module imported from a `data:` URL, by a
  // static import, a re-export or a dynamic import with a literal specifier, or
  // named by an HTML file's `<script src>`.  The bundler compiles that code into
  // the page, and it is in no file of this tree, so no gate here reads it.
  // run-gates.mjs fails `built-from-source` on any site, as it does for a
  // foreign provenance marker in the page.  (wave H 2026-10-14, H-CR7, final
  // review G2-R6)
  'data-uri-import',
  // And a module imported from one of the two directories exempt from
  // provenance marking, the root `.git` and `server/demo1/public`: code no
  // gate reads and no marker follows.  Failed the same way.  (wave H
  // 2026-10-14, H-CR8, final review G2-R7c)
  'exempt-dir-import',
];

// `use-hook` above is React 19's `use()` and nothing else.  "The tree contains
// hooks at all" is a different question with a different answer at every step,
// and conflating the two would have let step 1's "hooks are present" gate be
// satisfied only by a React 19 API that does not exist until step 6.  These are
// the hook calls that make a function component a function component.
// The three hooks that take a dependency list as their second argument and run
// their body for its effect.  `useMemo` and `useCallback` take one too, but a
// call to either with no list is a syntax error in practice - it returns the
// factory's first result forever - and neither survives step 7 anyway.
const EFFECT_HOOKS = new Set(['useEffect', 'useLayoutEffect', 'useInsertionEffect']);

// Scheduling calls that carry no news from outside with them, by bare name -
// the object they are reached through is a separate question, asked below.
// `setInterval` is deliberately absent: an interval always means repetition,
// and repetition is a reason.  `setTimeout` is absent too, because whether a
// timeout is one of these depends on its delay.
const IMMEDIATE_SCHEDULERS = new Set([
  'queueMicrotask', 'requestAnimationFrame', 'requestIdleCallback',
  'setImmediate',
]);

// The objects a global scheduler can be reached through.  Listing only `window`
// would leave the whole pass one keyword from reading zero.
const GLOBAL_HOSTS = new Set(['window', 'globalThis', 'self', 'global']);

// A delay that is not a wait: absent, or a literal no longer than a frame.
// Sixteen milliseconds is one frame at 60Hz and is the conventional boundary
// between "as soon as possible" and a pause somebody chose; below it a timer is
// the same statement written one task later.  A delay the scan cannot read -
// `setTimeout(fn, DELAY)` with `DELAY` bound elsewhere, or computed - is also
// counted, because a component that really waits can say how long in a number,
// and the alternative is a gate that any local constant walks past.  Only a
// literal above a frame is a demonstrated wait.
function isImmediateDelay(d) {
  if (d === undefined || d === null) return true;
  if (d.type === 'NumericLiteral') return d.value <= 16;
  if (d.type === 'UnaryExpression' && (d.operator === '+' || d.operator === '-')
      && d.argument && d.argument.type === 'NumericLiteral') {
    return d.argument.value <= 16;
  }
  return true;
}

// `Promise.resolve(...).then(cb)` and its `.finally` sibling: a promise the same
// expression created already settled, so the callback is this render's work on
// the microtask queue and nothing has been waited for.  A `.then` on anything
// else - a fetch, a query's promise, a value that came from outside - is a
// response, and a response is the case the instruction exempts.
function isSettledPromiseThen(call) {
  const c = call.callee;
  if (!c || c.type !== 'MemberExpression' || c.computed) return false;
  if (c.property.type !== 'Identifier') return false;
  if (c.property.name !== 'then' && c.property.name !== 'finally') return false;
  const recv = c.object;
  if (!recv || recv.type !== 'CallExpression') return false;
  const rn = calleeName(recv);
  return rn === 'Promise.resolve' || rn === 'Promise.all';
}

// Whether a call schedules the render's own work for immediately afterwards,
// and if so which argument is the work.  One question asked about the call -
// what object, what scheduler, what delay - rather than a list of spellings to
// match, because a list is what a candidate reads off and steps around: a
// matcher that knows `window.setTimeout` and not `globalThis.setTimeout` scores
// the honest spelling and nothing else.  `aliases` carries local names bound
// straight to a scheduler, resolved by the caller.
function immediateDeferral(call, aliases) {
  const name = calleeName(call);
  if (name) {
    const dot = name.indexOf('.');
    const host = dot < 0 ? null : name.slice(0, dot);
    const tail = dot < 0 ? name : name.slice(dot + 1);
    if (host === null || GLOBAL_HOSTS.has(host)) {
      const kind = aliases.get(tail) || tail;
      if (kind === 'setTimeout') {
        return isImmediateDelay((call.arguments || [])[1])
          ? (call.arguments || [])[0] || null : null;
      }
      if (IMMEDIATE_SCHEDULERS.has(kind)) return (call.arguments || [])[0] || null;
    }
  }
  if (isSettledPromiseThen(call)) return (call.arguments || [])[0] || null;
  return null;
}

// Calls that hand a function to something that will run it when that something
// has news: a listener, a subscription, an observer, a response, a repetition,
// a wait somebody chose the length of.  The deferral pass does not look inside
// such a callback at all.  What a resize handler does is caused by the resize,
// even when the handler asks for a frame before it does it, and §2 exempts the
// write that news from outside caused - so counting a scheduler nested in a
// listener would be reading the exemption as the violation.
const HANDOFF_CALLS = new Set([
  'addEventListener', 'addListener', 'on', 'once', 'subscribe', 'observe',
  'then', 'catch', 'finally', 'setInterval',
]);

function isHandoff(call, aliases) {
  if (immediateDeferral(call, aliases)) return false;
  const name = calleeName(call) || '';
  const dot = name.lastIndexOf('.');
  const tail = dot < 0 ? name : name.slice(dot + 1);
  const kind = aliases.get(tail) || tail;
  // A timeout that is not immediate reached here, which means its delay is a
  // literal longer than a frame: a wait somebody wrote down.
  return kind === 'setTimeout' || HANDOFF_CALLS.has(kind);
}

// Is this the "keep a ref pointing at the latest one" effect?  True when the
// callback's body is one or more assignments to a `.current` and nothing else:
// no declarations, no calls, no cleanup return.  A concise arrow body counts as
// the single statement it is.
function keepsRefsCurrent(fn) {
  if (!fn || (fn.type !== 'ArrowFunctionExpression' && fn.type !== 'FunctionExpression')) return false;
  const body = fn.body;
  const isRefAssign = (e) => e && e.type === 'AssignmentExpression' && e.operator === '='
    && e.left.type === 'MemberExpression' && !e.left.computed
    && e.left.property.type === 'Identifier' && e.left.property.name === 'current';
  if (body.type !== 'BlockStatement') return isRefAssign(body);
  const stmts = body.body;
  if (!stmts.length) return false;
  return stmts.every((st) => st.type === 'ExpressionStatement' && isRefAssign(st.expression));
}

const CORE_HOOKS = new Set([
  'useState', 'useEffect', 'useLayoutEffect', 'useRef', 'useReducer',
  'useContext', 'useMemo', 'useCallback', 'useImperativeHandle', 'useId',
  'useSyncExternalStore', 'useDebugValue', 'useSelector', 'useDispatch',
  'useStore', 'useTransition', 'useDeferredValue', 'useActionState',
  'useFormStatus', 'useOptimistic',
]);

// Store-level redux that Redux Toolkit replaces.  `createSlice` does the work of
// `combineReducers` plus the action-type constants, and `configureStore` the work
// of `createStore` plus `applyMiddleware`, so these are the sites step 2 removes.
// `compose` is deliberately absent: it is a redux export, but it is also the
// name of a generic helper in a dozen libraries, and this is a max-gate, so a
// false positive costs the agent marks for code that is not redux at all.
const REDUX_LEGACY = new Set([
  'createStore', 'combineReducers', 'applyMiddleware', 'bindActionCreators',
]);

// The three toolkit factories that build a store or a reducer, which is what
// this gate's minimum is evidence of.  The set is exactly these because the
// question is whether the store was configured by the toolkit rather than
// assembled by hand, and nothing else the toolkit exports answers it:
// `createApi` defines the data layer and is counted as that, one gate over;
// `createSelector` is reselect's, re-exported, and reads state rather than
// building it; `createAction`, `createAsyncThunk` and `createEntityAdapter` are
// real toolkit factories but none of them produces a store or a reducer, and on
// a gate this low a bar they are the cheapest thing to write twice.
const RTK_API = new Set(['configureStore', 'createSlice', 'createReducer']);

// Where the toolkit's own factories come from.  `@reduxjs/toolkit`,
// `@reduxjs/toolkit/query` and `@reduxjs/toolkit/query/react` are one package
// under three entry points, so the prefix is the test.
const TOOLKIT_MODULE = /^@reduxjs\/toolkit(\/|$)/;

// The packages a module could reach the network through instead of the
// browser's own `fetch`.  The property is that no module makes its own request;
// `fetch` is one spelling of that among several, and a tree that swapped it for
// `axios` or an `XMLHttpRequest` would be fetching by hand in a new alphabet.
// Matched through the file's import bindings rather than by local name, so a
// renamed import is still the same client.
const HTTP_CLIENT_MODULES = new Set([
  'axios', 'superagent', 'ky', 'got', 'node-fetch', 'isomorphic-fetch',
  'whatwg-fetch',
]);

// What a timer callback has to reach for the timer to be a poll rather than an
// animation: the network, or the store.  Reaching the network is every spelling
// of a request this tree could write - `isOwnRequest` below owns that half.
// Reaching the store is any `dispatch` at all, because after this step the
// thing a timer would dispatch to fetch is the data layer's own request action
// and it is a dispatch like any other, plus the two ways a query already on
// screen is re-run.  Slide rotation, which this application also does on a
// timer, touches none of them - which is the whole point of resolving the
// callback instead of counting `setInterval`.
const FETCHY_CALLS = new Set(['dispatch', 'refetch', 'initiate']);

// The names RTK Query generates its hooks under.  Every endpoint yields a
// family - the query hook, its subscription-only and state-only variants, and
// for mutations the trigger - so the shape has to admit all of them or a screen
// that reads its data through one of the variants is read as reading it through
// nothing.  The name alone is never enough: which module the callee came from
// is what makes it this repository's data layer, and that is resolved at the
// call site.
const RTK_QUERY_HOOK = /^use[A-Z]\w*(Query|QuerySubscription|QueryState|Mutation)$/;

// Router v6/v7 data-mode surface.  v7 is v6's API under a new major, so this one
// query serves step 3's "the modern API is in use" and step 8 scores the version
// from the manifest instead.  `json` is deliberately absent - too generic a name
// to attribute to the router.
const ROUTER_MODERN = new Set([
  'useNavigate', 'useParams', 'useLocation', 'useSearchParams', 'useLoaderData',
  'useRouteError', 'useNavigation', 'createBrowserRouter', 'createHashRouter',
  'createMemoryRouter', 'createRoutesFromElements', 'Outlet', 'RouterProvider',
  'Routes', 'NavLink', 'Navigate', 'redirect',
]);

// The v6 pieces that stand a router up, as opposed to the ones that navigate
// within one.  `createRoutesFromElements` is here because a tree that keeps its
// JSX route table and hands it to a data router has stood one up.
const ROUTER_DATA_API = new Set([
  'createHashRouter', 'createBrowserRouter', 'createMemoryRouter',
  'createRoutesFromElements', 'RouterProvider',
]);

// The half of the v6 data mode that fetches for a route rather than navigates
// within one.  These are the hooks a component reads a route's own data
// through; the loader that produced it is matched separately, on the route
// object, because `loader` on its own is a word and not an API.
const ROUTER_LOADER_HOOKS = new Set([
  'useLoaderData', 'useRouteLoaderData', 'useActionData', 'useAsyncValue',
  'useAsyncError', 'useFetcher', 'useSubmit', 'defer',
]);

// The keys that make an object literal a v6 route rather than any other object.
// A `loader` key is only a route loader when it sits on one of these.
const ROUTE_OBJECT_KEYS = new Set([
  'path', 'index', 'element', 'Component', 'children', 'lazy', 'errorElement',
  'ErrorBoundary', 'handle', 'caseSensitive',
]);

// The keys on a route object that fetch for it.
const ROUTE_FETCH_KEYS = new Set(['loader', 'action', 'shouldRevalidate']);

const ROUTER_MODULE = /^react-router(-dom)?(\/|$)/;
// The shim package by any spelling: the bare name, or anything reached
// through it.  `react-router-dom` is a re-export of `react-router` from v7
// on, so a tree that still routes through it is a tree with two copies of
// the router installed however the specifier is written.
const RRD_MODULE = /^react-router-dom(\/|$)/;
const REACT_MODULE = /^react(\/|$)/;

// The two hooks step 5 adopts.  Named here rather than inline because the scan
// asks about them twice: once by name in the call switch, and once through the
// import for a file that renamed them.
const CONCURRENT_HOOKS = new Set(['useTransition', 'useDeferredValue']);
// The three calls this step takes back out.  Named here for the same reason as
// the set above and read the same two ways: the switch counts them by name, and
// the import is consulted separately for a file that brought one of them in
// under a name of its own.
const MANUAL_MEMO = new Set(['useMemo', 'useCallback', 'memo']);
// And the six spellings the switch below has a case for, which is what the
// resolution has to stand out of the way of rather than the three names: the
// guard used to be the bare name, and a bare name excluded `R.memo` from both
// readings at once - the switch has no case for it and the resolution declined
// to look at it - so a namespace import of React was the one spelling of
// `memo` that was free.
const MANUAL_MEMO_NAMED = new Set([
  'useMemo', 'React.useMemo', 'useCallback', 'React.useCallback', 'memo', 'React.memo',
]);
// The directives the React Compiler reads out of a module's own source, taken
// from the plugin this tree installs: two that opt a function in, two that opt
// it out, and one parameterised form that defers the decision to a flag.  The
// gated form is matched on its opening rather than on the whole of it, because
// what is between its parentheses is the candidate's own expression and the
// site is a site whatever it says.
const COMPILER_DIRECTIVES = new Set([
  'use forget', 'use memo', 'use no forget', 'use no memo',
]);
const COMPILER_DIRECTIVE_GATED = /^use memo if\(/;
const isCompilerDirective = (v) =>
  typeof v === 'string' && (COMPILER_DIRECTIVES.has(v) || COMPILER_DIRECTIVE_GATED.test(v));
// The legacy DOM entry point, exactly: `react-dom/client` is where the root API
// lives and matching it here would count the new call as the old one.
const REACT_DOM_LEGACY = /^react-dom$/;
const REACT_DOM_CLIENT = /^react-dom\/client$/;
// The chart library, by the specifier its modules are imported under.  Both the
// core (`echarts/lib/echarts`) and a registration (`echarts/lib/chart/line`)
// are an import of it, and so is the package whole (`echarts`); only a module
// of this repository whose *name* begins with it is not, which is why the
// pattern is anchored.
const ECHARTS = /^echarts(\/|$)/;
const LEGACY_RENDER_EXPORTS = new Set(['render', 'hydrate', 'unstable_renderSubtreeIntoContainer']);
const ROOT_EXPORTS = new Set(['createRoot', 'hydrateRoot']);
// react-icons 2's per-icon module path.  Version 5 is imported from a *pack*
// subpath - `react-icons/fa` - so matching every subpath of the package would
// count the migrated form as the retired one; `lib/` is the shape that only the
// retired major has, and the one that reaches react-icon-base.
const ICONS_V2 = /^react-icons\/lib\//;

const findings = Object.fromEntries(GATE_IDS.map((id) => [id, []]));
const parseErrors = [];
let fileCount = 0;

const hit = (id, file, node, what) => {
  if (!findings[id]) throw new Error(`ast-scan: undeclared gate id ${id}`);
  findings[id].push({
    file,
    line: node && node.loc ? node.loc.start.line : 0,
    what,
  });
};

function sources(dir, acc = [], keep = SOURCE) {
  for (const e of fs.readdirSync(dir, { withFileTypes: true })) {
    const full = path.join(dir, e.name);
    if (e.isDirectory()) {
      if (SKIP.has(e.name)) continue;
      if (SKIP_PATHS.has(path.relative(ROOT, full))) continue;
      sources(full, acc, keep);
    } else if (e.isFile() && keep.test(e.name)) {
      // Regular files only, as eslint-run.mjs and provenance.mjs walk: this
      // runs as root, and a symlink or a FIFO named `x.js` would otherwise be
      // read through (or block the read). The file a link points at inside the
      // tree is scanned under its own name.
      acc.push(full);
    }
  }
  return acc;
}

// A hand-written walk rather than @babel/traverse: traverse pulls in the whole
// of babel-core, and every query here is a plain shape test on a node.
function walk(node, visit, parent = null) {
  if (!node || typeof node.type !== 'string') return;
  visit(node, parent);
  for (const key of Object.keys(node)) {
    if (key === 'loc' || key === 'leadingComments' || key === 'trailingComments') continue;
    const v = node[key];
    if (Array.isArray(v)) {
      for (const c of v) if (c && typeof c.type === 'string') walk(c, visit, node);
    } else if (v && typeof v.type === 'string') {
      walk(v, visit, node);
    }
  }
}

// The same walk with a stop list: nodes in `skip` are neither visited nor
// descended into.  Used by the deferral pass, which has to look at a whole
// effect body but not inside the callbacks that body hands to other things.
function walkExcept(node, visit, skip, parent = null) {
  if (!node || typeof node.type !== 'string' || skip.has(node)) return;
  visit(node, parent);
  for (const key of Object.keys(node)) {
    if (key === 'loc' || key === 'leadingComments' || key === 'trailingComments') continue;
    const v = node[key];
    if (Array.isArray(v)) {
      for (const c of v) if (c && typeof c.type === 'string') walkExcept(c, visit, skip, node);
    } else if (v && typeof v.type === 'string') {
      walkExcept(v, visit, skip, node);
    }
  }
}

// ------------------------------------------------------------------- helpers

const isName = (n, name) => n && n.type === 'Identifier' && n.name === name;

// `React.useState` and `useState` are the same call for every question asked
// here, so the qualifier is dropped before a family lookup.
const bare = (n) => (n || '').split('.').pop();

// `React.Component`, `React.PureComponent`, or the bare names when imported.
const REACT_BASE = new Set(['Component', 'PureComponent']);
function isReactComponentBase(n) {
  if (!n) return false;
  if (n.type === 'Identifier') return REACT_BASE.has(n.name);
  if (n.type === 'MemberExpression' && !n.computed) {
    return n.property.type === 'Identifier' && REACT_BASE.has(n.property.name);
  }
  return false;
}

// The callee of a call, as a dotted string when it is a plain member chain.
function calleeName(n) {
  if (!n || n.type !== 'CallExpression') return null;
  const c = n.callee;
  if (c.type === 'Identifier') return c.name;
  if (c.type === 'MemberExpression' && !c.computed && c.property.type === 'Identifier') {
    const obj = c.object.type === 'Identifier' ? c.object.name : '?';
    return `${obj}.${c.property.name}`;
  }
  return null;
}

// The property name of a member/class/object property, for the many gates that
// are "this name is defined or assigned somewhere".
function keyName(n) {
  const k = n.key || n.property;
  if (!k) return null;
  if (k.type === 'Identifier') return k.name;
  if (k.type === 'StringLiteral') return k.value;
  return null;
}

const UNSAFE_LIFECYCLES = new Set([
  'componentWillMount', 'componentWillReceiveProps', 'componentWillUpdate',
  'UNSAFE_componentWillMount', 'UNSAFE_componentWillReceiveProps',
  'UNSAFE_componentWillUpdate',
]);

const LEGACY_CONTEXT = new Set(['contextTypes', 'childContextTypes', 'getChildContext']);

// react-router v3/v5 exports with no v6+ equivalent under the same name.  A v7
// tree importing any of them is importing from a shim or a stale package.
const RETIRED_ROUTER = new Set([
  'hashHistory', 'browserHistory', 'createMemoryHistory', 'IndexRoute',
  'IndexRedirect', 'IndexLink', 'Router', 'RouterContext', 'applyRouterMiddleware',
  'withRouter', 'Switch', 'Redirect', 'useHistory', 'useRouteMatch', 'match',
]);

// Retired packages, each mapped to its own gate id as well as to the union.
// A single `retired-packages` count cannot be allocated to a step.  React 16
// deleted `React.PropTypes`, so step 1's documented migration path *introduces*
// the standalone `prop-types` package, and step 6 is where React 19 stops
// reading `propTypes` at all and it goes away again; meanwhile step 2 is what
// removes Immutable and the thunk middleware.  The union therefore rises at
// step 1 and falls twice, and no step could gate it monotonically.  The union
// stays in the output as a record of the whole set; the numbers that score are
// the per-family ones.
const RETIRED_PACKAGES = new Map([
  ['immutable', 'pkg-immutable'],
  ['redux-immutable', 'pkg-immutable'],
  ['redux-thunk', 'pkg-redux-thunk'],
  ['prop-types', 'pkg-prop-types'],
  ['react-addons-css-transition-group', 'pkg-transition-group'],
  ['recompose', 'pkg-recompose'],
  ['enzyme', 'pkg-legacy-test-tools'],
  ['react-dom/test-utils', 'pkg-legacy-test-tools'],
]);

// Records one site against both the union and the package's own gate, so the
// two can never disagree about a site.
const retiredHit = (rel, node, from) => {
  hit('retired-packages', rel, node, from);
  hit(RETIRED_PACKAGES.get(from), rel, node, from);
};

// Comments that can switch a *hook* rule off, and nothing else.
//
// This is belt-and-braces, not the instrument.  The instrument is one line in
// the scorer's own eslint.config.mjs - `linterOptions.noInlineConfig` - which
// makes ESLint ignore every inline directive, so a comment cannot silence the
// scored rule and the finding appears on its own.  Counting comment shapes
// cannot win that race on its own: the shape that motivated this change,
// `/* eslint react-hooks/rules-of-hooks: off */`, is not a disable directive at
// all and is not matched below.  The count stays because a site that carries a
// disable directive is still a site somebody tried to silence, and reporting it
// costs nothing.
//
// A suppression is counted as a finding because lint findings cannot tell a
// clean site from a silenced one: without this, commenting the rule out scores
// the same as satisfying it.  But only a comment that can silence the rule the
// gate is about is the gate's business.  An `eslint-disable-next-line
// no-console` over a debug statement suppresses nothing any requirement here
// mentions, and billing it charged a candidate a gate for tidying up unrelated
// code - which is a requirement the instruction does not state and could not
// state without naming the tool.
//
// So exactly two forms qualify: a disable directive naming a `react-hooks/...`
// rule, and a bare disable directive naming no rule at all, which turns off
// everything and hook rules with it.  `@ts-ignore`, `@ts-expect-error` and
// `@ts-nocheck` are deliberately absent - they suppress the TypeScript
// compiler, which is not what produces these findings.  So are the React
// Compiler's own opt-outs (`react-compiler-disable`, the `use no memo`
// directive): the rules those turn off are not the rule counted against here.
// A later step that scores the compiler needs its own query, not a wider one.
const DISABLE = /eslint-disable(-next-line|-line)?\b([\s\S]*)$/;

// The rule list of a disable directive is everything after it up to eslint's own
// ` -- ` description separator.  An empty list means every rule.
function hookSuppression(text) {
  const m = DISABLE.exec(text);
  if (!m) return null;
  const list = m[2].split(/\s--\s/)[0].trim();
  if (!list) return 'eslint-disable (every rule)';
  const hooks = list.split(',').map((r) => r.trim()).filter((r) => /^react-hooks\//.test(r));
  return hooks.length ? `eslint-disable ${hooks.join(' ')}` : null;
}

// ------------------------------------------------------------------- queries

const isPropTypesPkg = (s) => s === 'prop-types' || String(s).startsWith('prop-types/');

// Does this file get its validators from the standalone package?  Surviving
// prop validation is a conjunction - a `propTypes` declaration *and* the package
// React 16 moved the validators to - and each half on its own is satisfied by
// something that is not the property.  Fourteen unused imports are not
// validation; forty declarations off `React.PropTypes` are not the standalone
// package, and are exactly what the untouched tree already has.  So the two are
// resolved per file and only their conjunction is counted as a site.
//
// A separate pass rather than a flag set during the main walk: an assignment can
// precede the `require` that makes it legal, and a walk that learned about the
// import on its way past would score the same file differently depending on the
// order its statements happen to be in.
function importsPropTypes(ast) {
  let found = false;
  walk(ast.program, (n) => {
    if (found) return;
    if (n.type === 'ImportDeclaration' && n.source && isPropTypesPkg(n.source.value)) {
      found = true;
    } else if (n.type === 'CallExpression' && calleeName(n) === 'require'
               && n.arguments[0] && n.arguments[0].type === 'StringLiteral'
               && isPropTypesPkg(n.arguments[0].value)) {
      found = true;
    }
  });
  return found;
}

// Every function this file binds to a name, so that a timer given a bare
// identifier can be asked what it will do when it fires.
//
// `setInterval(fetchData, timerTime)` and `setInterval(() => dispatch(...), n)`
// are the same poll written two ways, and this application writes it the first
// way in all four places.  A gate that counted `setInterval` alone would score
// them the same as the carousel's slide rotation, and a candidate could satisfy
// a ceiling on the count by converting the rotation and leaving the polling -
// paid for work the step did not ask for while skipping the work it did.
//
// One level of resolution, not a call graph: the binding has to be in the same
// file and hold a function directly.  `useCallback(fn, deps)` and
// `useMemo(() => fn, deps)` are unwrapped because that is how a component keeps
// a timer callback stable, and this tree's four sites are all the first of
// those.  Anything that hides the work behind an import is not resolved and so
// is not counted - a max-gate that guessed would be charging for code it did
// not read.
function localFunctions(ast) {
  const table = new Map();
  const fn = (n) => {
    if (!n) return null;
    if (n.type === 'FunctionExpression' || n.type === 'ArrowFunctionExpression') return n;
    if (n.type === 'CallExpression') {
      const b = bare(calleeName(n));
      if (b === 'useCallback' || b === 'useMemo') {
        const inner = fn(n.arguments[0]);
        // `useMemo(() => fn, [])` hands back the function its body returns.
        if (inner && b === 'useMemo' && inner.body && inner.body.type !== 'BlockStatement') {
          return fn(inner.body) || inner;
        }
        return inner;
      }
    }
    return null;
  };
  walk(ast.program, (n) => {
    if (n.type === 'FunctionDeclaration' && n.id) table.set(n.id.name, n);
    else if (n.type === 'VariableDeclarator' && n.id && n.id.type === 'Identifier') {
      const f = fn(n.init);
      if (f) table.set(n.id.name, f);
    }
  });
  return table;
}

// Every object literal this file binds to a name, so that a `dispatch` handed a
// bare identifier can be asked what it will dispatch.  `const a = {type: 'x'};
// dispatch(a)` is the same hand-built action as dispatching the literal, and a
// query that read only the literal scored the two differently for a difference
// that is not the property.  One binding level and the same file, for the same
// reason localFunctions is one: a ceiling that chased a value across files would
// be charging for code it did not read.
function localObjects(ast) {
  const table = new Map();
  walk(ast.program, (n) => {
    if (n.type === 'VariableDeclarator' && n.id && n.id.type === 'Identifier'
        && n.init && n.init.type === 'ObjectExpression') {
      table.set(n.id.name, n.init);
    }
  });
  return table;
}

// Every call this file binds to a name, so that a value lifted out of the
// literal it used to sit in can still be asked what produced it.  `const
// baseQuery = fetchBaseQuery({...}); createApi({baseQuery, ...})` builds the
// data layer over the same call as the inline spelling does, and a query that
// read only the inline one was asking where the expression sits rather than what
// it is.  One binding level and the same file, for the same reason localObjects
// is one.
function localCalls(ast) {
  const table = new Map();
  walk(ast.program, (n) => {
    if (n.type === 'VariableDeclarator' && n.id && n.id.type === 'Identifier'
        && n.init && n.init.type === 'CallExpression') {
      table.set(n.id.name, n.init);
    }
  });
  return table;
}

// The browser's own request primitive, however this file names the global it
// hangs off.  `fetch`, `window.fetch` and `globalThis.fetch` are one function
// under three spellings, and a computed read of it is a fourth.
const GLOBAL_SCOPES = new Set(['window', 'globalThis', 'self']);
function isFetchRef(e) {
  if (!e) return false;
  if (e.type === 'Identifier') return e.name === 'fetch';
  if (e.type !== 'MemberExpression') return false;
  if (!(e.object.type === 'Identifier' && GLOBAL_SCOPES.has(e.object.name))) return false;
  return e.computed
    ? (e.property.type === 'StringLiteral' && e.property.value === 'fetch')
    : isName(e.property, 'fetch');
}

// The console methods a module can write over to make a console count say
// something other than what the application reported.  `assert` is on the list
// beside `error` and `warn` because a failed assertion is a console error too,
// so leaving it off would leave a third of the API as the way round the other
// two.
const CONSOLE_METHODS = new Set(['error', 'warn', 'assert']);

// The console object, however this file spells the global it hangs off - the
// bare name, or a read off `window` / `globalThis` / `self`, computed or not.
// The same four spellings isFetchRef reads for the request primitive, for the
// same reason: which identifier a module reaches a global under is not the
// property being asked about.
function isConsoleRef(e) {
  if (!e) return false;
  if (e.type === 'Identifier') return e.name === 'console';
  if (e.type !== 'MemberExpression') return false;
  if (!(e.object.type === 'Identifier' && GLOBAL_SCOPES.has(e.object.name))) return false;
  return e.computed
    ? (e.property.type === 'StringLiteral' && e.property.value === 'console')
    : isName(e.property, 'console');
}

// Which console method a member expression names, or null for anything else.
// `console.log` is not on the list: a module that logs is a module that logs,
// and only the three the development run counts are the ones a replacement
// would be silencing.
function consoleMethodName(e) {
  if (!e || e.type !== 'MemberExpression' || !isConsoleRef(e.object)) return null;
  const name = e.computed
    ? (e.property.type === 'StringLiteral' ? e.property.value : null)
    : (e.property.type === 'Identifier' ? e.property.name : null);
  return name && CONSOLE_METHODS.has(name) ? name : null;
}

// Every name this file binds to that primitive, one step.  `const http = fetch;
// http(url)` is the same request as `fetch(url)`: nothing about what the module
// does has changed, only what the call is spelled.  A ceiling that read the
// spelling could be cleared by a rename, which would make it evidence about an
// identifier rather than about the request, so the binding is resolved the same
// one level localFunctions and localObjects already resolve - same file, same
// declaration, no chasing a value across modules.
function localRequestNames(ast) {
  const names = new Set();
  walk(ast.program, (n) => {
    if (n.type !== 'VariableDeclarator' || !n.id || !n.init) return;
    if (n.id.type === 'Identifier' && isFetchRef(n.init)) {
      names.add(n.id.name);
    } else if (n.id.type === 'ObjectPattern' && n.init.type === 'Identifier'
               && GLOBAL_SCOPES.has(n.init.name)) {
      // `const {fetch: f} = window` takes the same function off the same object.
      for (const pr of n.id.properties) {
        if (pr.type === 'ObjectProperty' && keyName(pr) === 'fetch'
            && pr.value && pr.value.type === 'Identifier') names.add(pr.value.name);
      }
    }
  });
  return names;
}

// Every name this file binds to the store's own dispatch, one step.  The gate's
// property is that an object carrying a `type` is handed to something that
// dispatches it, and `const d = useDispatch()` or `const d = store.dispatch` is
// that something under a name the file chose.  Both sources are anchored to what
// produced the value - a `useDispatch` call, or a member actually called
// `dispatch` - so an arbitrary function does not become a dispatcher by being
// bound to a short name.
function dispatcherNames(ast) {
  const names = new Set();
  walk(ast.program, (n) => {
    if (n.type !== 'VariableDeclarator' || !n.id || n.id.type !== 'Identifier' || !n.init) return;
    const init = n.init;
    if (init.type === 'CallExpression' && bare(calleeName(init)) === 'useDispatch') {
      names.add(n.id.name);
    } else if (init.type === 'MemberExpression' && !init.computed && isName(init.property, 'dispatch')) {
      names.add(n.id.name);
    }
  });
  return names;
}

// Every call in this file whose callee is a plain parameter of a function that
// encloses it.  The 2017 tree hands its dispatcher in as an argument, so at the
// call site the dispatcher is whatever that parameter is called; `walk` carries
// a parent but not a scope, and the question is about scope, so this is the one
// query that needs its own descent.  Only a parameter written as a bare name
// counts: a destructured one is a property of an object the caller passed, and a
// component calling `onSelect({type})` out of its own props is not dispatching.
function paramCallSites(ast, returnedFns) {
  const out = new Set();
  const scopes = [];
  const isFn = (n) => n.type === 'FunctionDeclaration' || n.type === 'FunctionExpression'
    || n.type === 'ArrowFunctionExpression' || n.type === 'ObjectMethod' || n.type === 'ClassMethod';
  const visit = (n) => {
    if (!n || typeof n.type !== 'string') return;
    const fn = isFn(n);
    if (fn) {
      const names = new Set();
      // Only a function another function hands back contributes its parameters.
      // That is the shape the middleware calls with the store's own dispatch,
      // and it is the same evidence `thunk-creator` reads; without it any
      // callback parameter called with an object that has a `type` would be a
      // dispatch, and `emit(cb)` or a chart helper handed `{type: 'bar'}` would
      // be an action the tree assembled - a ceiling firing on a construct the
      // step never forbade.
      if (returnedFns.has(n)) {
        for (const p of n.params || []) if (p.type === 'Identifier') names.add(p.name);
      }
      scopes.push(names);
    }
    if (n.type === 'CallExpression' && n.callee.type === 'Identifier'
        && scopes.some((s) => s.has(n.callee.name))) {
      out.add(n);
    }
    for (const key of Object.keys(n)) {
      if (key === 'loc' || key === 'leadingComments' || key === 'trailingComments') continue;
      const v = n[key];
      if (Array.isArray(v)) {
        for (const c of v) if (c && typeof c.type === 'string') visit(c);
      } else if (v && typeof v.type === 'string') {
        visit(v);
      }
    }
    if (fn) scopes.pop();
  };
  visit(ast.program);
  return out;
}

// The functions this file hands back out of another function, and whether a
// function is only a link in a curried chain.  A thunk is what an action creator
// returns, so being returned is half of the shape; the other half is that the
// returned function does the work rather than passing the baton on again, which
// is what keeps `store => next => action => next(action)` - ordinary middleware,
// which this step does not forbid - from reading as one.
function returnedFunctions(ast) {
  const out = new Set();
  const fnNode = (e) => (e && (e.type === 'FunctionExpression' || e.type === 'ArrowFunctionExpression')
    ? e : null);
  walk(ast.program, (n) => {
    if (n.type === 'ReturnStatement') {
      const f = fnNode(n.argument);
      if (f) out.add(f);
    } else if (n.type === 'ArrowFunctionExpression') {
      const f = fnNode(n.body);
      if (f) out.add(f);
    }
  });
  return out;
}
function isCurryLink(n) {
  if (!n.body) return false;
  const isFn = (e) => !!e && (e.type === 'FunctionExpression' || e.type === 'ArrowFunctionExpression');
  if (isFn(n.body)) return true;
  if (n.body.type !== 'BlockStatement') return false;
  // Any return of a function, not only a body that is nothing but that return:
  // `next => { const t = Date.now(); return action => next(action); }` is the
  // same middleware link as `next => action => next(action)` with a line in
  // front of it, and a test that read only the shorter spelling would call
  // ordinary middleware - which this step never forbade - an action creator.
  return n.body.body.some((st) => st.type === 'ReturnStatement' && isFn(st.argument));
}
// Is this function's own first parameter called inside it?  That is what makes
// the parameter the dispatcher rather than a value: the middleware hands the
// function `dispatch` and the function calls it.
function invokesFirstParam(n) {
  const first = n.params && n.params[0];
  if (!first || first.type !== 'Identifier' || !n.body) return false;
  let found = false;
  walk(n.body, (c) => {
    if (!found && c.type === 'CallExpression' && isName(c.callee, first.name)) found = true;
  });
  return found;
}

// Every name this file binds by destructuring a `type` property.  A reducer that
// pulls the discriminant out of the action before switching on it is switching
// on the action's type; `const {type} = action` and `const {type: t} = action`
// are that name under another spelling, and so is a reducer that takes the
// action apart in its parameter list.  A query that saw only `action.type` read
// all three as no reducer at all.
function typeBindings(ast) {
  const names = new Set();
  const fromPattern = (pat) => {
    if (!pat || pat.type !== 'ObjectPattern') return;
    for (const pr of pat.properties) {
      if (pr.type !== 'ObjectProperty' || keyName(pr) !== 'type') continue;
      if (pr.value && pr.value.type === 'Identifier') names.add(pr.value.name);
    }
  };
  walk(ast.program, (n) => {
    if (n.type === 'VariableDeclarator') fromPattern(n.id);
    else if (n.params) for (const pr of n.params) fromPattern(pr);
  });
  return names;
}

// Every ES import this file binds, as local name -> the module it came from and
// the name it was exported under ('default' for a default import, '*' for a
// namespace one).  Three of the queries below are about provenance rather than
// spelling - did this factory come from the toolkit, did this hook come from the
// module that defines this repository's API - and a name on its own cannot
// answer either.  The exported name rather than the local one, because a renamed
// import is still the same construct.
//
// `require` is deliberately not resolved.  Nothing in this tree binds a module
// that way, and a second and weaker binding table for CommonJS would answer the
// same question differently depending on which syntax a file happened to use.
function importBindings(ast) {
  const table = new Map();
  walk(ast.program, (n) => {
    if (n.type !== 'ImportDeclaration' || !n.source) return;
    const source = n.source.value;
    for (const sp of n.specifiers || []) {
      if (!sp.local || sp.local.type !== 'Identifier') continue;
      const imported = sp.type === 'ImportDefaultSpecifier' ? 'default'
        : sp.type === 'ImportNamespaceSpecifier' ? '*'
          : (sp.imported && (sp.imported.name || sp.imported.value)) || sp.local.name;
      table.set(sp.local.name, { source, imported });
    }
  });
  return table;
}

// A relative specifier resolved to a file of this repository, the way the
// bundler resolves it: as written, with each of the extensions this tree uses,
// or as a directory with an index.  Bare specifiers do not resolve - a package
// is not a module of this repository - and neither does anything the scan did
// not parse, so the answer is always a file this scan has read.
const RESOLVE_EXT = ['', '.js', '.jsx', '.mjs', '.cjs', '.ts', '.tsx',
  '/index.js', '/index.jsx', '/index.mjs', '/index.ts', '/index.tsx'];
function resolveModule(fromRel, spec, known) {
  if (typeof spec !== 'string' || !spec.startsWith('.')) return null;
  const base = path.join(path.dirname(fromRel), spec);
  for (const ext of RESOLVE_EXT) {
    if (known.has(base + ext)) return base + ext;
  }
  return null;
}

// The name this callee resolves to in the toolkit's own exports, or null.  An
// identifier bound by an import from the package, or a member read off a
// namespace or default import of it; a local name that merely matches a factory
// is not one of these, which is what stops a module that declares its own
// `createApi` from counting as a data layer.
// Which name a callee was imported under, from a module matching `re`.  The
// same two shapes `toolkitExport` resolves - a named import called directly, or
// a member read off a namespace/default import - asked of any package, because
// `redirect` and `lazy` are ordinary enough words that a call to one is only a
// site of anything if it came from the library that defines it.  Step 2's blind
// candidate wrote `const lazy = load => (location, callback) => {...}`, which a
// name-only scan would have read as React's.
function moduleExport(callee, imports, re) {
  if (!callee) return null;
  if (callee.type === 'Identifier') {
    const b = imports.get(callee.name);
    if (b && re.test(b.source) && b.imported !== 'default' && b.imported !== '*') {
      return b.imported;
    }
    return null;
  }
  if (callee.type === 'MemberExpression' && !callee.computed
      && callee.object.type === 'Identifier' && callee.property.type === 'Identifier') {
    const b = imports.get(callee.object.name);
    if (b && re.test(b.source) && (b.imported === '*' || b.imported === 'default')) {
      return callee.property.name;
    }
  }
  return null;
}

// ------------------------------------------------- React's lazy, through a name

// Whether a function is one whose body calls React's `lazy`.
const FN_EXPRS = new Set(['ArrowFunctionExpression', 'FunctionExpression']);
function callsReactLazy(fn, imports) {
  let found = false;
  walk(fn, (n) => {
    if (n.type === 'CallExpression'
        && moduleExport(n.callee, imports, REACT_MODULE) === 'lazy') found = true;
  });
  return found;
}

// The local names in this file that mean React's `lazy` because a function
// bound to one of them calls it.  A one-line helper around `lazy` is what a
// tree that splits forty routes and a dozen panels writes rather than repeating
// the call and the boundary at each site - the blind lineage of this task wrote
// exactly that at step 3 - and a scan that counts only the direct call reads a
// tree that split everything through its helper as a tree that split nothing.
//
// The helper has to *call* React's `lazy`, resolved through the import the same
// way the direct call is; the name is never consulted. That is the whole test,
// and it is what separates this from the name-only scan the direct call was
// written to avoid: step 2's blind candidate wrote `const lazy = load =>
// (location, callback) => {...}`, which calls nothing of React's and is not one
// of these. A binding whose initializer is a *call* - `const Chart =
// lazy(() => import(...))` - is not one either: that is a component this
// scan already counted at the call, not a second name for the factory.  (The
// specifier is elided on purpose: assemble-step.sh reads this package's own
// relative imports back out with a grep that cannot tell code from comment,
// and a written-out one here would look to it like a module of this directory
// that does not exist.)
function lazyWrapperNames(ast, imports) {
  const names = new Set();
  walk(ast.program, (n) => {
    if (n.type === 'VariableDeclarator' && n.id && n.id.type === 'Identifier'
        && n.init && FN_EXPRS.has(n.init.type) && callsReactLazy(n.init, imports)) {
      names.add(n.id.name);
    }
    if (n.type === 'FunctionDeclaration' && n.id && callsReactLazy(n, imports)) {
      names.add(n.id.name);
    }
  });
  return names;
}

// And the names a *different* module can import one of those helpers under.
// One hop, from the module that declares the wrapper to the modules that import
// it - the same depth the data layer's wrappers are followed, and for the same
// reason: a helper factored into its own file is the same helper.
function exportedLazyWrappers(ast, local, imports) {
  const out = new Set();
  for (const n of ast.program.body) {
    if (n.type === 'ExportNamedDeclaration') {
      const d = n.declaration;
      if (d && d.type === 'FunctionDeclaration' && d.id && local.has(d.id.name)) {
        out.add(d.id.name);
      }
      if (d && d.type === 'VariableDeclaration') {
        for (const v of d.declarations) {
          if (v.id && v.id.type === 'Identifier' && local.has(v.id.name)) out.add(v.id.name);
        }
      }
      if (!d) {
        for (const sp of n.specifiers || []) {
          const l = sp.local && sp.local.name;
          if (l && local.has(l)) {
            out.add((sp.exported && (sp.exported.name || sp.exported.value)) || l);
          }
        }
      }
    } else if (n.type === 'ExportDefaultDeclaration') {
      const d = n.declaration;
      if (d && d.type === 'Identifier' && local.has(d.name)) out.add('default');
      else if (d && (d.type === 'FunctionDeclaration' || FN_EXPRS.has(d.type))
               && callsReactLazy(d, imports)) out.add('default');
    }
  }
  return out;
}

// The names a module hands React's own `lazy` on under.  Not a wrapper - no
// function of this repository is involved - just React's export passing through
// a module of the tree on its way to the file that calls it:
//
//   // src/react.js
//   export { lazy } from 'react';        // and `export {lazy}` after importing it
//   // every panel
//   import { lazy } from '../../react';
//
// which is a legal factoring, calls React's `lazy` once per panel, and read as
// zero while the call was resolved through the importing file's own bindings
// alone.  One hop, the same depth the wrappers above are followed and the same
// reason: a re-export is not a second `lazy`.
function reExportedReactLazy(ast, imports) {
  const out = new Set();
  for (const n of ast.program.body) {
    if (n.type !== 'ExportNamedDeclaration' || n.declaration) continue;
    for (const sp of n.specifiers || []) {
      if (sp.type !== 'ExportSpecifier') continue;
      const local = sp.local && (sp.local.name || sp.local.value);
      const exported = (sp.exported && (sp.exported.name || sp.exported.value)) || local;
      if (!local) continue;
      if (n.source && typeof n.source.value === 'string') {
        // `export {lazy} from 'react'`: the local half names the export being
        // taken from that module, not a binding of this one.
        if (REACT_MODULE.test(n.source.value) && local === 'lazy') out.add(exported);
      } else {
        // `import {lazy} from 'react'` above, `export {lazy}` below.
        const b = imports.get(local);
        if (b && REACT_MODULE.test(b.source) && b.imported === 'lazy') out.add(exported);
      }
    }
  }
  return out;
}

// The router's own `<Route>`, under whatever local name this file gave it.  The
// bare tag counts on its own, because that is what the word means in a route
// table and what every tree of this task writes.  The other two spellings are
// resolved through the import, for the reason the loader hooks are: a ceiling a
// candidate steps out of by renaming an import is not a ceiling, and
// `import {Route as R}` or `<router.Route>` is one rename away from a route
// fetch this scan cannot see.
function isRouteElement(name, imports) {
  if (!name) return false;
  if (name.type === 'JSXIdentifier') {
    if (name.name === 'Route') return true;
    const b = imports.get(name.name);
    return !!(b && ROUTER_MODULE.test(b.source) && b.imported === 'Route');
  }
  if (name.type === 'JSXMemberExpression' && name.object.type === 'JSXIdentifier'
      && name.property.type === 'JSXIdentifier' && name.property.name === 'Route') {
    const b = imports.get(name.object.name);
    return !!(b && ROUTER_MODULE.test(b.source)
      && (b.imported === '*' || b.imported === 'default'));
  }
  return false;
}

// react-dom's own root factory, resolved through the module it came from rather
// than by the name it is called under.  `createRoot` is an ordinary identifier
// and `hydrateRoot` is nearly one; the property the step states is that the root
// comes from react-dom's *client* entry point, and a local function of either
// name satisfies a name-only floor without any of react-dom in it.  The two
// spellings a tree can reach the export by are the two `moduleExport` resolves -
// a named import called directly, under whatever local name it was given, and a
// member read off a namespace or default import of the same module - which is
// the shape `isRouteElement` uses for the router's own `<Route>`.  Returns the
// name the export has in the package, so a site can say which call it was under
// a local name that is not it.
function rootExport(n, imports) {
  if (!n || n.type !== 'CallExpression') return null;
  const r = moduleExport(n.callee, imports, REACT_DOM_CLIENT);
  return r && ROOT_EXPORTS.has(r) ? r : null;
}

// React's own `<StrictMode>`, under whatever local name this file gave it.  The
// bare tag counts on its own, and so does `<React.StrictMode>`, because those
// are what the element is called and what every tree of this task writes; a
// renamed import and a namespace import are resolved through the binding, for
// the reason `isRouteElement` resolves its own two - a floor a candidate misses
// by renaming an import is a floor that fails a tree which did the work.
function isStrictModeElement(name, imports) {
  if (!name) return false;
  if (name.type === 'JSXIdentifier') {
    if (name.name === 'StrictMode') return true;
    const b = imports.get(name.name);
    return !!(b && REACT_MODULE.test(b.source) && b.imported === 'StrictMode');
  }
  if (name.type === 'JSXMemberExpression' && name.object.type === 'JSXIdentifier'
      && name.property.type === 'JSXIdentifier' && name.property.name === 'StrictMode') {
    if (name.object.name === 'React') return true;
    const b = imports.get(name.object.name);
    return !!(b && REACT_MODULE.test(b.source)
      && (b.imported === '*' || b.imported === 'default'));
  }
  return false;
}

// React's own `<Suspense>`, on the same three spellings `isStrictModeElement`
// accepts.  Step 5 reads this as a floor rather than a ceiling, so the member
// spelling matters in the other direction: `<React.Suspense fallback={...}>` is
// what a good deal of React's own documentation writes, and a name-only scan
// read a tree that wrapped every lazy panel that way as having no boundary at
// all.
function isSuspenseElement(name, imports) {
  if (!name) return false;
  if (name.type === 'JSXIdentifier') {
    if (name.name === 'Suspense') return true;
    const b = imports.get(name.name);
    return !!(b && REACT_MODULE.test(b.source) && b.imported === 'Suspense');
  }
  if (name.type === 'JSXMemberExpression' && name.object.type === 'JSXIdentifier'
      && name.property.type === 'JSXIdentifier' && name.property.name === 'Suspense') {
    if (name.object.name === 'React') return true;
    const b = imports.get(name.object.name);
    return !!(b && REACT_MODULE.test(b.source)
      && (b.imported === '*' || b.imported === 'default'));
  }
  return false;
}

function toolkitExport(callee, imports) {
  if (!callee) return null;
  if (callee.type === 'Identifier') {
    const b = imports.get(callee.name);
    if (b && TOOLKIT_MODULE.test(b.source) && b.imported !== 'default' && b.imported !== '*') {
      return b.imported;
    }
    return null;
  }
  if (callee.type === 'MemberExpression' && !callee.computed
      && callee.object.type === 'Identifier' && callee.property.type === 'Identifier') {
    const b = imports.get(callee.object.name);
    if (b && TOOLKIT_MODULE.test(b.source) && (b.imported === '*' || b.imported === 'default')) {
      return callee.property.name;
    }
  }
  return null;
}

// Is this `createApi` call built over the toolkit's own base query?  The
// instruction's sentence is one definition, built with the query layer, over
// `fetchBaseQuery` with the application's base URL, and that is a conjunction:
// either half on its own is satisfied by something that is not a data layer.
// So the two are resolved together and only the conjunction is a site.
//
// Both halves are resolved one binding step through this same file, the way the
// timer callback and the hand-built action already are.  The options object may
// be bound to a name, it may be spread in from a shared one, and the base query
// may be lifted out of the literal or handed to the toolkit's own `retry`, and
// all of those are what a codebase writes the moment `prepareHeaders`, a retry
// policy or re-auth appears; a query that read only the doubly-inline spelling
// asked for an expression in a particular place rather than for a data layer.
// What it does not do is relax the provenance test.  The call the name resolves
// to still has to be the toolkit's own `fetchBaseQuery`, and a wrapper is
// unwrapped only when the wrapper is itself an export of the toolkit - so a
// module that declares a `fetchBaseQuery` of its own, or wraps a home-made base
// query in the toolkit's retry, is no more a data layer than one that inlined
// something that was never the toolkit's.
function hasToolkitBaseQuery(call, imports, objects, calls) {
  const a0 = call.arguments && call.arguments[0];
  const opts = a0 && a0.type === 'ObjectExpression' ? a0
    : (a0 && a0.type === 'Identifier' ? objects.get(a0.name) : null);
  if (!opts || opts.type !== 'ObjectExpression') return false;
  // `createApi({...shared, reducerPath})` puts the base query on the spread
  // object, one level, resolved through the same table the options object is.
  const properties = [];
  const collect = (o, depth) => {
    for (const pr of o.properties || []) {
      if (pr.type === 'SpreadElement' || pr.type === 'SpreadProperty') {
        if (depth) continue;
        const s = pr.argument;
        const inner = s && s.type === 'ObjectExpression' ? s
          : (s && s.type === 'Identifier' ? objects.get(s.name) : null);
        if (inner && inner.type === 'ObjectExpression') collect(inner, depth + 1);
      } else {
        properties.push(pr);
      }
    }
  };
  collect(opts, 0);
  const isToolkitBase = (v) => {
    const c = v && v.type === 'CallExpression' ? v
      : (v && v.type === 'Identifier' ? calls.get(v.name) : null);
    if (!c) return false;
    const tk = toolkitExport(c.callee, imports);
    if (tk === 'fetchBaseQuery') return true;
    return !!tk && (c.arguments || []).some(isToolkitBase);
  };
  return properties.some((pr) => keyName(pr) === 'baseQuery' && isToolkitBase(pr.value));
}

// Every name this file binds to the API object, and the name of the data layer's
// own surface each one stands for: the `createApi` call this file makes itself,
// the api it imported from the module that makes it, and the generated hooks
// taken off either.  The hooks really are properties of the `createApi` result,
// so `const {useThingQuery} = api` in a screen is the library's own surface and
// not a second idiom; before this the one file that could destructure that way
// was the one file that could not import the hooks from anywhere else, and every
// screen written that way read as reading its data through nothing.
//
// A local name and the name it stands for are kept apart, because a file may
// rename what it imports (`import {useThingQuery as useThing}`) or what it
// destructures, and RTK Query writes the endpoint into the generated name - so
// the exported name is the only place the hook says what it is, and testing the
// local one asked the file to keep the library's spelling.
//
// The object has to be one whose provenance this scan traced - this file's own
// `createApi` result, or an import from a module in the api set - and never an
// arbitrary object that happens to be called `api`.  That is the whole of what
// keeps a name from being the evidence.
function localApiNames(ast, imports, apiImports) {
  const roots = new Set(apiImports.keys());
  const names = new Map(apiImports);
  const isApiCall = (n) => n && n.type === 'CallExpression'
    && toolkitExport(n.callee, imports) === 'createApi';
  const read = (m) => (!m.computed && m.property.type === 'Identifier' ? m.property.name
    : (m.computed && m.property.type === 'StringLiteral' ? m.property.value : null));
  // Twice, so the answer does not depend on whether the destructuring happens to
  // be written above the `createApi` call it takes apart.
  for (let pass = 0; pass < 2; pass++) {
    walk(ast.program, (n) => {
      if (n.type !== 'VariableDeclarator' || !n.init || !n.id) return;
      // A property read off the api object binds that one hook under whatever
      // name the file gives it; the object itself, by any route, binds all of
      // them.
      if (n.init.type === 'MemberExpression' && n.init.object.type === 'Identifier'
          && roots.has(n.init.object.name)) {
        const p = read(n.init);
        if (p && n.id.type === 'Identifier') names.set(n.id.name, p);
        return;
      }
      if (!(isApiCall(n.init) || (n.init.type === 'Identifier' && roots.has(n.init.name)))) return;
      if (n.id.type === 'Identifier') {
        roots.add(n.id.name);
        names.set(n.id.name, n.id.name);
      } else if (n.id.type === 'ObjectPattern') {
        for (const pr of n.id.properties) {
          if (pr.type === 'ObjectProperty' && pr.value && pr.value.type === 'Identifier') {
            names.set(pr.value.name, keyName(pr) || pr.value.name);
          }
        }
      }
    });
  }
  for (const n of roots) if (!names.has(n)) names.set(n, n);
  return names;
}

// The generated hook this callee resolves to, or null.  Two routes, and both end
// at a binding this scan traced back to the data layer: a name the file bound to
// one hook - under the library's spelling or under its own - or a property read
// off the api object itself, dotted or by bracket.  A bracket read is the same
// property access written past the dotted form; refusing it would make the gate
// a question about punctuation.
function queryHookName(c, sources) {
  if (!c) return null;
  if (c.type === 'Identifier') {
    const nm = sources.names.get(c.name);
    return nm && RTK_QUERY_HOOK.test(nm) ? nm : null;
  }
  if (c.type === 'MemberExpression' && c.object.type === 'Identifier'
      && sources.names.has(c.object.name)) {
    const p = !c.computed && c.property.type === 'Identifier' ? c.property.name
      : (c.computed && c.property.type === 'StringLiteral' ? c.property.value : null);
    return p && RTK_QUERY_HOOK.test(p) ? p : null;
  }
  return null;
}

// Is this call one of the sites `rtk-query-hooks` counts - a read through the
// data layer, reached through a binding this scan traced back to it?  Asked in
// three places now (the count itself, what a timer re-runs, and what makes a
// function a wrapper around one), and the three have to agree or a timer would
// be charged for re-running a read the hook gate does not believe in.  Either a
// generated hook, or a function a module of the data layer wraps around one -
// the second is `sources.reads`, whose members are whole functions this scan has
// read and found to make such a call.
function countsAsQueryHook(n, sources) {
  if (!n || n.type !== 'CallExpression') return false;
  if (n.callee.type === 'Identifier' && sources.reads.has(n.callee.name)) return true;
  return !!queryHookName(n.callee, sources);
}

// The endpoint a generated hook reads, and whether it reads it or only triggers
// it.  RTK Query names the family after the endpoint - `useSellersQuery`,
// `useLazySellersQuery` - so the name is the only place the endpoint is written,
// and the lazy prefix is the only thing that distinguishes a screen that
// subscribes from a screen that fires the request by hand.
const QUERY_HOOK_ENDPOINT = /^use([A-Z]\w*?)(Query|QuerySubscription|QueryState|Mutation)$/;
const LAZY_QUERY_HOOK = /^useLazy([A-Z]\w*?)Query$/;
function queryEndpoint(name) {
  const lazy = LAZY_QUERY_HOOK.exec(name);
  if (lazy) return { name: lazy[1], lazy: true };
  const m = QUERY_HOOK_ENDPOINT.exec(name);
  return m && m[2] === 'Query' ? { name: m[1], lazy: false } : null;
}

// Every name in this file that re-runs a request the file itself already reads.
// Two spellings of the same thing, and both are what the step replaces with a
// refresh interval on the query:
//
//   the handle's own `refetch`, reached by name - destructured, destructured
//   under another name, read off the handle as a member, or handed on once more
//   through a plain alias.  A timer does not have to *call* it inside a body:
//   `setInterval(refetch, 1000)` is the plainest spelling there is, and reading
//   only bodies left it uncounted;
//
//   a lazy trigger for an endpoint this same file also subscribes to.  The
//   trigger on its own is not the construct - the reference solution keeps three
//   timers that trigger an endpoint their screen does *not* read, which the
//   instruction explicitly permits - so what makes it one is that the screen
//   already reads that endpoint and the timer is repeating that read.  Same
//   file, same endpoint name, no reasoning across modules.
//
// The anchor throughout is a call `rtk-query-hooks` counts, never a name: a
// `refetch` of the file's own making is a method it wrote, and the step never
// forbade one.
function queryRequestNames(ast, counted, hookName) {
  const names = new Set();
  const handles = new Set();
  const subscribed = new Set();
  const triggers = [];
  const aliases = [];

  walk(ast.program, (n) => {
    if (n.type === 'CallExpression') {
      const ep = queryEndpoint(hookName(n) || bare(calleeName(n)));
      if (ep && !ep.lazy && counted(n)) subscribed.add(ep.name);
      return;
    }
    if (n.type !== 'VariableDeclarator' || !n.id || !n.init) return;
    if (n.id.type === 'Identifier') {
      // Collected whatever the initialiser is, and resolved below once the
      // names it could refer to are all known, so a file that aliases above the
      // line it destructures is read the same as one that does not.
      if (n.init.type === 'Identifier') aliases.push({ local: n.id.name, from: n.init.name });
      else if (n.init.type === 'MemberExpression' && !n.init.computed
               && n.init.object.type === 'Identifier' && isName(n.init.property, 'refetch')) {
        aliases.push({ local: n.id.name, handle: n.init.object.name });
      }
    }
    if (!counted(n.init)) return;
    const ep = queryEndpoint(hookName(n.init) || bare(calleeName(n.init)));
    if (n.id.type === 'Identifier') handles.add(n.id.name);
    else if (n.id.type === 'ObjectPattern') {
      for (const pr of n.id.properties) {
        if (pr.type === 'ObjectProperty' && keyName(pr) === 'refetch'
            && pr.value && pr.value.type === 'Identifier') names.add(pr.value.name);
      }
    } else if (n.id.type === 'ArrayPattern' && ep && ep.lazy) {
      const first = (n.id.elements || [])[0];
      if (first && first.type === 'Identifier') {
        triggers.push({ local: first.name, endpoint: ep.name });
      }
    }
  });

  for (const t of triggers) if (subscribed.has(t.endpoint)) names.add(t.local);
  // One hop, not a chain: a name handed on once is still the same function, and
  // following it further would be guessing at indirection this scan did not read.
  for (const a of aliases) {
    if (a.from !== undefined ? names.has(a.from) : handles.has(a.handle)) names.add(a.local);
  }
  return { names, handles };
}

// Is this expression the request itself, rather than something that calls it?
const isQueryRequest = (e, req) => {
  if (!e) return false;
  if (e.type === 'Identifier') return req.names.has(e.name);
  if (e.type === 'MemberExpression' && !e.computed && e.object.type === 'Identifier'
      && isName(e.property, 'refetch')) {
    return req.handles.has(e.object.name);
  }
  return false;
};

// The functions in this module that read through the data layer on a caller's
// behalf: a screen that calls one is reading its data through the data layer
// just as surely as one that calls the generated hook itself, and a tree that
// gives each endpoint its own hook - a common way to write this - measured a
// dozen reads where it had forty.
//
// Two conditions, and the second is the whole of the evidence: the module
// imports from the data layer, and the function's body contains a call this scan
// already counts.  Forty exported functions that call nothing still count
// nothing, so the only way to the floor is still forty real reads.  The
// function's own name is not one of the conditions - `useSellers` and
// `getSellers` wrap the same call, and which one a tree wrote is a naming
// convention rather than a property of the data layer.
//
// Components are not wrappers.  A function that builds UI is React's other kind
// of function, and counting it would credit a caller for rendering a screen
// rather than for reading data; the distinction is the library's own, so the
// test is the library's own too - a body that produces elements is a component.
//
// Deliberately still not resolved: a wrapper whose own call is itself behind
// another import.  Guessing at arbitrary indirection would be a floor crediting
// code the scanner never read, which is the mirror of the mistake the provenance
// anchoring removed.
function wrapperHooks(ast, counted) {
  const out = new Set();
  const consider = (name, fn) => {
    if (!name || !fn) return;
    let reads = false;
    let renders = false;
    walk(fn, (n) => {
      if (!reads && counted(n)) reads = true;
      if (!renders && (n.type === 'JSXElement' || n.type === 'JSXFragment')) renders = true;
    });
    if (reads && !renders) out.add(name);
  };
  walk(ast.program, (n) => {
    if (n.type === 'FunctionDeclaration' && n.id) consider(n.id.name, n);
    else if (n.type === 'VariableDeclarator' && n.id && n.id.type === 'Identifier' && n.init
             && (n.init.type === 'FunctionExpression' || n.init.type === 'ArrowFunctionExpression')) {
      consider(n.id.name, n.init);
    }
  });
  return out;
}

// The modules that define this repository's data layer: a file that calls the
// toolkit's `createApi`, and any file that re-exports one.  `export ... from` and
// `export * from` both publish the generated hooks under a second path, and a
// component importing from there is still importing from the data layer, so the
// set is grown to a fixpoint - a re-export can itself be re-exported.
function apiModules(parsed, known) {
  const defines = new Set();
  const reexports = new Map();
  for (const p of parsed) {
    const out = [];
    walk(p.ast.program, (n) => {
      if (n.type === 'CallExpression' && toolkitExport(n.callee, p.imports) === 'createApi') {
        defines.add(p.rel);
      } else if ((n.type === 'ExportNamedDeclaration' || n.type === 'ExportAllDeclaration')
                 && n.source) {
        const r = resolveModule(p.rel, n.source.value, known);
        if (r) out.push(r);
      }
    });
    reexports.set(p.rel, out);
  }
  for (let changed = true; changed;) {
    changed = false;
    for (const [rel, out] of reexports) {
      if (!defines.has(rel) && out.some((o) => defines.has(o))) {
        defines.add(rel);
        changed = true;
      }
    }
  }
  return defines;
}

// Does this node make an HTTP request itself?  `fetch` however it is qualified
// or reached, `new XMLHttpRequest`, jQuery's `ajax`, `axios` bare or through one
// of its methods, and any call on a binding imported from one of the client
// packages - which is the same property written in every spelling this tree
// could reach for.  `aliases` is what this file named the browser's own
// primitive if it named it at all: the module still goes to the server on its
// own account, and which identifier it does that under is not the property.
function isOwnRequest(n, imports, aliases) {
  const rootOf = (e) => {
    if (!e) return null;
    if (e.type === 'Identifier') return e.name;
    if (e.type === 'MemberExpression' && !e.computed && e.object.type === 'Identifier') {
      return e.object.name;
    }
    return null;
  };
  if (n.type === 'NewExpression') {
    const c = n.callee;
    const nm = c && c.type === 'MemberExpression' && !c.computed
      && c.property.type === 'Identifier' ? c.property.name : rootOf(c);
    return nm === 'XMLHttpRequest';
  }
  if (n.type !== 'CallExpression') return false;
  const name = calleeName(n);
  if (bare(name) === 'fetch') return true;
  // `window['fetch'](url)` is the same call written past the dotted spelling
  // `calleeName` can read, and one hop away is `const f = fetch; f(url)`.
  if (isFetchRef(n.callee)) return true;
  if (n.callee.type === 'Identifier' && aliases && aliases.has(n.callee.name)) return true;
  if (name === '$.ajax' || name === 'jQuery.ajax') return true;
  const root = rootOf(n.callee);
  if (root === 'axios') return true;
  const b = root ? imports.get(root) : null;
  return !!(b && HTTP_CLIENT_MODULES.has(b.source));
}

// Does this function body reach the network, directly or through the store?
const reachesData = (node, imports, req, aliases) => {
  let found = false;
  if (!node) return false;
  walk(node, (n) => {
    if (found) return;
    if (n.type === 'CallExpression'
        && (FETCHY_CALLS.has(bare(calleeName(n))) || isQueryRequest(n.callee, req))) found = true;
    else if (isOwnRequest(n, imports, aliases)) found = true;
  });
  return found;
};

// StrictMode on the path that actually mounts, rather than anywhere in a module
// the build happens to compile - and, from the other side, a root render that is
// not wrapped at all.
//
// `strict-mode` counts the element wherever it appears, which is the right
// question for "is the instrument in this tree at all" and the wrong one for the
// property the step states - StrictMode *around the application*.  A
// `<StrictMode><div /></StrictMode>` beside the provider satisfies the first and
// wraps nothing, and the development run that then reports a quiet console is
// quiet because the second mount pass never happened: the heaviest check in the
// step would be paid for the absence of the work it exists to force.
//
// So this pass reads the element only where it is what a root renders: the
// outermost JSX of the argument to a `.render(...)` whose receiver is a root
// from react-dom's client entry point, either the call itself
// (`createRoot(el).render(<StrictMode>...)`) or a name bound to it in the same
// file (`const root = createRoot(el); root.render(<StrictMode>...)`).  The
// receiver is resolved through the package by `rootExport`, so a `render` method
// on something else is not a mount.
//
// Two gates come out of the one read, because a floor alone cannot state the
// property.  `strict-mode-root` is the floor - at least one root render is
// wrapped - and `root-render-unwrapped` is its complement: a root render this
// pass could read and whose outermost element is not StrictMode.  A floor on its
// own is satisfied by a decoy, since a second `createRoot(...).render(
// <StrictMode><div /></StrictMode>)`, or a first throwaway `root.render` on the
// real container overwritten by a second, leaves the application mounted the
// ordinary way while a wrapped render exists somewhere in the file.  `create-root`
// at `max: 1` does not close it either: the overwrite shape has one root and two
// renders.  "At least one wrapped, and none unwrapped" is the pair that says
// every root render is wrapped, which is the sentence the step actually writes.
//
// One binding hop, in one file, the way the timer callback and the base query
// are resolved - and now on the argument as well as on the receiver, because
// `const ui = <StrictMode>...</StrictMode>; root.render(ui)` hands the root the
// element the step asks for and a pass that read only the inline spelling would
// charge a correct tree for where it put the element rather than for which
// element it is.  A root or an element handed across a module boundary is not
// followed.
//
// What it cannot read it never credits: a render whose argument is a call, a
// conditional, or a name this file does not bind to JSX is not a wrapped render
// as far as the floor is concerned.  Whether it is charged to the complement
// depends on the rest of its file, which is why the walk collects a file's root
// renders before it scores any of them.
//
// A lone unreadable render is charged to neither.  `render(buildTree())` as the
// only root render in its file is a tree that mounts once through a helper, and
// failing it would fail a spelling nobody could read rather than a property -
// the worse of the two errors, and the one this tri-state exists to avoid.
//
// An unreadable render *beside a readable wrapped one, in the same file*, is
// charged as unwrapped.  Otherwise the pair has a hole exactly the shape of the
// decoy it was written to close: a throwaway
// `createRoot(el).render(<StrictMode><div /></StrictMode>)` takes the floor
// while the real mount is spelled `createRoot(app).render(React.createElement(
// ...))` and is charged to nothing, so both halves read as a tree whose every
// root render is wrapped while `#app` was mounted without StrictMode.  A file
// that has shown it can write the readable spelling and then hands a second
// root something this pass cannot follow is the case where refusing to guess is
// the more expensive error, and the candidate's fix is to write the same
// spelling twice.
//
// A fragment holding one element is unwrapped before the element is read:
// `<><StrictMode>...</StrictMode></>` renders exactly what `<StrictMode>...`
// renders, so refusing it would be refusing a spelling.  A fragment holding any
// other number of children is read as an unwrapped render rather than as
// something unreadable: a second child is precisely the sibling outside
// StrictMode this pass exists to refuse, and a fragment with none mounts no
// application at all.
function strictModeRoots(rel, ast, imports) {
  // Which names in this file hold a root, read before the walk: the root is
  // written above the render in every ordinary spelling, but a pass that learned
  // about the binding on its way past would answer differently depending on the
  // order the statements happen to be in.
  const roots = new Set();
  // And which names hold an element, for the hop on the argument, read in the
  // same pass and for the same reason.
  const elements = new Map();
  walk(ast.program, (n) => {
    if (n.type === 'VariableDeclarator' && n.id && n.id.type === 'Identifier') {
      if (rootExport(n.init, imports)) roots.add(n.id.name);
      else if (n.init && (n.init.type === 'JSXElement' || n.init.type === 'JSXFragment')) {
        elements.set(n.id.name, n.init);
      }
    } else if (n.type === 'AssignmentExpression' && n.left.type === 'Identifier'
               && rootExport(n.right, imports)) {
      roots.add(n.left.name);
    }
  });

  // The children of a fragment that render.  Indentation is JSXText and
  // `{/* ... */}` is an empty expression; neither is a child that renders.
  const kidsOf = (node) => (node.children || []).filter((c) => {
    if (c.type === 'JSXText') return c.value.trim() !== '';
    if (c.type === 'JSXExpressionContainer') return c.expression.type !== 'JSXEmptyExpression';
    return true;
  });

  // The JSX value an expression denotes, as far as one binding hop carries it,
  // or null when this file does not say.
  const asJsx = (e) => {
    if (!e) return null;
    if (e.type === 'JSXElement' || e.type === 'JSXFragment') return e;
    if (e.type === 'JSXExpressionContainer') return asJsx(e.expression);
    if (e.type === 'Identifier') return asJsx(elements.get(e.name));
    return null;
  };

  // What a root is handed to render: `{ el, strict }` for an argument this pass
  // read, or null for one it did not.
  const outermost = (arg) => {
    let e = asJsx(arg);
    if (!e) return null;
    while (e.type === 'JSXFragment') {
      const kids = kidsOf(e);
      if (kids.length !== 1) return { el: e, strict: false };
      const inner = asJsx(kids[0]);
      if (!inner) return null;
      e = inner;
    }
    return { el: e, strict: isStrictModeElement(e.openingElement.name, imports) };
  };

  // The tag as written, so a site can name the element that was rendered rather
  // than the one the gate was looking for.
  const tagOf = (el) => {
    if (!el || el.type !== 'JSXElement') return 'fragment';
    const n = el.openingElement.name;
    if (n.type === 'JSXIdentifier') return n.name;
    if (n.type === 'JSXMemberExpression' && n.object.type === 'JSXIdentifier'
        && n.property.type === 'JSXIdentifier') {
      return `${n.object.name}.${n.property.name}`;
    }
    return 'element';
  };

  // Which call this is, and under what receiver: `.render()` on a root, or
  // `hydrateRoot`, which is the other member of the client entry point's pair
  // and takes the element as its second argument instead of creating a root to
  // hand it to.  `create-root` counts both, so this counts both; nothing in this
  // tree hydrates, and a floor that refused the spelling would be refusing it
  // for that reason rather than for a property.
  const rendered = (n) => {
    if (rootExport(n, imports) === 'hydrateRoot') {
      return {
        arg: n.arguments[1],
        site: (el) => `hydrateRoot(el, <${el}>)`,
        raw: (what) => `hydrateRoot(el, ${what})`,
      };
    }
    const c = n.callee;
    if (c.type !== 'MemberExpression' || c.computed || !isName(c.property, 'render')) return null;
    const on = c.object.type === 'Identifier'
      ? (roots.has(c.object.name) ? c.object.name : null)
      : (rootExport(c.object, imports) ? 'createRoot(...)' : null);
    return on ? {
      arg: n.arguments[0],
      site: (el) => `${on}.render(<${el}>)`,
      raw: (what) => `${on}.render(${what})`,
    } : null;
  };

  // Collected before any of them is scored, because what an unreadable render
  // means depends on whether a readable wrapped one is in the same file, and a
  // pass that decided on its way past would answer differently depending on
  // which of the two was written first.
  const mounts = [];
  walk(ast.program, (n) => {
    if (n.type !== 'CallExpression') return;
    const r = rendered(n);
    if (!r) return;
    mounts.push({ node: n, site: r.site, raw: r.raw, read: outermost(r.arg) });
  });

  const anyWrapped = mounts.some((m) => m.read && m.read.strict);
  for (const m of mounts) {
    if (!m.read) {
      // Unreadable.  Charged only beside a readable wrapped render; see above.
      if (anyWrapped) {
        hit('root-render-unwrapped', rel, m.node,
          m.raw('an argument this file does not resolve to JSX'));
      }
      continue;
    }
    const where = m.read.el.openingElement || m.read.el.openingFragment || m.node;
    if (m.read.strict) hit('strict-mode-root', rel, where, m.site('StrictMode'));
    else hit('root-render-unwrapped', rel, where, m.site(tagOf(m.read.el)));
  }
}

// Writing over a console method, which is the one edit to this tree that makes a
// count of what the development run reported stop being a reading of what the
// application reported.  A ceiling rather than a floor and not scored on its own:
// no correct migration of this tree reassigns `console.error`, so it costs a tree
// that did the work nothing, and it takes the larger of the two channels the
// development driver listens on out of a candidate's reach.
//
// Its own pass, beside strictModeRoots, because the question spans a whole
// assignment or call rather than one node the main switch could key on.
function consoleSuppressions(rel, ast) {
  walk(ast.program, (n) => {
    if (n.type === 'AssignmentExpression') {
      const m = consoleMethodName(n.left);
      if (m) hit('console-suppressions', rel, n, `console.${m} = ...`);
      return;
    }
    if (n.type !== 'CallExpression') return;
    const name = calleeName(n);
    const args = n.arguments || [];
    // `Object.defineProperty(console, 'error', {...})` and
    // `Object.assign(console, {error() {}})` replace the same method the plain
    // assignment does, written past the assignment a ceiling on `=` can read.
    if (name === 'Object.defineProperty' && isConsoleRef(args[0])
        && args[1] && args[1].type === 'StringLiteral'
        && CONSOLE_METHODS.has(args[1].value)) {
      hit('console-suppressions', rel, n, `Object.defineProperty(console, '${args[1].value}')`);
      return;
    }
    if (name !== 'Object.assign' || !isConsoleRef(args[0])) return;
    for (const a of args.slice(1)) {
      if (!a || a.type !== 'ObjectExpression') continue;
      for (const prop of a.properties || []) {
        const k = keyName(prop);
        if (k && CONSOLE_METHODS.has(k)) {
          hit('console-suppressions', rel, prop, `Object.assign(console, {${k}})`);
        }
      }
    }
  });
}

// The other half of that question, asked of the build rather than of a module.
// A config that hands the bundler `drop: ['console']` removes the calls from the
// development transform as well as from the production build, so the run the
// development driver listens to has nothing left to say; `pure` marks them
// droppable by name and `define` rewrites the object out from under them.  None
// of the three is an assignment, so the ceiling above cannot read any of them.
//
// A ceiling rather than a floor and not scored on its own, for the same reason
// as `console-suppressions`: no correct migration of this tree configures its
// build this way, so it costs a tree that did the work nothing.
//
// Its own pass, beside consoleSuppressions, because the question is about a
// property of an options object and the array or object hanging off it rather
// than about one node the main switch could key on.
const DROPPED_CONSOLE_DEFINES = new Set([
  'console', 'console.error', 'console.warn', 'console.assert',
]);

// Only a build's own configuration can configure a build.  The keys this pass
// reads - `drop`, `pure`, `define` - are ordinary English words that an
// application's own options object is entitled to use, and this pass runs over
// every file of every step, so the question is asked only where the answer
// could mean anything.
const isBuildConfig = (rel) => {
  const base = rel.split('/').pop() || '';
  return /^(vite|rollup|esbuild|webpack|babel)\.config\.[cm]?[jt]s$/.test(base)
    || /\.config\.[cm]?[jt]s$/.test(base)
    || /^\.babelrc(\.[cm]?js)?$/.test(base);
};

// Which files are read as a build's own configuration.  The basename test is
// the floor; the two additions below are the routes a rename takes.  Filled
// in once, before the per-file scan, because both additions are questions
// about the tree rather than about one file.
const buildConfigs = new Set();

// The keys that hand a bundler a *list* of what to take out: `drop` and `pure`
// are esbuild's, `pure_funcs` is terser's, and all three are read the same way.
const CONSOLE_DROP_LISTS = new Set(['drop', 'pure', 'pure_funcs']);
// And the transforms that do it from inside the pipeline instead of from an
// option, which a configuration names in a string wherever it lists plugins.
const REMOVE_CONSOLE_PLUGINS = /transform-remove-console|vite-plugin-remove-console/;

function buildConsoleDrops(rel, src, ast) {
  // The string members of an array literal, and nothing at all for any other
  // node: each shape is tested before it is read rather than assumed.
  const strings = (n) => (n && n.type === 'ArrayExpression' ? (n.elements || []) : [])
    .filter((e) => e && e.type === 'StringLiteral' && typeof e.value === 'string')
    .map((e) => e.value);
  walk(ast.program, (n) => {
    // A plugin that removes the calls during the transform is named, not
    // configured, so the string is the whole of the evidence.
    if (n && n.type === 'StringLiteral' && typeof n.value === 'string'
        && REMOVE_CONSOLE_PLUGINS.test(n.value)) {
      hit('build-console-dropped', rel, n, n.value);
      return;
    }
    if (!n || n.type !== 'ObjectProperty' || n.computed) return;
    const k = keyName(n);
    if (!k) return;
    const v = n.value;
    if (CONSOLE_DROP_LISTS.has(k)) {
      // `console`, `console.error` and `console.log` are the same intent named
      // at three depths, so the prefix is what is matched rather than any one
      // of them.  A list this pass cannot read - a ternary, a name bound above,
      // a spread - is reported as the key and its shape: what a build does with
      // a list it computes is not readable here, and silence about it would be
      // a reading of the wrong thing.
      if (v && v.type === 'ArrayExpression') {
        const s = strings(v).find((x) => x.startsWith('console'));
        if (s !== undefined) hit('build-console-dropped', rel, n, `${k}: ['${s}']`);
      } else {
        // A list this pass cannot read as a literal - a ternary, a name bound
        // above, a spread - is read as text instead of guessed at.  What a build
        // does with a list it computes is not readable here, but whether the
        // expression names the console at all is, and an expression that never
        // says the word removes no console call however it evaluates.  The
        // alternative, hitting on every computed list, fails a configuration
        // whose `pure` is about something else entirely - which is the same
        // correct-tree-loses-points defect the basename narrowing was opened for.
        const text = src.slice(v.start, v.end);
        if (text.includes('console')) {
          hit('build-console-dropped', rel, n, `${k}: <computed, names console>`);
        }
      }
      return;
    }
    // terser's own switch.  It takes no list, so the key is the whole of the
    // intent - except when it is turned off, which is a configuration saying
    // the calls stay and must not be read as a configuration removing them.
    if (k === 'drop_console') {
      if (!(v && v.type === 'BooleanLiteral' && v.value === false)) {
        hit('build-console-dropped', rel, n, 'drop_console');
      }
      return;
    }
    if (k !== 'define' || !v || v.type !== 'ObjectExpression') return;
    // The keys of a `define` map are written as string literals, which is the
    // spelling keyName already reads beside the identifier form.
    for (const prop of v.properties || []) {
      if (!prop || prop.type !== 'ObjectProperty' || prop.computed) continue;
      const dk = keyName(prop);
      if (dk && DROPPED_CONSOLE_DEFINES.has(dk)) {
        hit('build-console-dropped', rel, prop, `define: {'${dk}'}`);
      }
    }
  });
}

// Whether the two concurrent hooks this tree adopts are *used*, as opposed to
// called.  `concurrent-hooks` counts the calls, which is the right question for
// "does this tree know these hooks exist" and the wrong one for a floor: both
// have a shape that satisfies a call count and defers nothing.
//
//   const deferred = useDeferredValue(list);   // and the rows render `list`
//   const [, startTransition] = useTransition();  // and nothing ever calls it
//
// Neither is a trap a candidate falls into by accident - they are what writing
// the hook for the gate rather than for the render looks like - so these two
// ids exist to guard the floor rather than to be scored on their own.
//
// Its own pass, beside strictModeRoots and consoleSuppressions, for the same
// reason as both: the question spans a declaration and every later reference to
// what it bound, and a switch on one node at a time cannot ask it.
function concurrentUses(rel, ast, imports) {
  // Both ids at once, because the union is not a third question: every site of
  // either one is a site of a hook whose result this file does something with,
  // and a guard that wants "the step has begun" reads the union rather than
  // two counts it would have to add up.
  const used = (id, node, what) => {
    hit(id, rel, node, what);
    hit('concurrent-hooks-used', rel, node, what);
  };
  // Every identifier the file mentions in a position that reads it: not the
  // name half of `x.name`, not the key half of `{name: 1}`, and not the `id` of
  // the declarator that bound it.  A name a file never reads is a name the
  // render does not depend on, whatever it was assigned.
  const reads = new Map();
  const bound = new Set();
  walk(ast.program, (n, parent) => {
    if (n.type !== 'Identifier' || !parent) return;
    if (parent.type === 'MemberExpression' && parent.property === n && !parent.computed) return;
    if ((parent.type === 'ObjectProperty' || parent.type === 'Property')
        && parent.key === n && !parent.computed) return;
    if (parent.type === 'VariableDeclarator' && parent.id === n) { bound.add(n); return; }
    if (parent.type === 'ArrayPattern' || parent.type === 'ObjectPattern') { bound.add(n); return; }
    reads.set(n.name, (reads.get(n.name) || 0) + 1);
  });

  // Whether a call was handed a function with anything in it.
  // `startTransition(() => setTime('week'))` is the shape the step asks for;
  // `startTransition(() => {})` clears a call count and moves no update into a
  // transition.  A function passed by name is taken on trust - following it
  // would be a second question, and a candidate who named the callback is not
  // the candidate this test is for.
  const movesSomething = (call) => {
    const fn = (call.arguments || [])[0];
    if (!fn) return false;
    if (fn.type === 'Identifier') return true;
    if (fn.type !== 'ArrowFunctionExpression' && fn.type !== 'FunctionExpression') return false;
    let work = false;
    walk(fn.body, (b) => {
      if (b.type === 'CallExpression' || b.type === 'AssignmentExpression') work = true;
    });
    return work;
  };

  // Which names this file calls that way, for the destructured case below: the
  // call is written inside a handler, well away from the `useTransition()` that
  // bound the name, so the two are matched by name rather than by position.
  const started = new Set();
  walk(ast.program, (n) => {
    if (n.type !== 'CallExpression' || !n.callee || n.callee.type !== 'Identifier') return;
    if (movesSomething(n)) started.add(n.callee.name);
  });

  // React's standalone `startTransition`, resolved through its import: the same
  // API without the pending flag, and the same evidence.
  walk(ast.program, (n) => {
    if (n.type !== 'CallExpression') return;
    if (moduleExport(n.callee, imports, REACT_MODULE) !== 'startTransition') return;
    if (movesSomething(n)) used('transition-start', n, 'startTransition(...)');
  });

  walk(ast.program, (n) => {
    if (n.type !== 'VariableDeclarator' || !n.init) return;
    const from = moduleExport(n.init.callee, imports, REACT_MODULE);
    if (from === 'useTransition') {
      // `const [isPending, startTransition] = useTransition()`.  The second
      // element is the one that moves an update; the first is a flag this
      // application has nothing to do with, and is not asked for.
      if (!n.id || n.id.type !== 'ArrayPattern') return;
      const start = (n.id.elements || [])[1];
      if (!start || start.type !== 'Identifier') return;
      if (started.has(start.name)) {
        used('transition-start', n, start.name);
      }
      return;
    }
    if (from === 'useDeferredValue') {
      // `const rows = useDeferredValue(list)` and something else reads `rows`.
      if (!n.id || n.id.type !== 'Identifier') return;
      if ((reads.get(n.id.name) || 0) > 0) {
        used('deferred-value-used', n, n.id.name);
      }
    }
  });
}

// A state write an effect does not do itself, but schedules for immediately
// after itself.  `set-state-in-effect` reads the effect's body, so moving
// `setRows(...)` into `setTimeout(() => setRows(...), 0)` silences the rule
// while leaving the derived state exactly where it was - the value is still
// held in `useState`, still written by an effect, still a render behind.  This
// pass asks the question the rule cannot: does a scheduling call inside an
// effect, of a kind that waits for nothing, end in a state setter.
//
// Its own pass rather than a case in the walk below, because the question spans
// three nodes - the effect, the deferral, and the setter - and a switch on one
// node at a time cannot ask it.  Nothing here is about deferral as such: an
// effect that writes state when a request answers, when a subscription reports,
// or when an interval fires is a component waiting for the world, and the
// world is not this render.  Only the schedulers that mean "the same work, one
// task later" are counted.
function deferredStateWrites(rel, ast, imports) {
  // The setters this file bound: `const [rows, setRows] = useState()` and the
  // dispatch half of `useReducer`.  By name and not by position, because the
  // write is written inside a callback well away from the call that bound it.
  // A name is enough here - a file that binds `setRows` from `useState` and
  // calls `setRows` is calling that setter - and the alternative, counting
  // anything shaped like `setX`, would read a plain helper as state.  `tuples`
  // is the same binding left undestructured, whose setter is reached as `s[1]`.
  const setters = new Set();
  const tuples = new Set();
  walk(ast.program, (n) => {
    if (n.type !== 'VariableDeclarator' || !n.init || !n.id) return;
    const b = reactHook(n.init, imports);
    if (b !== 'useState' && b !== 'useReducer') return;
    if (n.id.type === 'Identifier') { tuples.add(n.id.name); return; }
    if (n.id.type !== 'ArrayPattern') return;
    const set = (n.id.elements || [])[1];
    if (set && set.type === 'Identifier') setters.add(set.name);
  });
  if (!setters.size && !tuples.size) return;

  // Local names bound straight to a scheduler - `const q = queueMicrotask`, or
  // `const t = globalThis.setTimeout`.  One hop and no further: a scheduler
  // reached through two renamings is not something anybody writes except to be
  // reached through two renamings, and a scan that chased arbitrarily far would
  // start reading ordinary indirection as evasion.
  const aliases = new Map();
  walk(ast.program, (n) => {
    if (n.type !== 'VariableDeclarator' || !n.id || n.id.type !== 'Identifier') return;
    const init = n.init;
    if (!init) return;
    const src = init.type === 'Identifier' ? init.name
      : (init.type === 'MemberExpression' && !init.computed
         && init.object.type === 'Identifier' && GLOBAL_HOSTS.has(init.object.name)
         && init.property.type === 'Identifier' ? init.property.name : null);
    if (!src) return;
    if (src === 'setTimeout' || IMMEDIATE_SCHEDULERS.has(src)) aliases.set(n.id.name, src);
  });

  // Local functions that defer on their caller's behalf.  `function later(set,
  // v) { queueMicrotask(() => set(v)); }` puts the scheduler nowhere near the
  // effect and the setter nowhere near the scheduler; calling it from an effect
  // with a setter as an argument is the same write put off the same way.
  const helpers = new Set();
  for (const [fname, fn] of localFunctions(ast)) {
    let defers = false;
    walk(fn, (n) => {
      if (n.type === 'CallExpression' && immediateDeferral(n, aliases)) defers = true;
    });
    if (defers) helpers.add(fname);
  }

  // A reference to one of this file's setters, in either spelling: the
  // destructured name, or element 1 of the tuple the hook returned.
  const isSetterRef = (n) => {
    if (!n) return false;
    if (n.type === 'Identifier') return setters.has(n.name);
    return n.type === 'MemberExpression' && n.computed
      && n.object.type === 'Identifier' && tuples.has(n.object.name)
      && n.property.type === 'NumericLiteral' && n.property.value === 1;
  };
  const setterName = (n) => (n.type === 'Identifier' ? n.name : `${n.object.name}[1]`);

  // One reading per write, not per deferral it is wrapped in: a setter inside
  // a timer inside a timer is one derived value put off twice, and counting the
  // nesting would price it as two.
  const counted = new Set();
  const take = (node, what) => {
    if (counted.has(node)) return;
    counted.add(node);
    hit('effect-defers-set-state', rel, node, what);
  };

  walk(ast.program, (n) => {
    if (n.type !== 'CallExpression') return;
    if (!EFFECT_HOOKS.has(reactHook(n, imports) || '')) return;
    const body = (n.arguments || [])[0];
    if (!body) return;
    // Functions this effect names before it uses them, so that a callback
    // written as `const onResize = () => ...` and a callback written inline are
    // the same callback to both of the questions below.
    const named = new Map(localFunctions(ast));
    walk(body, (v) => {
      if (v.type === 'FunctionDeclaration' && v.id) named.set(v.id.name, v);
      else if (v.type === 'VariableDeclarator' && v.id && v.id.type === 'Identifier'
               && v.init && (v.init.type === 'FunctionExpression'
                             || v.init.type === 'ArrowFunctionExpression')) {
        named.set(v.id.name, v.init);
      }
    });
    const asFunction = (a) => {
      if (!a) return null;
      if (a.type === 'FunctionExpression' || a.type === 'ArrowFunctionExpression') return a;
      if (a.type === 'Identifier') return named.get(a.name) || null;
      return null;
    };

    // The callbacks this effect hands to other things, which the walk below
    // steps over: their contents run when those things report, and §2 exempts
    // the write that a report caused.
    const handed = new Set();
    walk(body, (c) => {
      if (c.type !== 'CallExpression' || !isHandoff(c, aliases)) return;
      for (const a of c.arguments || []) {
        const f = asFunction(a);
        if (f) handed.add(f);
      }
    });
    walkExcept(body, (d) => {
      if (d.type !== 'CallExpression') return;
      const cb = immediateDeferral(d, aliases);
      const how = calleeName(d) || 'a scheduler';
      if (cb) {
        // The setter handed to the scheduler rather than called inside it:
        // `setTimeout(setRows, 0, sorted)` is the write with its arguments
        // moved along one.
        for (const a of d.arguments || []) {
          if (isSetterRef(a)) take(a, `${setterName(a)} passed to ${how}`);
        }
        // And every setter call anywhere inside the deferred callback, which
        // takes a deferral nested in a deferral with it: the write is no closer
        // to an event for having been put off twice.  `asFunction` because
        // `setTimeout(bump, 0)` and `setTimeout(() => bump(), 0)` defer the
        // same body.
        walkExcept(asFunction(cb) || cb, (s) => {
          if (s.type === 'CallExpression' && isSetterRef(s.callee)) {
            take(s, `${setterName(s.callee)}() via ${how}`);
          }
        }, handed);
        return;
      }
      // A local helper that defers, handed a setter to defer.
      if (helpers.has(how)) {
        for (const a of d.arguments || []) {
          if (isSetterRef(a)) take(a, `${setterName(a)} passed to ${how}()`);
        }
      }
    }, handed);
  });
}

// Which React hook a call is, by what the file imported rather than by what it
// spelled the name here.  `import { useEffect as ue }` still calls the hook, and
// a matcher that reads only the local name is one rename away from reading zero
// on a tree that deleted every dependency array.
function reactHook(call, imports) {
  const local = bare(calleeName(call));
  if (CORE_HOOKS.has(local)) return local;
  return moduleExport(call.callee, imports, REACT_MODULE);
}

function scanFile(rel, src, ast, facts) {
  const standalone = importsPropTypes(ast);
  const locals = localFunctions(ast);
  const objects = localObjects(ast);
  const calls = localCalls(ast);
  const types = typeBindings(ast);
  // Who this file can reach the request primitive and the store's dispatch
  // under, and which of its calls are calls to a parameter.  All three ask the
  // same question the ceilings are for - is the old layer still doing the work -
  // about a tree that chose its own names for the pieces.
  const fetchAliases = localRequestNames(ast);
  const dispatchers = dispatcherNames(ast);
  const returnedFns = returnedFunctions(ast);
  const paramCalls = paramCallSites(ast, returnedFns);
  const { imports, hookSources, lazyWrappers } = facts;
  // Which names in this file re-run a read it already makes.  Read before the
  // walk, because a timer may be written above the query it re-runs.
  const requests = queryRequestNames(ast, (n) => countsAsQueryHook(n, hookSources),
    (n) => queryHookName(n.callee, hookSources));
  // The options objects of the hook calls this file makes into the data layer.
  // A refresh interval is a property of a query, so the question the polling
  // gate asks is not "does the word appear" but "is it an option on one of
  // those calls".  Read before the walk, because an options object bound to a
  // name is declared above the call that passes it and so is reached first -
  // written at the call or bound a line above, the options of a call are its
  // options either way, and the binding is resolved the same one step the base
  // query and the hand-built action are.
  const queryOptions = new Set();
  walk(ast.program, (n) => {
    if (n.type !== 'CallExpression' || !countsAsQueryHook(n, hookSources)) return;
    for (const a of n.arguments) {
      const o = a && a.type === 'ObjectExpression' ? a
        : (a && a.type === 'Identifier' ? objects.get(a.name) : null);
      if (o && o.type === 'ObjectExpression') queryOptions.add(o);
    }
  });

  // Its own pass rather than a case in the walk below: the question is about a
  // whole expression - a root, a render, and the element that render is handed -
  // and a switch on one node at a time cannot ask it.
  strictModeRoots(rel, ast, imports);
  // Same reason, one question over: an assignment and the object it writes into.
  consoleSuppressions(rel, ast);
  // And the same question asked of the build's own configuration instead.
  if (buildConfigs.has(rel)) buildConsoleDrops(rel, src, ast);
  // And one over again: a hook call and every later read of what it returned.
  concurrentUses(rel, ast, imports);
  // And one more: an effect, a scheduling call inside it, and a state setter
  // at the end of that - the write `set-state-in-effect` stops seeing once it
  // is one task away from the body.
  deferredStateWrites(rel, ast, imports);

  for (const c of ast.comments || []) {
    const what = hookSuppression(c.value);
    if (what) hit('hook-suppressions', rel, c, what);
  }

  walk(ast.program, (n, parent) => {
    switch (n.type) {
      case 'ClassDeclaration':
      case 'ClassExpression':
        if (isReactComponentBase(n.superClass)) {
          hit('class-components', rel, n, n.id ? n.id.name : '<anonymous>');
        }
        break;

      case 'ImportDeclaration': {
        const from = n.source.value;
        // One site per import statement, matching the `require` form above.
        if (ICONS_V2.test(from)) hit('react-icons-deep-import', rel, n, from);
        if (RETIRED_PACKAGES.has(from)) {
          for (const s of n.specifiers) retiredHit(rel, s, from);
          if (!n.specifiers.length) retiredHit(rel, n, from);
        }
        if (/^react-router(-dom|-config)?(\/|$)/.test(from)) {
          for (const s of n.specifiers) {
            const name = s.imported ? (s.imported.name || s.imported.value) : null;
            if (name && RETIRED_ROUTER.has(name)) {
              hit('retired-router-api', rel, s, `${name} from ${from}`);
            }
          }
          // The package, not the specifier that spells it: `react-router-dom/dist/
          // index.mjs` is the same package reached past its own entry, and a scan
          // that counts only the bare name lets a tree keep the shim installed,
          // import it through its build output, and read zero here.
          if (RRD_MODULE.test(from)) hit('react-router-dom', rel, n, from);
        }
        break;
      }

      // A module telling the compiler what to do with it, which is the half of
      // the requirement no build configuration can answer: these live in the
      // source of the function they apply to, not in the plugin's options.  All
      // five forms are counted, the two that ask to be passed over and the two
      // that ask to be taken alike, because the claim the step makes is that
      // the compiler ran on this tree and not on a tree with exemptions written
      // into it - and a function that has to ask to be compiled is one the
      // compiler was not otherwise going to take.  Babel hangs directives on a
      // `directives` array at module scope and on every function body, and the
      // walk reaches both, so a component that opts itself out from inside its
      // own body is a site in the same way a whole module is.
      case 'Directive':
        if (n.value && n.value.type === 'DirectiveLiteral'
            && isCompilerDirective(n.value.value)) {
          hit('compiler-opt-out', rel, n, n.value.value);
        }
        break;

      // The other half of a route's own fetch, and the one a route table cannot
      // be read to find: the router picks `loader`, `action` and
      // `shouldRevalidate` up as *exports* of a module a route loads lazily, so
      // a `lazy` key whose function dynamically imports page.js, plus an
      // `export async function loader()` inside page.js, is a route fetch with
      // no key on any object here to count.  (Spelled out rather than shown:
      // assemble-step.sh reads the package's own relative imports back out of
      // these files with a grep that cannot tell code from comment, and an
      // example import in a comment would look to it like a missing module.)
      //
      // Counted by the exported name alone, in any module, and not only in one a
      // route can be proved to load: proving that needs the route table to
      // resolve its own dynamic specifiers, which is exactly the analysis a
      // candidate controls.  The three names carry the router's meaning and no
      // other in this repository - there is no `loader`, `action` or
      // `shouldRevalidate` export in any tree of this task - and the instruction
      // states this form as plainly as it states the key form, so a tree that
      // wants a helper under one of these names has been told to rename it.
      case 'ExportNamedDeclaration': {
        const d = n.declaration;
        if (d && d.type === 'FunctionDeclaration' && d.id
            && ROUTE_FETCH_KEYS.has(d.id.name)) {
          hit('route-loader', rel, d, `export ${d.id.name}`);
        }
        if (d && d.type === 'VariableDeclaration') {
          for (const dec of d.declarations || []) {
            if (dec.id && dec.id.type === 'Identifier' && ROUTE_FETCH_KEYS.has(dec.id.name)) {
              hit('route-loader', rel, dec, `export ${dec.id.name}`);
            }
          }
        }
        // `export {load as loader}` publishes the same route module property
        // under the name the router reads, which is the name that matters.
        for (const s of n.specifiers || []) {
          const out = s.exported ? (s.exported.name || s.exported.value) : null;
          if (out && ROUTE_FETCH_KEYS.has(out)) {
            hit('route-loader', rel, s, `export ${out}`);
          }
        }
        break;
      }

      case 'CallExpression': {
        const name = calleeName(n);
        // Set-driven queries first: these ask "is this call one of a family",
        // which the switch below cannot express without one case per member.
        // `bare` drops a `React.` / `ReactDOM.` qualifier so `useState` and
        // `React.useState` are one site of the same kind.
        const b = bare(name);
        // A call to a custom hook is a hook call site.  The step permits
        // factoring shared state and effects into one, and counting only the
        // core names would read a component that calls `useChart()` as having
        // no hooks at all - scoring a legal design as if it had fewer than it
        // has.  `use[A-Z]` is React's own convention: the hook lint rules
        // decide what is a hook from exactly this shape.  A name that merely
        // looks like a hook can only raise a floor, never lower a ceiling -
        // this gate is only ever read as a minimum.
        if (CORE_HOOKS.has(b) || /^use[A-Z]/.test(b)) hit('react-hooks', rel, n, name);
        // Through the import as well as by name: an effect called under a
        // local alias is still an effect with no dependency list.  A second
        // argument that is not an array literal is deliberately not counted
        // here - `exhaustive-deps` reports that shape itself, as a list it
        // cannot verify, so it is already a finding in the check this guards
        // and counting it twice would price one evasion as two.
        if (EFFECT_HOOKS.has(reactHook(n, imports) || '') && n.arguments.length < 2
            && !keepsRefsCurrent(n.arguments[0])) {
          hit('effect-every-render', rel, n, `${name}(fn)`);
        }
        if (REDUX_LEGACY.has(b)) hit('redux-legacy-api', rel, n, name);
        // Which package a factory came from, not what it is called here: a
        // module that declares its own `createSlice` has written a function, not
        // configured a store, and these are minima where a name on its own would
        // be the whole of the evidence.
        const tk = toolkitExport(n.callee, imports);
        if (tk && RTK_API.has(tk)) hit('rtk-api', rel, n, tk);
        // The DOM entry points under a bare name.  `import { render } from
        // 'react-dom'` is the retired call spelled without the qualifier the
        // switch below matches, and `import * as client from 'react-dom/client'`
        // is the new one spelled with a qualifier that switch does not know - a
        // ceiling that reads only one spelling is one import style from zero,
        // and a floor that does is one import style from failing a tree that
        // did the work.  Identifier form for the retired call, because the
        // member form is exactly what the switch below already counts by name -
        // `ReactDOM.render` is a ceiling, and a ceiling that let a spelling
        // through would be one import style from zero.
        //
        // The root call is the other way round: it is a *floor*, so counting it
        // by name is what costs nothing to satisfy - a local `function
        // createRoot()` would clear it with no react-dom in the file at all.
        // Both of its spellings are therefore resolved through the package the
        // instruction names, react-dom's client entry point, and the switch
        // below counts neither of them.
        if (n.callee.type === 'Identifier') {
          const rd = moduleExport(n.callee, imports, REACT_DOM_LEGACY);
          if (rd && LEGACY_RENDER_EXPORTS.has(rd)) hit('legacy-render', rel, n, `${name} (${rd})`);
        }
        const rc = rootExport(n, imports);
        if (rc) hit('create-root', rel, n, name === rc ? name : `${name} (${rc})`);
        if (ROUTER_MODERN.has(b)) hit('router-modern-api', rel, n, name);
        if (ROUTER_DATA_API.has(b)) hit('router-data-api', rel, n, name);
        if (b === 'useParams') hit('router-params-hook', rel, n, name);
        if (b === 'useOutletContext') hit('outlet-context', rel, n, name);
        // Reading a route's own fetched data.  These names belong to the router
        // and nothing else in this tree, so the name is the evidence here in the
        // same way `useParams` is, and a name-only match is what a ceiling wants:
        // resolving the import first would let a re-export through.  But a name
        // is only half of it, because an import can be renamed on the way in -
        // `import {useLoaderData as useData}` calls the hook the instruction
        // names under a local name that is not it, and a ceiling that reads the
        // local name alone is one rename away from zero.  Either match counts,
        // which also picks up `router.useLoaderData()` off a namespace import.
        if (ROUTER_LOADER_HOOKS.has(b)) {
          hit('route-loader', rel, n, name);
        } else {
          const lh = moduleExport(n.callee, imports, ROUTER_MODULE);
          if (lh && ROUTER_LOADER_HOOKS.has(lh)) hit('route-loader', rel, n, `${name} (${lh})`);
        }
        // `redirect` and `lazy` are resolved through their import rather than
        // by name: both are ordinary words, and a local helper called `lazy` is
        // not React's code split.  The import is the whole test and the local
        // name is not consulted at all - an earlier version required the callee
        // to *also* be spelled `lazy`, which made `import {lazy as lz}` read 0
        // on a tree that had split every panel, and the floor step 5 hangs on
        // this id would have failed it for the rename.
        if (moduleExport(n.callee, imports, ROUTER_MODULE) === 'redirect') {
          hit('router-navigate', rel, n, name);
        }
        // Directly, or through a local helper that calls it: both are the
        // same split, and which one a tree wrote is a matter of how many
        // times it had to write it.
        const lazyWhat = moduleExport(n.callee, imports, REACT_MODULE) === 'lazy'
          ? name
          : (n.callee.type === 'Identifier' && lazyWrappers.has(n.callee.name)
            ? `${name} (${lazyWrappers.get(n.callee.name)})` : null);
        if (lazyWhat !== null) {
          hit('route-lazy', rel, n, lazyWhat);
          // And again under the id that is React's call alone.
          hit('react-lazy', rel, n, lazyWhat);
        }
        // The two concurrent hooks under a local name.  The switch below counts
        // them by name, which is the right half of the answer for a ceiling and
        // half an answer for a floor: `import {useTransition as ut}` is legal,
        // idiomatic enough, and would score a tree that adopted the hook at
        // zero.  Resolved through the import here, and only when the name is
        // not already what the switch keys on, so nothing is counted twice.
        if (!CONCURRENT_HOOKS.has(b)) {
          const ch = moduleExport(n.callee, imports, REACT_MODULE);
          if (CONCURRENT_HOOKS.has(ch)) hit('concurrent-hooks', rel, n, `${name} (${ch})`);
        }
        // The same hole, pointing the other way.  `manual-memo` is a ceiling
        // graded down from the count the tree arrives with, so a file that
        // wrote `import {useCallback as hold}` and went on calling it would
        // read as memoisation removed when it is memoisation renamed - the one
        // edit that scores the step without doing any of it.  Resolved through
        // the import, and only where the local name is not already one the
        // switch keys on, so a call is one site rather than two.
        if (!MANUAL_MEMO_NAMED.has(name)) {
          const mm = moduleExport(n.callee, imports, REACT_MODULE);
          if (MANUAL_MEMO.has(mm)) hit('manual-memo', rel, n, `${name} (${mm})`);
        }
        // CommonJS is not how this tree is written, but a retired router name
        // destructured out of a require() is the same surviving import the
        // ImportDeclaration scan counts, and a scan that sees only one of the
        // two spellings measures the spelling rather than the API.
        if (b === 'require' && n.arguments.length === 1
            && n.arguments[0].type === 'StringLiteral'
            && /^react-router(-dom|-config)?(\/|$)/.test(n.arguments[0].value)) {
          if (RRD_MODULE.test(n.arguments[0].value)) {
            hit('react-router-dom', rel, n, n.arguments[0].value);
          }
          if (parent && parent.type === 'VariableDeclarator'
              && parent.id && parent.id.type === 'ObjectPattern') {
            for (const p of parent.id.properties || []) {
              const k = keyName(p);
              if (k && RETIRED_ROUTER.has(k)) {
                hit('retired-router-api', rel, p, `${k} from ${n.arguments[0].value}`);
              }
            }
          }
        }
        if (b === 'forwardRef') hit('forward-ref', rel, n, name);
        // One site per definition, not per factory call: the conjunction of
        // `createApi` and the toolkit's own base query is the instruction's
        // whole sentence, and counting the two halves separately scored a tree
        // with two APIs and no base query the same as a tree with one of each.
        if (tk === 'createApi' && hasToolkitBaseQuery(n, imports, objects, calls)) {
          hit('rtk-query-def', rel, n, 'createApi over fetchBaseQuery');
        }
        // The generated hooks are named by the endpoint, so the shape RTK Query
        // generates them in is all the name can say.  What makes one of them
        // this repository's data layer is where the callee came from: an import
        // from the module that defines the API, a member read off such an
        // import, the hooks a file takes off the api object it holds, or a
        // domain hook a module of this tree wraps around one of those.  Without
        // that, forty local functions called `useFooQuery` are a data layer.
        // The label falls back to the hook the callee resolves to, because a
        // bracket read has no dotted name to print and a site reported as
        // `null` tells a reader nothing about what was counted.
        if (countsAsQueryHook(n, hookSources)) {
          hit('rtk-query-hooks', rel, n,
              name || queryHookName(n.callee, hookSources) || 'query hook');
        }
        // A request this module makes itself, in whichever spelling: `fetch`
        // bare or through `window`, an XMLHttpRequest, or an HTTP client.
        if (isOwnRequest(n, imports, fetchAliases)) hit('raw-fetch', rel, n, name || 'request');
        // What the gate measures is that an object carrying a `type` is being
        // handed to something that dispatches it, which is exactly the work
        // `createSlice`'s generated creators take over.  Who that something is
        // called does not change the property: the name `dispatch`, a parameter
        // the enclosing function was handed the dispatcher in, or a name this
        // file bound to `useDispatch()` or to a `dispatch` member are one
        // construct, and a ceiling that read only the first could be cleared by
        // a rename.
        const dispatcher = b === 'dispatch' ? 'dispatch'
          : n.callee.type === 'Identifier'
            && (dispatchers.has(n.callee.name) || paramCalls.has(n)) ? n.callee.name : null;
        if (dispatcher && n.arguments[0]) {
          const a0 = n.arguments[0];
          // The literal, and the literal one binding away: `const a = {type};
          // dispatch(a)` assembles the same action, and reading only the literal
          // scored the two differently for a difference that is not the
          // property.
          const obj = a0.type === 'ObjectExpression' ? a0
            : a0.type === 'Identifier' ? objects.get(a0.name) : null;
          if (obj && obj.properties.some((pr) => keyName(pr) === 'type')) {
            hit('hand-built-action', rel, n,
                a0.type === 'ObjectExpression'
                  ? `${dispatcher}({type: ...})` : `${dispatcher}(${a0.name})`);
          }
        }
        if (b === 'setInterval' || b === 'setTimeout') {
          const arg = n.arguments[0];
          const body = arg && arg.type === 'Identifier'
            ? locals.get(arg.name)
            : arg;
          // The request may be the callback rather than something the callback
          // calls: handing `refetch` straight to the timer is the same poll.
          if (isQueryRequest(arg, requests) || reachesData(body, imports, requests, fetchAliases)) {
            hit('timer-driven-fetch', rel, n,
                `${name}(${arg.type === 'Identifier' ? arg.name : 'fn'}, ...)`);
          }
        }

        if (name === 'require' && n.arguments[0] && n.arguments[0].type === 'StringLiteral') {
          const from = n.arguments[0].value;
          if (RETIRED_PACKAGES.has(from)) retiredHit(rel, n, from);
          if (ICONS_V2.test(from)) hit('react-icons-deep-import', rel, n, from);
        }
        switch (name) {
          case 'createReactClass':
          case 'React.createClass':
          // Bare `createClass`: what react-codemod's update-react-imports makes
          // of `React.createClass`, and therefore a spelling a tree can arrive
          // at without anyone choosing it.  Counted, or the class it declares
          // stops being a class the moment its import is tidied up.
          case 'createClass':
            hit('class-components', rel, n, name);
            break;
          case 'connect':
          case 'ReactRedux.connect':
            hit('connect-hoc', rel, n, name);
            break;
          case 'findDOMNode':
          case 'ReactDOM.findDOMNode':
            hit('find-dom-node', rel, n, name);
            break;
          case 'ReactDOM.render':
          case 'ReactDOM.unstable_renderSubtreeIntoContainer':
          case 'ReactDOM.hydrate':
            hit('legacy-render', rel, n, name);
            break;
          // `createRoot` / `hydrateRoot` deliberately absent: they are counted
          // above, through the react-dom/client binding that makes them the
          // root API rather than two words.
          case 'useMemo':
          case 'React.useMemo':
          case 'useCallback':
          case 'React.useCallback':
          case 'memo':
          case 'React.memo':
            hit('manual-memo', rel, n, name);
            break;
          case 'useTransition':
          case 'React.useTransition':
          case 'useDeferredValue':
          case 'React.useDeferredValue':
            hit('concurrent-hooks', rel, n, name);
            break;
          case 'useActionState':
          case 'React.useActionState':
          case 'useFormStatus':
          case 'useOptimistic':
            hit('action-hooks', rel, n, name);
            break;
          case 'use':
          case 'React.use':
            hit('use-hook', rel, n, name);
            break;
          case 'setInterval':
          case 'window.setInterval':
            hit('hand-rolled-polling', rel, n, name);
            break;
          case 'React.cloneElement':
          case 'cloneElement':
            hit('clone-element', rel, n, name);
            break;
          default:
            break;
        }
        break;
      }

      // The one request shape that is not a call.  A module that builds its own
      // XMLHttpRequest is fetching by hand as surely as one that calls `fetch`.
      case 'NewExpression':
        if (isOwnRequest(n, imports, fetchAliases)) hit('raw-fetch', rel, n, 'new XMLHttpRequest()');
        break;

      case 'JSXAttribute': {
        const attr = n.name && n.name.name;
        if (attr === 'ref' && n.value && n.value.type === 'StringLiteral') {
          hit('string-refs', rel, n, n.value.value);
        }
        if (attr === 'activeClassName' || attr === 'activeStyle') {
          hit('retired-router-api', rel, n, attr);
        }
        // The v3 route table's own way of code-splitting a route: a prop taking
        // a `(location, callback)` loader.  v6 has no such prop.
        if (attr === 'getComponent' || attr === 'getComponents') {
          hit('route-get-component', rel, n, attr);
        }
        // A route fetch written as an attribute on a route element.  Only on
        // `<Route>`: `action` on a plain form is HTML and has nothing to do
        // with the router.
        if (ROUTE_FETCH_KEYS.has(attr) && parent && parent.type === 'JSXOpeningElement'
            && isRouteElement(parent.name, imports)) {
          hit('route-loader', rel, n, attr);
        }
        // The same route key written as an attribute rather than as an object
        // property.  `createRoutesFromElements` takes a `<Route>` tree and
        // turns it into the route objects the other branch reads, so the two
        // spellings describe the same route table and a detector that saw only
        // one of them read 0 on a tree that splits every route - which is what
        // this one did before, on this task's own reference tree.
        if (attr === 'lazy' && parent && parent.type === 'JSXOpeningElement'
            && isRouteElement(parent.name, imports)
            && n.value && n.value.type === 'JSXExpressionContainer'
            && (n.value.expression.type === 'ArrowFunctionExpression'
                || n.value.expression.type === 'FunctionExpression'
                || n.value.expression.type === 'Identifier'
                || n.value.expression.type === 'MemberExpression')) {
          hit('route-lazy', rel, n, attr);
        }
        break;
      }

      // Half of the modern router surface and all of Suspense appear only as
      // elements, never as calls, so a CallExpression-only scan would report a
      // Router v6 tree as having no router.
      case 'JSXOpeningElement': {
        // Suspense is read before the bare-tag shortcut below, because the two
        // member spellings of it never reach that line.
        if (isSuspenseElement(n.name, imports)) hit('suspense', rel, n, 'Suspense');
        const tag = n.name && n.name.type === 'JSXIdentifier' ? n.name.name : null;
        if (!tag) break;
        if (tag === 'StrictMode') hit('strict-mode', rel, n, tag);
        if (tag === 'Outlet') {
          hit('router-outlet', rel, n, tag);
          // `<Outlet context={...}>` is the other half of `useOutletContext`,
          // and the half a parent writes.  Counting both means the pair can be
          // required without requiring which side of it a file is.
          for (const a of n.attributes || []) {
            if (a.type === 'JSXAttribute' && a.name && a.name.name === 'context') {
              hit('outlet-context', rel, a, 'Outlet context');
            }
          }
        }
        if (tag === 'NavLink') hit('router-navlink', rel, n, tag);
        if (tag === 'Navigate') hit('router-navigate', rel, n, tag);
        if (ROUTER_DATA_API.has(tag)) hit('router-data-api', rel, n, tag);
        // `Route` deliberately absent: v3 spells it the same way, so counting it
        // would report the untouched baseline as already using the modern router.
        if (ROUTER_MODERN.has(tag)) hit('router-modern-api', rel, n, tag);
        break;
      }

      // The property of a JSX member name is a JSXIdentifier, not an
      // Identifier, so `isName` - which tests for the latter - answered false
      // for every element here and both queries below were dead.  Neither moved
      // a count on any tree of this task, which is why it went unnoticed: no
      // tree writes `<Ctx.Provider>` or `<React.StrictMode>`.  Asked directly of
      // the JSX name instead.
      // The opening tag only: an element's name node appears once on the way in
      // and once on the way out, and a site is an element, not a tag.
      case 'JSXMemberExpression': {
        if (!parent || parent.type !== 'JSXOpeningElement') break;
        const p = n.property && n.property.type === 'JSXIdentifier' ? n.property.name : null;
        // <Ctx.Provider> still works in React 19 but <Ctx> is the documented
        // form; step 6 scores the transition, so count the old sites.
        if (p === 'Provider') hit('context-provider-member', rel, n, 'Provider');
        // <React.StrictMode> is the same element as <StrictMode>, and the
        // JSXOpeningElement case above cannot see it: that one reads a tag only
        // when the element's name is a plain identifier.
        if (p === 'StrictMode') hit('strict-mode', rel, n, 'React.StrictMode');
        break;
      }

      case 'ClassProperty':
      case 'ClassMethod':
      case 'ObjectProperty':
      case 'ObjectMethod': {
        const k = keyName(n);
        if (!k) break;
        if (UNSAFE_LIFECYCLES.has(k)) hit('unsafe-lifecycles', rel, n, k);
        if (LEGACY_CONTEXT.has(k)) hit('legacy-context', rel, n, k);
        if (k === 'propTypes') {
          hit('prop-types', rel, n, k);
          if (standalone) hit('prop-types-standalone', rel, n, k);
        }
        if (k === 'defaultProps') hit('default-props', rel, n, k);
        // How a declarative data layer says "poll": an option on the hook that
        // reads the query, rather than a timer in a component.  Only on such a
        // hook - the property is a refresh interval declared on a query this
        // repository actually makes, and the word on a loose object is not one.
        if (k === 'pollingInterval' && queryOptions.has(parent)) {
          hit('polling-config', rel, n, k);
        }
        // A route object's own code split: a `lazy` key whose value is a
        // function that dynamically imports the route's module.  The
        // value has to be callable, because `lazy: true` is a word on an object
        // and not a split.
        if (k === 'lazy' && n.value
            && (n.value.type === 'ArrowFunctionExpression'
                || n.value.type === 'FunctionExpression'
                || n.value.type === 'Identifier')) {
          hit('route-lazy', rel, n, k);
        }
        // A route object's own fetch.  These three words are route fetches only
        // when the object carrying them is a route - one that also says where
        // it is, what it renders, or what it contains - which keeps a loose
        // options bag that happens to use the word out of the count.  And the
        // value has to be callable, for the same reason `lazy`'s does.
        if (ROUTE_FETCH_KEYS.has(k) && parent && parent.type === 'ObjectExpression'
            && (n.type === 'ObjectMethod'
                || (n.value
                    && (n.value.type === 'ArrowFunctionExpression'
                        || n.value.type === 'FunctionExpression'
                        || n.value.type === 'Identifier'
                        || n.value.type === 'MemberExpression')))
            && (parent.properties || []).some((p) => ROUTE_OBJECT_KEYS.has(keyName(p) || ''))) {
          hit('route-loader', rel, n, k);
        }
        break;
      }

      case 'AssignmentExpression': {
        const left = n.left;
        if (left.type === 'MemberExpression' && !left.computed) {
          const k = keyName(left);
          if (k === 'propTypes') {
            hit('prop-types', rel, n, k);
            if (standalone) hit('prop-types-standalone', rel, n, k);
          }
          if (k === 'defaultProps') hit('default-props', rel, n, k);
          if (k && LEGACY_CONTEXT.has(k)) hit('legacy-context', rel, n, k);
          if (k && UNSAFE_LIFECYCLES.has(k)) hit('unsafe-lifecycles', rel, n, k);
        }
        break;
      }

      // An async action creator written for redux-thunk: a function the
      // middleware calls with the store's own `dispatch`.  Named by its
      // parameter and not by what encloses it, because the tree writes them as
      // `return function (dispatch) {...}` inside an object of action creators,
      // and a candidate could as easily write an arrow.
      //
      // Two ways to be one, because the name is a spelling and the shape is the
      // construct.  Either the first parameter is called `dispatch`, or the
      // function is what another function hands back *and* calls its own first
      // parameter - which is that same object of action creators with the name
      // taken away, and is the thing redux-thunk does to a returned function.
      // A link in a curried chain is excluded: `store => next => action =>
      // next(action)` is middleware, which this step never forbade.
      // `createAsyncThunk`'s payload creator takes `(arg, thunkAPI)` and calls
      // neither, so it is not one of these - the gate forbids the construct the
      // step replaces, not asynchrony.
      case 'FunctionDeclaration':
      case 'FunctionExpression':
      case 'ArrowFunctionExpression': {
        const first = n.params && n.params[0];
        if (first && first.type === 'Identifier') {
          if (first.name === 'dispatch') hit('thunk-creator', rel, n, 'fn(dispatch, ...)');
          else if (returnedFns.has(n) && !isCurryLink(n) && invokesFirstParam(n)) {
            hit('thunk-creator', rel, n, `returned fn(${first.name}, ...) that calls it`);
          }
        }
        break;
      }

      // A reducer that decides what to do by looking at the action's type.
      // `createSlice` replaces the switch with one case reducer per action and
      // generates the types, so this is the construct the step removes; the
      // discriminant is what identifies it, not the enclosing function's name.
      case 'SwitchStatement': {
        const d = n.discriminant;
        let what = null;
        if (d && d.type === 'MemberExpression') {
          if (!d.computed && isName(d.property, 'type')) what = 'switch (action.type)';
          else if (d.computed && d.property.type === 'StringLiteral' && d.property.value === 'type') {
            what = "switch (action['type'])";
          }
        } else if (d && d.type === 'Identifier' && types.has(d.name)) {
          what = `switch (${d.name}) on a destructured type`;
        }
        if (what) hit('switch-reducer', rel, n, what);
        break;
      }

      case 'MemberExpression':
        if (!n.computed && isName(n.property, 'PropTypes') && isName(n.object, 'React')) {
          hit('react-proptypes', rel, n, 'React.PropTypes');
        }
        // A retired router name read off a default or namespace import of the
        // router package.  The named-import scan above cannot see these, and
        // `import * as Router from 'react-router'` followed by `Router.Router`
        // is the same surviving v3 API as importing the name directly.
        if (!n.computed && n.object.type === 'Identifier'
            && n.property.type === 'Identifier'
            && RETIRED_ROUTER.has(n.property.name)) {
          const b = imports.get(n.object.name);
          if (b && /^react-router(-dom|-config)?(\/|$)/.test(b.source)
              && (b.imported === '*' || b.imported === 'default')) {
            hit('retired-router-api', rel, n, `${n.property.name} from ${b.source}`);
          }
        }
        break;

      default:
        break;
    }
  });
}

// ---------------------------------------------------------------------- main

// Parse the whole tree first, then scan it.  Three of the queries are about
// provenance rather than spelling - did this factory come from the toolkit, does
// this hook come from the module that defines this repository's API - and the
// second of those is a fact about the tree, not about the file being read: the
// API module can be parsed after the component that imports from it.  A pass
// that scanned each file as it parsed it could only answer that question for
// whichever order the directory walk happened to produce.  The parse itself is
// unchanged and still happens exactly once per file.
const scanned = [...new Set(sources(ROOT))].sort();
const parsed = [];
const parseSource = (rel, src) => {
  let ast;
  try {
    ast = parseAnyDialect(src, {
      sourceType: 'unambiguous',
      allowReturnOutsideFunction: true,
      errorRecovery: true,
      attachComment: true,
    });
  } catch (e) {
    // A file the scorer cannot parse is not a free pass: it is reported, and
    // the gates that consume this treat an unparsable repository as a failure.
    parseErrors.push({ file: rel, message: String(e.message) });
    return;
  }
  fileCount++;
  parsed.push({ rel, src, ast, imports: importBindings(ast) });
};
for (const file of scanned) parseSource(path.relative(ROOT, file), fs.readFileSync(file, 'utf8'));

// The scripts written inline in HTML files.  Vite compiles the body of a
// `<script type="module">` with no `src` into the page exactly as it compiles a
// module file, so a class component written there reached the page while no
// gate read it (final review G2-R5).  A classic inline script - no `src`, and
// no `type` or a JavaScript one - is kept in the page verbatim and run there,
// and was not read either (G2-R7a).  Each body is read as a source of its own,
// named `<file>.html#inline-module-<n>` or `<file>.html#inline-script-<n>`
// (line numbers count from the body's first line), parsed in the same
// dialects, and scanned by every query like any other module.  The scripts are
// found by reach.mjs's extractor, the one provenance.mjs and eslint-run.mjs
// use, with `data-src` and `data-type` not read as `src` and `type` (G2-R5b).
// Every `<script src>` is kept for the specifier checks below.  (wave H
// 2026-10-14, H-CR7; classic scripts and the shared extractor, H-CR8)
const htmlSrcs = [];
for (const file of [...new Set(sources(ROOT, [], HTML_NAME))].sort()) {
  const rel = path.relative(ROOT, file);
  let html;
  try {
    html = fs.readFileSync(file, 'utf8');
  } catch {
    continue;
  }
  for (const sc of htmlScripts(html)) {
    if (sc.src !== null) htmlSrcs.push({ rel, dir: path.dirname(file), spec: sc.src });
  }
  for (const sc of inlineScripts(html)) parseSource(`${rel}#${sc.name}`, sc.body);
}

// Two findings about where an imported module comes from, read off every
// specifier in every source read above - a static import, a re-export, or a
// dynamic import whose specifier is a string or begins with a template
// literal's first part - and off every HTML `<script src>`.  A specifier
// computed some other way is not resolved here, as no import is.
//
// `data-uri-import` (G2-R6): a module imported from a `data:` URL.
//
// `exempt-dir-import` (G2-R7c): a module resolved into one of the two
// directories the user exempted from provenance marking on 2026-10-14, the
// root `.git` and `server/demo1/public`.  Neither is read by any source gate,
// and neither gets a marker, so a module compiled from there reached the page
// with nothing to say so.  The coordinator's ruling (2026-10-14): those
// directories are exempt from marking, not from accountability.  A relative or
// `/`-rooted specifier is resolved against the importing file (and, rooted,
// against the tree root and Vite's `/@fs/` prefix), and the result counts if it
// lies in either directory, lexically or through a symbolic link anywhere on
// its path.  An asset of one of Vite's own non-JavaScript types is not a module
// and is not counted.  Not resolved: `resolve.alias` and other build-config
// resolution, `import.meta.glob`, and `new URL(..., import.meta.url)`; provenance
// cannot see modules it did not mark, so this specifier reading is the check.
// run-gates.mjs fails `built-from-source` on a site of either.  (wave H
// 2026-10-14, H-CR7 and H-CR8)
const DATA_URL = /^\s*data:/i;
const shortSpec = (s) => (s.length > 60 ? `${s.slice(0, 60)}...` : s);
const EXEMPT_DIRS = ['.git', path.join('server', 'demo1', 'public')];
let REAL_ROOT = ROOT;
try { REAL_ROOT = fs.realpathSync(ROOT); } catch { /* the walk above would have failed */ }
const inExempt = (abs, root) => EXEMPT_DIRS.some((d) => {
  const dir = path.join(root, d);
  return abs === dir || abs.startsWith(dir + path.sep);
});
// The real path of `p`: its longest existing prefix resolved through any links,
// with the part that does not exist appended unchanged.
const realish = (p) => {
  const rest = [];
  for (let head = p; ;) {
    try {
      return path.join(fs.realpathSync(head), ...rest);
    } catch { /* not there: try the parent */ }
    const up = path.dirname(head);
    if (up === head) return p;
    rest.unshift(path.basename(head));
    head = up;
  }
};
const intoExempt = (spec, fromDir) => {
  let s = spec.trim().replace(/[?#].*$/, '');
  if (s.startsWith('/@fs/')) s = s.slice('/@fs'.length);
  else if (/^[a-z][a-z\d+.-]*:/i.test(s)) return false;
  let at;
  if (s.startsWith('/')) at = [path.join(ROOT, s), path.join(fromDir, s), s];
  else if (s.startsWith('.')) at = [path.resolve(fromDir, s)];
  else return false;
  if (VITE_NOT_JS.test(s)) return false;
  return at.some((c) => inExempt(path.normalize(c), ROOT) || inExempt(realish(c), REAL_ROOT));
};
const specifierSites = (rel, fromDir, spec, node) => {
  if (typeof spec !== 'string') return;
  if (DATA_URL.test(spec)) hit('data-uri-import', rel, node, shortSpec(spec));
  else if (intoExempt(spec, fromDir)) hit('exempt-dir-import', rel, node, shortSpec(spec));
};
const STATIC_IMPORT = new Set(['ImportDeclaration', 'ExportNamedDeclaration', 'ExportAllDeclaration']);
const literalSpec = (n) => (n && n.type === 'StringLiteral' ? n.value
  : n && n.type === 'TemplateLiteral' && n.quasis.length ? n.quasis[0].value.cooked
    : null);
for (const h of htmlSrcs) specifierSites(h.rel, h.dir, h.spec, null);
for (const p of parsed) {
  const fromDir = path.dirname(path.join(ROOT, p.rel));
  walk(p.ast.program, (n) => {
    let spec = null;
    if (STATIC_IMPORT.has(n.type)) spec = literalSpec(n.source);
    else if (n.type === 'ImportExpression') spec = literalSpec(n.source);
    else if (n.type === 'CallExpression' && n.callee && n.callee.type === 'Import') {
      spec = literalSpec(n.arguments[0]);
    }
    specifierSites(p.rel, fromDir, spec, n);
  });
}

const known = new Set(parsed.map((p) => p.rel));
const apiSet = apiModules(parsed, known);

// Which names each file can reach the data layer's hooks under.  Two rounds,
// because the second question is about the module graph: a file's own bindings
// first, then the domain hooks other modules wrap around them.  One hop, from
// the module that declares the wrapper to the modules that import it - the same
// depth `apiModules` walks, and for the same reason.
const facts = [];
for (const p of parsed) {
  // The name the module exported it under, not the name this file gave it: RTK
  // Query writes the endpoint into the generated name, so that is where the hook
  // says what it reads, and an import alias does not change what was imported.
  const apiImports = new Map();
  for (const [local, b] of p.imports) {
    const r = resolveModule(p.rel, b.source, known);
    if (r && apiSet.has(r)) apiImports.set(local, b.imported);
  }
  const own = localApiNames(p.ast, p.imports, apiImports);
  facts.push({ p, apiImports, own });
}
// The names each file can call React's `lazy` under: the helpers it declares
// itself, plus the ones it imports from a module that exports one.
const ownLazy = new Map();
const exportedLazy = new Map();
const passedOnLazy = new Map();
for (const p of parsed) {
  const own = lazyWrapperNames(p.ast, p.imports);
  ownLazy.set(p.rel, own);
  const ex = exportedLazyWrappers(p.ast, own, p.imports);
  if (ex.size) exportedLazy.set(p.rel, ex);
  const re = reExportedReactLazy(p.ast, p.imports);
  if (re.size) passedOnLazy.set(p.rel, re);
}
// Name to how it got here, rather than a bare set, because the two routes are
// different findings and a site that says which one it was is the difference
// between reading gates.json and guessing at it.
const lazyNames = new Map();
for (const p of parsed) {
  const names = new Map();
  for (const n of ownLazy.get(p.rel)) names.set(n, "wraps React's lazy");
  for (const [local, b] of p.imports) {
    const r = resolveModule(p.rel, b.source, known);
    if (!r) continue;
    const ex = exportedLazy.get(r);
    if (ex && ex.has(b.imported)) names.set(local, "wraps React's lazy");
    const re = passedOnLazy.get(r);
    if (re && re.has(b.imported)) names.set(local, "React's lazy, re-exported");
  }
  lazyNames.set(p.rel, names);
}

const declaredWrappers = new Map();
for (const f of facts) {
  if (!f.apiImports.size) continue;
  const w = wrapperHooks(f.p.ast, (n) => countsAsQueryHook(n, { names: f.own, reads: new Set() }));
  if (w.size) declaredWrappers.set(f.p.rel, w);
}
// What counts as a static import, read by both of the walks below: the
// build-configuration closure a few lines down and the entry-reach walk further
// on.  `export ... from` counts as a static import, because it is one, and
// leaving it out is what let a configuration re-export its real body from a file
// the closure never opened.  `require` is not followed, for the reason the
// binding table does not resolve it: nothing in this tree binds a module that
// way, and a build that did could not be an ESM build of it.
const STATIC_FROM = new Set([
  'ImportDeclaration', 'ExportNamedDeclaration', 'ExportAllDeclaration',
]);
const staticSources = (ast) => ast.program.body.filter(
  (n) => STATIC_FROM.has(n.type) && n.source && typeof n.source.value === 'string',
);

for (const p of parsed) if (isBuildConfig(p.rel)) buildConfigs.add(p.rel);
// A configuration the build script names by path is a configuration however
// it is spelled: `vite build --config vite.prod.mjs` has no `.config.` in it.
let pkgJson = null;
try { pkgJson = JSON.parse(fs.readFileSync(path.join(ROOT, 'package.json'), 'utf8')); } catch { pkgJson = null; }
const scripts = pkgJson && pkgJson.scripts && typeof pkgJson.scripts === 'object' ? pkgJson.scripts : {};
const byRel = new Map(parsed.map((p) => [p.rel, p]));
for (const cmd of Object.values(scripts)) {
  if (typeof cmd !== 'string') continue;
  for (const m of cmd.matchAll(/(?:--config|-c)[= ]+(\S+)/g)) {
    const rel = path.normalize(m[1].replace(/^\.\//, ''));
    if (byRel.has(rel)) buildConfigs.add(rel);
  }
  // And the flag that needs no configuration file at all.
  if (/--drop:console|--drop-console|drop_console|transform-remove-console/.test(cmd)) {
    hit('build-console-dropped', 'package.json', null, `script: ${cmd}`);
  }
}
// A file a configuration statically imports is part of that configuration.
// Closure rather than one hop, because `vite.config.js` -> `./build/opts.js`
// -> `./build/minify.js` is the same evasion written twice.
for (let added = true; added;) {
  added = false;
  for (const rel of [...buildConfigs]) {
    const p = byRel.get(rel);
    if (!p) continue;
    for (const n of staticSources(p.ast)) {
      const r = resolveModule(rel, n.source.value, known);
      if (r && byRel.has(r) && !buildConfigs.has(r)) { buildConfigs.add(r); added = true; }
    }
  }
}
for (const f of facts) {
  const reads = new Set(declaredWrappers.get(f.p.rel) || []);
  for (const [local, b] of f.p.imports) {
    const r = resolveModule(f.p.rel, b.source, known);
    const w = r && declaredWrappers.get(r);
    if (w && w.has(b.imported)) reads.add(local);
  }
  scanFile(f.p.rel, f.p.src, f.p.ast,
    { imports: f.p.imports, hookSources: { names: f.own, reads },
      lazyWrappers: lazyNames.get(f.p.rel) || new Map() });
}

// ------------------------------------------- what the entry reaches statically

// The javascript a visitor waits for before anything renders, read off the
// source rather than off the bundle: the module `index.html` asks for, and
// everything that module reaches through a chain of *static* imports.  A
// dynamic `import(...)` is deliberately not followed - the difference between
// the two is the whole of the question.
//
// This is the one query here that is about the module graph rather than about a
// spelling, and it is here rather than in the artifact gates because what it
// measures is a property of the source: which modules the entry still pulls in
// whether a visitor needs them or not.  The bundle-level reading of the same
// property is the byte budget, which measures the effect; this measures the
// arrangement that causes it, and the two are meant to be read together.

// The seeds are `index.html`'s own module scripts and the module that mounts
// the application, together rather than one in preference to the other: the
// module that mounts the application is part of the entry however index.html
// reaches it.  Taking only the html's script when it names one is what made a
// trampoline work - `index.html` pointing at a stub whose whole body is a
// dynamic import of the mounting module leaves every chart panel statically
// imported and reads as an entry that reaches nothing, because a dynamic
// import is not followed and the mounting module was never seeded.  If
// neither resolves, every module that imports the chart library counts: an
// entry a static resolver cannot find is the padding vector that the scoped
// counts in run-gates.mjs were written against, and the answer is the same
// from this side - unmeasurable is a failure of the tree, never a silent pass.
const entrySeeds = () => {
  const seeds = new Set();
  let html = '';
  try { html = fs.readFileSync(path.join(ROOT, 'index.html'), 'utf8'); } catch { html = ''; }
  for (const m of html.matchAll(/<script\b[^>]*(?<![\w-])src\s*=\s*["']([^"']+)["']/gi)) {
    const spec = m[1].replace(/^\.?\//, '');
    for (const ext of ['', '.js', '.jsx', '.mjs', '.ts', '.tsx']) {
      if (known.has(spec + ext)) { seeds.add(spec + ext); break; }
    }
  }
  for (const site of findings['create-root']) seeds.add(site.file);
  return seeds;
};

{
  const byRel = new Map(parsed.map((p) => [p.rel, p]));
  const stack = [...entrySeeds()];
  const reached = new Set();
  while (stack.length) {
    const rel = stack.pop();
    if (reached.has(rel) || !byRel.has(rel)) continue;
    reached.add(rel);
    for (const n of staticSources(byRel.get(rel).ast)) {
      const r = resolveModule(rel, n.source.value, known);
      if (r) stack.push(r);
    }
  }
  const scope = reached.size ? reached : new Set(known);

  // Whether a module carries the chart library in, which is transitive and not
  // a spelling.  A module carries it if its own static imports name echarts, or
  // if one of the modules it statically imports does - so a local re-export,
  // `src/charts.js` that imports the core and a handful of registrations and
  // hands the namespace on, does not hide the library from this count.  Reading
  // only the specifier read a tree whose twelve panels said
  // `import echarts from '../../charts'` as one module in the entry's reach,
  // with every panel still statically imported and not a byte moved.
  //
  // A fixpoint over the same graph the closure above walks, because a chain of
  // those wrappers is as long as a tree cares to make it.
  const edges = new Map();
  for (const p of parsed) {
    edges.set(p.rel, staticSources(p.ast).map((n) => ({
      node: n,
      spec: n.source.value,
      to: resolveModule(p.rel, n.source.value, known),
    })));
  }
  const carries = new Set();
  for (const [rel, own] of edges) {
    if (own.some((e) => ECHARTS.test(e.spec))) carries.add(rel);
  }
  for (let grew = true; grew;) {
    grew = false;
    for (const [rel, own] of edges) {
      if (carries.has(rel)) continue;
      if (own.some((e) => e.to && carries.has(e.to))) { carries.add(rel); grew = true; }
    }
  }
  // One site per module: at its first import of the library when it has one -
  // a file with seven registration lines in it is one module in the entry's
  // reach, not seven - and at the import that carries the library in when it
  // has none of its own.
  for (const p of parsed) {
    if (!scope.has(p.rel) || !carries.has(p.rel)) continue;
    const own = edges.get(p.rel);
    const site = own.find((e) => ECHARTS.test(e.spec))
      || own.find((e) => e.to && carries.has(e.to));
    if (site) hit('entry-echarts', p.rel, site.node, site.spec);
  }
}

// --------------------------------------- redirects written through a helper
//
// `<Navigate>` is counted above wherever it is written, which reads a tree that
// wrote one small component around it - to carry the query string across the
// way react-router 3's <Redirect> did - and used that component for every
// redirect as a tree with one redirect.  Every blind run of step 3 wrote exactly
// that, under four different names.  The same reading is given here that a
// helper around React's `lazy` gets: a function whose result IS a `<Navigate>`
// (or one of these helpers) is a redirect helper, and each place the tree uses
// one is a redirect.  Two shapes of helper:
//   - a component that returns the element, possibly behind a condition;
//   - a small factory whose returned route object or element holds that one
//     element and at most one other (the `<Route>` or object around it).
// A function that returns a whole route table is neither: it holds many
// elements, and its single `<Navigate>` children are counted where they are.
// Helpers are followed one import hop, as the lazy helpers are.  The count
// with helpers resolved replaces the direct count only when it is larger, so no
// tree reads lower than it did.  (wave R 2026-10-20, trajectory review R-1)
{
  const navTag = (el) => (el && el.type === 'JSXElement' && el.openingElement
    && el.openingElement.name && el.openingElement.name.type === 'JSXIdentifier'
    ? el.openingElement.name.name : null);
  // What a function hands back: its expression body, or the arguments of its own
  // return statements (not those of functions nested inside it), with
  // conditional, logical and sequence expressions opened up.
  const returnedLeaves = (fn) => {
    const roots = [];
    if (fn.body && fn.body.type !== 'BlockStatement') roots.push(fn.body);
    else {
      const nested = new Set();
      walk(fn.body, (n) => {
        if (n !== fn && (FN_EXPRS.has(n.type) || n.type === 'FunctionDeclaration')) nested.add(n);
      });
      walkExcept(fn.body, (n) => {
        if (n.type === 'ReturnStatement' && n.argument) roots.push(n.argument);
      }, nested);
    }
    const leaves = [];
    const open = (e) => {
      if (!e) return;
      if (e.type === 'ConditionalExpression') { open(e.consequent); open(e.alternate); }
      else if (e.type === 'LogicalExpression') { open(e.left); open(e.right); }
      else if (e.type === 'SequenceExpression') open(e.expressions[e.expressions.length - 1]);
      else if (e.type === 'ParenthesizedExpression') open(e.expression);
      else leaves.push(e);
    };
    roots.forEach(open);
    return leaves;
  };
  const isRedirectHelper = (fn, names) => {
    const isNav = (tag) => tag === 'Navigate' || names.has(tag);
    for (const leaf of returnedLeaves(fn)) {
      if (isNav(navTag(leaf))) return true;
      if (leaf.type === 'ObjectExpression' || leaf.type === 'ArrayExpression'
          || leaf.type === 'JSXElement') {
        const els = [];
        let fnInside = false;
        walk(leaf, (n) => {
          if (n.type === 'JSXElement') els.push(n);
          if (FN_EXPRS.has(n.type) || n.type === 'FunctionDeclaration') fnInside = true;
        });
        if (!fnInside && els.length <= 2 && els.filter((e) => isNav(navTag(e))).length === 1) {
          return true;
        }
      }
    }
    return false;
  };
  // The functions of a file that have a name to be used by, and the anonymous
  // default export, which has one only in the files that import it.
  const namedFunctions = (ast) => {
    const out = [];
    walk(ast.program, (n) => {
      if (n.type === 'FunctionDeclaration' && n.id) out.push({ name: n.id.name, fn: n });
      if (n.type === 'VariableDeclarator' && n.id && n.id.type === 'Identifier'
          && n.init && FN_EXPRS.has(n.init.type)) out.push({ name: n.id.name, fn: n.init });
    });
    for (const n of ast.program.body) {
      if (n.type === 'ExportDefaultDeclaration' && n.declaration
          && (FN_EXPRS.has(n.declaration.type)
              || (n.declaration.type === 'FunctionDeclaration' && !n.declaration.id))) {
        out.push({ name: null, fn: n.declaration });
      }
    }
    return out;
  };
  const exportedAs = (ast, local) => {
    const out = new Map();
    for (const n of ast.program.body) {
      if (n.type === 'ExportNamedDeclaration') {
        const d = n.declaration;
        if (d && d.type === 'FunctionDeclaration' && d.id && local.has(d.id.name)) out.set(d.id.name, d.id.name);
        if (d && d.type === 'VariableDeclaration') {
          for (const v of d.declarations) {
            if (v.id && v.id.type === 'Identifier' && local.has(v.id.name)) out.set(v.id.name, v.id.name);
          }
        }
        if (!d && !n.source) {
          for (const sp of n.specifiers || []) {
            const l = sp.local && sp.local.name;
            if (l && local.has(l)) out.set((sp.exported && (sp.exported.name || sp.exported.value)) || l, l);
          }
        }
      } else if (n.type === 'ExportDefaultDeclaration') {
        const d = n.declaration;
        if (d && d.type === 'Identifier' && local.has(d.name)) out.set('default', d.name);
        else if (d && d.type === 'FunctionDeclaration' && d.id && local.has(d.id.name)) out.set('default', d.id.name);
      }
    }
    return out;
  };
  // Per file: local name -> helper, to a fixpoint (a helper around a helper is
  // one), with the helpers each file imports from a module that exports one.
  const helperFns = new Map();   // rel -> Set of function nodes that are helpers
  const helperNames = new Map(); // rel -> Set of names usable in that file
  const exportedHelpers = new Map(); // rel -> Set of exported names
  const anonDefault = new Set(); // rels whose anonymous default export is a helper
  for (let round = 0; round < 2; round++) {
    for (const p of parsed) {
      const names = new Set(helperNames.get(p.rel) || []);
      const fns = new Set(helperFns.get(p.rel) || []);
      const all = namedFunctions(p.ast);
      for (let grew = true; grew;) {
        grew = false;
        for (const { name, fn } of all) {
          if (fns.has(fn) || !isRedirectHelper(fn, names)) continue;
          fns.add(fn);
          if (name) names.add(name); else anonDefault.add(p.rel);
          grew = true;
        }
      }
      helperFns.set(p.rel, fns);
      helperNames.set(p.rel, names);
      const ex = exportedAs(p.ast, names);
      exportedHelpers.set(p.rel, new Set([...ex.keys(), ...(anonDefault.has(p.rel) ? ['default'] : [])]));
    }
    // One import hop, then a second pass so a file's own helpers around an
    // imported one are found too.
    for (const p of parsed) {
      const names = helperNames.get(p.rel);
      for (const [local, b] of p.imports) {
        const r = resolveModule(p.rel, b.source, known);
        const ex = r && exportedHelpers.get(r);
        if (ex && ex.has(b.imported)) names.add(local);
      }
    }
  }
  const viaHelpers = [];
  for (const p of parsed) {
    const names = helperNames.get(p.rel);
    walkExcept(p.ast.program, (n) => {
      if (n.type === 'JSXOpeningElement' && n.name && n.name.type === 'JSXIdentifier') {
        const tag = n.name.name;
        if (tag === 'Navigate') viaHelpers.push({ file: p.rel, n, what: tag });
        else if (names.has(tag)) viaHelpers.push({ file: p.rel, n, what: `${tag} (renders Navigate)` });
      }
      if (n.type === 'CallExpression') {
        if (n.callee.type === 'Identifier' && names.has(n.callee.name)) {
          viaHelpers.push({ file: p.rel, n, what: `${n.callee.name}() (renders Navigate)` });
        } else if (moduleExport(n.callee, p.imports, ROUTER_MODULE) === 'redirect') {
          viaHelpers.push({ file: p.rel, n, what: 'redirect' });
        }
      }
    }, helperFns.get(p.rel));
  }
  if (viaHelpers.length > findings['router-navigate'].length) {
    findings['router-navigate'] = [];
    for (const s of viaHelpers) hit('router-navigate', s.file, s.n, s.what);
  }
}

// Sites are reported with the file they were found in, relative to the root, so
// a consumer can restrict any count to any set of files without this scan having
// to know which set that will be.  run-gates.mjs does exactly that with the file
// set the provenance probes measured.
const out = {
  root: ROOT,
  files: fileCount,
  parseErrors,
  gates: Object.fromEntries(
    Object.entries(findings).map(([id, sites]) => [id, { count: sites.length, sites }]),
  ),
};

if (JSON_OUT) fs.writeFileSync(JSON_OUT, JSON.stringify(out, null, 1));

const ids = Object.keys(out.gates).sort();
console.log(`files parsed: ${fileCount}, parse errors: ${parseErrors.length}`);
for (const id of ids) console.log(`  ${id}: ${out.gates[id].count}`);
for (const e of parseErrors) console.log(`  PARSE ERROR ${e.file}: ${e.message}`);
