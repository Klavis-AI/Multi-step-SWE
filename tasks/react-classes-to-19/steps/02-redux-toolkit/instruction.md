# Step 2 — Redux Toolkit, and a data layer that owns the requests

`/app` now holds the React 16.8 function-component application the previous
step produced, built by Vite. Its state layer is still the one written in 2017:
a store assembled by hand from `createStore` over a `combineReducers` map with
thunk middleware installed; four reducers that `switch` on an action-type
constant and keep their state in Immutable collections; forty-seven action
creators that each return a function taking `dispatch`, call `fetch`
themselves, and dispatch an action object they build by hand; and four
components that refresh their data by putting a `setInterval` around a
dispatch.

Replace all of it with Redux Toolkit and its query layer, without changing a
single thing the application does.

**The environment and how the tree runs.** Node 22 is installed. `/app` is the whole
repository: the application under `src/`, the Sass under `src/styles/`, and the
Express mock API under `server/demo1`, which serves both the data routes and —
from its own `public/` directory — the built site. `npm run build` writes the
site to `/app/dist`; putting that output in the server's static root and
starting the server is how the application runs, as `/app/README.md` describes.
The application is a hash-routed single page and `src/constants/actionApi.js`
holds the API base and the path of every request it makes.

---

## 1. The store

**Redux Toolkit is the project's declared state library.** `@reduxjs/toolkit`
is a dependency of `/app/package.json`. Any 2.x release is fine; the step is
about the constructs, not a version.

**The store is not assembled by hand.** `createStore`, `combineReducers`,
`applyMiddleware` and `bindActionCreators` are called nowhere in the
repository, whichever package they were imported from. The store is configured
by the toolkit, from a map of reducers, with its own default middleware.

**No reducer switches on an action type.** No reducer decides what to do by
`switch`ing on the action's type. Reducers are defined by the toolkit's own
factories, case by case.

**No action creator returns a function.** The thunk pattern — an action creator
returning a function that the middleware then calls with the store's `dispatch`
— is gone, and so is the hand-installed thunk middleware that made it work:
`redux-thunk` is imported nowhere, and nothing in the repository is still
written to be handed the store's `dispatch` and dispatch from there. Asynchrony
itself is not what this forbids; the toolkit's own async action factory takes
its argument first and the store's API second, and is not one of these.

**No action object is built by hand.** Nothing dispatches an action object it
assembled itself — whether the object is written at the call or built a line
above and dispatched by name. Actions come from the factories that define the
reducers that handle them.

**State in the store is plain data.** Nothing in the repository imports
`immutable` or `redux-immutable`. Slices hold plain objects and arrays, and
each slice owns its own updates.

**The state tree's shape is yours to choose.** The four top-level keys the
store uses today are the present spelling, not a requirement. What must not
change is what the application renders from them.

---

## 2. The data layer

**One place owns the requests.** The repository has a data layer built with the
toolkit's query layer: `createApi`, given the toolkit's own `fetchBaseQuery`
with the application's base URL. Every request the application makes is an
endpoint defined there, and what the application asks the server for does not
change — the same paths, for the same screens, as today. The data layer's
reducer and its middleware are part of the store.

**No module makes its own HTTP request.** Nothing outside the data layer goes
to the server on its own account — not through the browser's `fetch`, bare or
through `window`; not through an `XMLHttpRequest`; not through a client library
brought in for the purpose. Requests are made by the data layer, not by the
code that wants the data.

**The old request layer is removed, not left beside the new one.** Migrating
the data layer means the code that fetched before is gone from the repository.
Action creators that fetch, and modules that go to the server for themselves,
are still this application's data layer for as long as they are in the tree —
whether or not anything calls them any more. A tree that stands the query layer
up next to them has two data layers and has migrated neither.

**Components read their data through the generated hooks.** A component that
needs data from the server gets it by calling the data layer's own hook for
that endpoint. This is the whole application, not a pilot: every request this
application makes today is read through the data layer's own hook for it, at
every place that reads it. A tree that routes a handful of screens through the
data layer and leaves the rest as they are has not done this step.

**No timer fetches for itself.** A `setInterval` or `setTimeout` callback makes
no request itself, in any of the spellings above, and it dispatches nothing at
all — not an action object it built, and not the data layer's own request
action either. Nor does it re-run a request the screen it lives on already
reads: re-running a subscription on a timer is hand-rolled polling with a new
spelling, and that case is the refresh interval below. A timer whose job is to
ask for something *else* keeps that job, and asks for it the way every other
caller does — by triggering that endpoint through the data layer, which owns
the request and decides how it is made.

**Where a screen refreshes the request it already reads, the refresh interval
belongs to that query.** One screen in this application asks for a set of data
when it mounts and then asks the same endpoint for the same data again, on a
fixed interval, for as long as it is on screen. That is a query that refreshes
itself, and it is declared as an interval on the query rather than kept as a
timer in the component. Not every timer in the application is this: a timer
that asks for something *other* than what its screen already reads is starting
a second request, not repeating the first, and stays a timer — see §4.

---

## 3. Scope

Every JavaScript file in the repository is in scope, whether or not anything
imports it. A module nothing imports is still a module in the repository, and a
thunk declared in one is still a thunk in the repository.

Two acts clear such a file: converting it, or deleting it. A file the running
application genuinely does not reach may be removed rather than converted;
anything reachable is converted, not removed.

Every JavaScript file still in the repository at the end parses under a current
JavaScript parser.

---

## 4. Behavior does not change

The application in front of you is the specification. Everything it renders and
requests today it renders and requests after the migration.

**What must be identical.** For every route, every interaction and every
navigation: the rendered DOM, the charts drawn (their series, geometry, colours
and labels), the document title, the URL hash, and the set of API paths the
application requests.

**The charts are drawn by echarts 3.8.5.** That is the last 3.x release, and the
one the `^3.4.0` range this project started from installs. It is the release
this application's charts are drawn with, and the release they are still drawn
with after this step: a different echarts release lays out and paints the same
option differently.

**The set, not the sequence.** A data layer that answers a repeated request
from its cache where the present code goes to the server again is preserving
behavior, not changing it. How many times a path is requested is not part of
the behavior; *which* paths are requested is.

Five things a straight port changes without meaning to. All five are
consequences of the paragraph above, and all five are places this application
is more particular than it looks:

- **Arriving at a screen a second time asks the server again.** Every screen
  requests its data when it mounts, so navigating away and back re-requested
  it and redrew the chart. A cache that serves the second visit without ever
  delivering a response leaves that chart on its loading mask.
- **Two charts that request the same path are still two charts.** Several
  entries in `src/constants/actionApi.js` are the same path for different
  charts, and some of those charts are mounted on the same page. Each one asked
  for its own copy and wrote its own piece of state. A data layer that treats
  them as one leaves the other chart empty.
- **A timer that refreshes something other than what its screen reads goes on
  refreshing that other thing.** Three of the four fetching timers ask for a
  different endpoint than the one their component subscribes to. Turning those
  into a refresh interval on the component's own query changes both what is
  requested and what is drawn.
- **Components do not start rendering more often than they do now.** Two
  components run an effect with no dependency list, so it runs after every
  commit. A hook that publishes loading and fetching transitions to a component
  makes that component commit where it did not before, and those effects then
  run more often than they did. Where a component only needs a hook to make the
  request, it does not have to select anything out of it.
- **A response that repeats the one already held still changes the store.**
  This is the sharpest edge in the step, and it is a property of the
  collections being replaced. Writing a key deep in an Immutable map keeps the
  old map only when the new value is the *same object* as the one already
  there; equal contents are not enough. Every reducer case builds its value
  fresh, so every response — including a second, identical one — produced a new
  state, a re-render and a redraw. That is what a screen relies on when it is
  arrived at a second time. A store that instead compares the new value with
  the old one field by field, or a cache that hands a reducer back the very
  object it is already holding, stops the second visit from drawing anything.
  The few places that store a bare string or number are the exception and were
  always the exception: two equal primitives *are* the same value, and those
  writes did collapse.

**Nothing a request returns is modified in place.** One reducer reads its
payload back to front by reversing the array it was handed. With a data layer
that caches, the array a reducer receives is the cached response itself, and
reversing it reorders it for every later reader of that response.

**The API is fixed.** The mock API under `/app/server` — its routes, its data,
its response shapes and its timing — is part of the interface. Do not change
it, and do not change the paths in `src/constants/actionApi.js` that address
it.

**Bugs are behavior.** Where the current application does something odd — a
chart that re-measures itself a beat late, a request fired twice on one
navigation, a selection that survives a navigation it arguably should not — it
keeps doing it. This step changes how the code is written, not what it does.

---

## 5. Build and toolchain

**The build is not replaced.** Vite is the declared build tool, `npm run build`
writes a static site to `/app/dist` including `index.html`, and what it writes
is compiled from this repository's own source by that build tool rather than
copied from a build made earlier. This is a standing requirement of the tree,
not a detail of how it arrived here.

**Lockfile.** `package-lock.json` is part of this repository and must agree with
`package.json`. The install is performed from the lockfile: it fails outright
rather than falling back, both when the two disagree and when the file is not
there at all. A step that adds or drops a dependency therefore regenerates the
lockfile rather than hand-editing it, and never deletes it.

**One package must be gone from the installed tree.** `redux-immutable` is not
present anywhere under `node_modules` when the install finishes — not as a
direct dependency and not as anything else's.

---

## 6. Meta requirements

- The implementation budget for this step is 7,200 seconds. Check
  `bash /app/timer.sh`; the configured timeout is authoritative.
- Work in `/app`. Preserve the repository's source, assets and documents except
  where this instruction requires otherwise.
- Do not cheat by using online solutions or hints specific to this task. Work
  only from this workspace and its documentation; do not search the system
  for other implementations or answers.
- Do not write a prose answer, a migration report or an alternate entry point.
  The migrated repository is the deliverable.
