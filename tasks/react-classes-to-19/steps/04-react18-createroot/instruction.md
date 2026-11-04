# Step 4 — React 18, a root, and StrictMode

`/app` now holds the function-component application on Redux Toolkit and
react-router 6 that the previous three steps produced. Three migrations in and
it still boots the way it booted in 2017: a single `ReactDOM.render` call in
`src/components/app.js`, against react 16.14 and react-dom 16.14.

Put it on React 18, render it through a root, and turn StrictMode on — and then
fix what StrictMode finds.

**The environment and how the tree runs.** Node 22 is installed. `/app` is the whole
repository: the application under `src/`, the Sass under `src/styles/`, and the
Express mock API under `server/demo1`, which serves both the data routes and —
from its own `public/` directory — the built site. `npm run build` writes the
site to `/app/dist`; putting that output in the server's static root and
starting the server is how the application runs, as `/app/README.md` describes.
The application is a hash-routed single page and `src/constants/actionApi.js`
holds the API base and the path of every request it makes.

---

## 1. The root

**React 18 is the React this repository runs on.** `react` and `react-dom` are
both installed at major 18 when the install finishes. Not 16 with an 18 beside
it, and not 19: major 18 is the one where the work below is work you do rather
than work the package does to you. The retired render call the next paragraph
asks you to stop using is still there to be stopped rather than already removed,
and the legacy context §3's icon package reads still resolves — so the tree
runs, on the version it arrives on, while you change it. A later major is a
different migration from the one this step is.

**The application mounts through a root.** `ReactDOM.render` is not what starts
this application afterwards. The root API lives in react-dom's own client entry
point, the root is created once for the container the page provides, and the
application is rendered into it. `ReactDOM.render`, `ReactDOM.hydrate` and
`unstable_renderSubtreeIntoContainer` are called nowhere in the repository.

---

## 2. StrictMode, and what it finds

**StrictMode is what the root renders.** The element handed to the root's
`render` is `<StrictMode>`, and the application is inside it — not a
`<StrictMode>` somewhere down in the tree, which double-invokes only what is
beneath it and leaves the rest of the application mounted the ordinary way. It
costs the production build nothing — the checks it adds are development-only
code, compiled out of what `npm run build` emits — and it is what makes a
development run report the things React 19 will stop running at all.

**And what it reports is zero.** This is the part of the step that is work
rather than a rewrite, so it is worth being exact about what that zero covers:

- This repository is built a second time with `NODE_ENV=development` in the
  environment, using its own `npm run build`. Vite takes `NODE_ENV` from the
  environment when it is already set, so that is the project's build command
  producing the project's build in the other configuration. That build has to
  work, and it has to be a development build of React.
- The result is served, and a fixed list of addresses is visited in a fresh
  browser context each: one per layout in the application, every one of the
  compare-by-seller screens, and an address no route matches.
- At each of them the development build has to render the screen that address
  is for — the same screen the production build renders there — and ask the
  mock API for its data. Not the empty page around it, not a placeholder
  standing in for it, and not the no-such-route page. A page that renders
  nothing has nothing to report, and neither has a screen that is not on the
  page: a console that is quiet because the application never started, or
  because the components being visited were not the ones that mounted, is not a
  quiet console.
- Every `console` message of type `error` and every uncaught exception is
  counted. The count has to be zero.

**Why what it reports is a real defect rather than a StrictMode artefact.**
Under a root, StrictMode mounts a component, runs its effects, runs their
cleanups, and mounts it again. An effect whose cleanup cannot survive being
called is therefore called exactly the way an unmount would call it, on the
first render rather than at some later navigation — so what the development run
reports is not something StrictMode did to a correct application, it is
something the application always did, reached sooner. Fix the effects, not the
symptom: an effect that runs twice must leave the same state behind as an effect
that runs once, and a cleanup must be able to run against a mount that took the
other branch.

Everything the development run reports is in scope.

---

## 3. The icons

**The icon package is a major that does not read legacy context.**
`react-icons` 2 reaches each icon through its own module — `react-icons/lib/fa/…`
— and every one of those renders through `react-icon-base`, which is the last
package in this tree that reads React's legacy context. Legacy context still
works in React 18 and does not work in React 19. So:

- No module imports an icon through `react-icons/lib/…` afterwards.
- `react-icon-base` is not in the installed tree afterwards.
- The icon package that is installed is major 5 or later, whose icons are
  ordinary function components.

**The markup an icon renders does not change.** Version 5 imports from a pack
subpath rather than a per-icon path, and renders the same SVG — including the
`className` the surrounding code passes it, which several of these icons are
positioned by. Two of the icons this repository uses were renamed between the
two majors; a rename that lands on the wrong icon is a different picture on the
page, and the markup each icon renders today is part of what this step does not
change.

**`react-transition-group` is declared in `package.json` and its React peer
range stops at 16.** Whether the steps before this one left that declaration
standing on its own or left a module still importing it, `npm ci` has to
succeed on this repository with no extra flags: the manifest and the lockfile
have to be a set that `npm ci`, run as it stands, will install against React 18
— and §4 holds for whatever imports it, if anything still does.

---

## 4. Behavior does not change

The application in front of you is the specification. Everything it renders and
requests today it renders and requests after this step.

**What must be identical.** For every route, every interaction and every
navigation: the rendered DOM, the charts drawn (their series, geometry, colours
and labels), the document title, the URL hash, and the set of API paths the
application requests.

**The charts are drawn by echarts 3.8.5.** That is the last 3.x release, and the
one the `^3.4.0` range this project started from installs. It is the release
this application's charts are drawn with, and the release they are still drawn
with after this step: a different echarts release lays out and paints the same
option differently.

**The set, not the sequence.** *Which* API paths a screen asks for is compared;
how many times is not.

**This step is about how the application is mounted and what StrictMode finds
when it is.** The router, the data layer and the component model are where the
steps before this one left them: no route grows a `loader`, an `action` or a
`shouldRevalidate`, no module goes to the server on its own account instead of
through the data layer's own hooks, nothing goes back to a class, and the route
table stays the data router's. Those hooks are also still what the screens
call: the data layer came into this tree with dozens of call sites across the
application, and most of those call sites are still there at the end of this
step — a screen that stops using its hook has left the data layer, not moved
within it. Concurrent rendering — `useTransition`, `useDeferredValue`,
`Suspense` around data — is not this step's subject: the version this step
installs makes those available, and making the application use them is not what
this step is for.

**Code splitting survives.** The route components are still fetched when they
are first needed and the built page still reaches at least thirty JavaScript
files. This step rewrites the import list of every file that reaches for an
icon — four dozen of them — and moves the entry point; neither is a reason for
the route split to end.

**The API is fixed.** The mock API under `/app/server` — its routes, its data,
its response shapes and its timing — is part of the interface. Do not change it,
and do not change the paths in `src/constants/actionApi.js` that address it.

**Bugs are behavior.** Where the current application does something odd — a
chart that re-measures itself a beat late, a request fired twice on one
navigation, a selection that survives a navigation it arguably should not, a
sidebar entry that links nowhere — it keeps doing it, and §2 is the exception
that proves it: a cleanup that throws is fixed because StrictMode runs it, and
fixing it changes nothing the application renders.

---

## 5. Scope

Every JavaScript file in the repository is in scope, whether or not anything
imports it. A module nothing imports is still a module in the repository, and an
import of the retired API in one is still an import of the retired API in the
repository.

Two acts clear such a file: converting it, or deleting it. A file the running
application genuinely does not reach may be removed rather than converted;
anything reachable is converted, not removed.

Every JavaScript file still in the repository at the end parses under a current
JavaScript parser.

---

## 6. Build and toolchain

**The build is not replaced.** Vite is the declared build tool, `npm run build`
writes a static site to `/app/dist` including `index.html`, and what it writes
is compiled from this repository's own source by that build tool rather than
copied from a build made earlier. This is a standing requirement of the tree,
not a detail of how it arrived here.

**Lockfile.** `package-lock.json` is part of this repository and must agree with
`package.json`. The install is performed from the lockfile: it fails outright,
naming the file, both when the two disagree and when the file is not there at
all. This step changes the dependency set, so regenerate the lockfile with the
package manager, and never delete it.

---

## 7. Meta requirements

- The implementation budget for this step is 7,200 seconds. Check
  `bash /app/timer.sh`; the configured timeout is authoritative.
- Work in `/app`. Preserve the repository's source, assets and documents except
  where this instruction requires otherwise.
- Do not cheat by using online solutions or hints specific to this task. Work
  only from this workspace and its documentation; do not search the system
  for other implementations or answers.
- Do not write a prose answer, a migration report or an alternate entry point.
  The migrated repository is the deliverable.
