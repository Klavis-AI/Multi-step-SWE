# Step 1 — React 16.8 with hooks, and a Vite build

`/app` holds a 2017 React 15 data-visualisation dashboard: class components,
string refs, `findDOMNode`, `React.PropTypes`, `connect()`-wrapped containers,
hand-rolled polling, and a webpack 2 / babel 6 / browserify / node-sass
toolchain. It renders echarts charts over an Express mock API that ships with
the repository.

Move it to React 16.8 or later — function components with hooks throughout —
and replace the build with Vite, without changing a single thing the
application does.

**The environment and how the tree runs.** Node 22 is installed; the retired
build's own interpreter is not, and neither is the native toolchain `node-sass`
4 needs, so the existing build cannot be revived. `/app` is the whole repository: the
React application under `src/`, its Sass under `src/styles/`, the hand-vendored
runtime assets under `lib/`, and the Express mock API under `server/demo1`,
which serves both the data routes and — from its own `public/` directory — the
built site. `/app/README.md` describes the arrangement the project already used:
build the site, place the output in the server's static root, start the server
with `npm start` in `server/demo1`, and open `http://localhost:3000`. The
application is a hash-routed single page; `src/constants/actionApi.js` holds the
API base it calls.

---

## 1. React and the component layer

**React version.** `react` and `react-dom` at 16.8 or later within major 16,
both at the same major. 16.8 is the first release with hooks; nothing earlier
satisfies the rest of this section.

**Nothing is a class.** No component is defined as a class anywhere in the
repository: no `class X extends React.Component`, no `extends
React.PureComponent`, no `React.createClass` or `createReactClass`. The
component layer is function components.

**The class facilities are expressed as hooks.** Component state, effects,
subscriptions, timers and element references are hooks — across the component
layer, not in a handful of files.

**No string refs.** No element identifies itself to its owner by a string name:
`ref="chartBox"` on a JSX element is gone, everywhere.

**No `findDOMNode`.** No code reaches a DOM node by asking the renderer to find
it. Elements that must be measured, resized or handed to echarts are referenced
directly.

**No pre-async lifecycles.** `componentWillMount`, `componentWillReceiveProps`
and `componentWillUpdate` appear nowhere, under either the plain or the
`UNSAFE_`-prefixed spelling.

**No `connect()` wrapper.** No component receives store state or `dispatch` by
being wrapped in a higher-order component. A component that needs store state
reads it where it renders; a component that dispatches gets `dispatch` the same
way. This changes how a component reaches the store, not where the state
lives.

**Prop validation survives, off the standalone package.** `React.PropTypes` is
gone — React 16 removed it. Where a component still receives props worth
validating, the validation goes with it, declared through the standalone
`prop-types` package. Do not drop prop validation as a side effect of dropping
the class that held it.

**Memoization.** A class computed some values once — in a constructor or a
lifecycle — and held them across renders; a function component's body runs
again on every render, so the same value is rebuilt every time unless it is
memoized. Where that is what the class did, the function component memoizes:
the derived value, and the callback passed down as a prop, are `useMemo` and
`useCallback` rather than a fresh object each render.

**No React-15 animation addon.** `react-addons-css-transition-group` is imported
nowhere. Whatever it was doing for the carousel still happens.

**Hooks are called by the rules.** Every hook call is unconditional and at the
top level of the function that makes it — never inside an `if`, a loop, a
`try`, a nested function or a callback — so that every render of a component
makes the same hook calls in the same order. A function that calls hooks is
either a component or a custom hook, and is named so: components begin with a
capital letter, custom hooks begin with `use`. The repository's own convention
for its class names — `_compare_out`, `_sellersRoute` — does not satisfy that.

This has to hold in the code rather than in what a tool reports about it: where
a hook call would break the rule, restructure the component so that it does
not, rather than annotating the code to stop the complaint.

---

## 2. Scope

Every JavaScript file in the repository is in scope, whether or not anything
imports it. A file that nothing renders and nothing imports is still a file in
the repository, and a class declared in one is still a class in the repository.

Two acts clear such a file: converting it, or deleting it. A file that the
running application genuinely does not reach may be removed rather than
converted; anything reachable is converted, not removed.

Every JavaScript file still in the repository at the end parses under a current
JavaScript parser.

---

## 3. Build and toolchain

**Vite.** Vite is the project's declared build tool.

**`npm run build` produces the site.** The command writes a static build to
`/app/dist`, including `/app/dist/index.html`. Not `build/`, not `out/`.

**The site is built from this source.** What `npm run build` writes to `dist/`
is compiled from the repository's own source by the declared build tool — not a
build produced earlier and put back by the build script, and not a copy kept
aside.

**More than one chunk.** The current build splits its JavaScript — an
application chunk, a vendor chunk, and a chunk per lazily-loaded route group.
The new build still emits more than one JavaScript chunk; collapsing the site
into a single bundle is a regression.

**The build output is self-contained.** Every local file the built page loads
is inside `dist/`. The current arrangement copies `lib/` and `favicon.ico` into
place by hand after the build (`npm run Copy`); after this step the build
itself accounts for them.

**The old toolchain is not installed.** `webpack`, `babel-core`, `node-sass`,
`browserify`, `mocha` and `karma` are absent from the installed dependency
tree — not merely dropped from `package.json`, but present nowhere in it,
including as some other package's transitive dependency. Nothing may fall back
to the retired build.

**Lockfile.** If `package-lock.json` is present it must agree with
`package.json`: the install is performed from the lockfile (`npm ci`; `npm
install` when there is no lockfile), run with no extra flags, and it fails
outright on a mismatch rather than falling back.

---

## 4. Behavior does not change

The application in front of you is the specification. Everything it renders and
requests today it renders and requests after the migration.

**How it is served.** The built site is served as static files from the mock API
server's own document root, so the page and the API share an origin: the
application's hardcoded API base `http://localhost:3000/`
(`src/constants/actionApi.js`) resolves to the server that served the page.
Asset references must work from that root. `/app/README.md` describes this
arrangement as the project already used it.

**The API is fixed.** The mock API under `/app/server` — its routes, its data,
its response shapes and its timing — is part of the interface. Do not change it.

**What must be identical.** For every route, every interaction and every
navigation: the rendered DOM, the charts drawn (their series, geometry, colours
and labels), the document title, the URL hash, and the set of API paths the
application requests.

**The charts are drawn by echarts 3.8.5.** `package.json` asks for `echarts`
`^3.4.0`, and what that range installs is 3.8.5, the last 3.x release. That is
the release this application's charts are drawn with, and the release they are
still drawn with after the migration: a different echarts release lays out and
paints the same option differently. The site already sitting in
`server/demo1/public/` is a build made in 2017 against echarts 3.4.0. Its pages,
requests, titles and addresses are this application's, but its charts are drawn
by that older release, not by 3.8.5.

**Bugs are behavior.** Where the current application does something odd — a
chart that re-measures itself a beat late, a handler that fires twice, a
selection that survives a navigation it arguably should not — it keeps doing
it. This step changes how the code is written, not what it does.

---

## 5. Meta requirements

- The implementation budget for this step is 7,200 seconds. Check
  `bash /app/timer.sh`; the configured timeout is authoritative.
- Work in `/app`. Preserve the repository's source, assets and documents except
  where this instruction requires otherwise.
- Do not cheat by using online solutions or hints specific to this task. Work
  only from this workspace and its documentation; do not search the system
  for other implementations or answers.
- Do not write a prose answer, a migration report or an alternate entry point.
  The migrated repository is the deliverable.
