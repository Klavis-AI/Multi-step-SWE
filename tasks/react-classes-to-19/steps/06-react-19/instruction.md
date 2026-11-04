# Step 6 — React 19

`/app` now holds the application on React 18: function components throughout,
the store bindings on react-redux 7, the router on the data router, the root
created through `createRoot` with StrictMode on, and the chart panels loaded on
demand behind `lazy` so that what a visitor waits for is a fraction of what it
used to be.

This step moves it to **React 19**, and finishes what that move starts. The
version bump is one line of `package.json`; the step is the rest of it.

**The environment and how the tree runs.** Node 22 is installed. `/app` is the whole
repository: the application under `src/`, the Sass under `src/styles/`, and the
Express mock API under `server/demo1`, which serves both the data routes and —
from its own `public/` directory — the built site. `npm run build` writes the
site to `/app/dist`; putting that output in the server's static root and
starting the server is how the application runs, as `/app/README.md` describes.
The application is a hash-routed single page and `src/constants/actionApi.js`
holds the API base and the path of every request it makes.

---

## 1. The runtime

**React and react-dom are on major 19.** Both of them: they are one release in
two packages, and a tree holding them apart is on neither version. Only one
major of each is installed when you are done — a second copy of React left
somewhere in the tree is not an upgrade, it is two Reacts.

---

## 2. The store bindings come with it

**react-redux 7 does not run on React 19, and it says so.** Its own declared
peer range ends at React 18. Install React 19 beside it and the install fails
outright — not a warning, a refusal — so this is not optional work you can
defer: nothing builds until it is done.

**So the bindings move to a release that supports React 19**, which means
**react-redux 9**. But not merely the major: the first releases of 9 still
declare a React ceiling of 18, and support for 19 arrives partway through the
line, at **9.2**. Read the peer range of the release you actually end up with
and check that React 19 is in it; anything below 9.2 is a copy whose own
manifest says it does not support the runtime you are installing. Two majors
separate that from where the tree is now, and each did something: version 8
rewrote how the bindings subscribe to the store, and version 9 dropped every
React below 18. Only one major of react-redux is installed when you are done,
the same as for React: a copy of 7 still resolved somewhere in the tree is the
conflict, not the fix.

**Support, not suppression.** There is a way to make the install succeed without
moving the bindings at all, by telling the package manager to disregard what
react-redux says it supports. The application then builds, and it runs — as an
application running a package outside the support that package's own author
declares, which is the opposite of an upgrade. What must be true at the end is
that the react-redux **that is installed** is a release that supports React 19.

**Your call sites are probably fine.** Read them rather than assuming either
way: `<Provider>` and `useSelector` have the same signatures across this jump.
What changes underneath is when a store update reaches a render, and §6 is what
decides whether that mattered.

---

## 3. The validation layer React 19 stopped reading

**React 19 removed `propTypes` from function components.** The declarations are
no longer checked, no longer warned about, and no longer read at all. Components
in this repository still carry one, and after this step they are text that
claims the props are validated while nothing validates them.

**So that layer is gone, all three ways it exists in this tree:**

- no component declares a `propTypes` object;
- nothing imports the `prop-types` package;
- and `prop-types` is not a dependency of this repository and is not installed.

**Those are three statements because a partial removal is worse than none.**
Delete the assignment and keep the import and the package is still downloaded by
every visitor, doing nothing. Delete the import and keep the object it was built
from and the module throws before it exports anything — `PropTypes.array` is
read the moment that file loads — so the component does not render and §6 fails
on every screen it is on. Take the whole layer out, and take the dependency out
with it.

**Nothing here asks you to put something else in its place.** This application
validated these props because the framework used to do it for free; it does not
any more, so it stops. A validation library of your own is not part of this
step.

---

## 4. What the upgrade must not cost

**The entry cost is still under budget.** Open the site at an address the
application has no route for, and wait for the application's own no-such-route
screen to appear: the JavaScript the browser had to fetch to get there — all of
it, however it was reached — is under **140 KB gzipped** (140,000 bytes, each file gzipped at maximum compression). That screen is also
where the fetching stops: with it on the page the application is not still
pulling the rest of itself down behind it.

This number is not where it was. React 19's runtime is larger than React 18's,
and this upgrade spends roughly 22 KB gzipped of a budget the tree is currently
about 30 KB inside. It fits, and it fits with room to spare, but not with enough
room to also let something else back into the entry. **The budget does not
move.** A new major is not a licence to hand back what the previous step earned,
and the way to stay inside it is to not undo that step: the chart panels stay
out of the entry chunk and keep arriving on their own. React 19 also adds
resource-hint APIs — `preinit`, `preload` and their relatives — that can pull
the chart chunks forward; used before that first screen they add to exactly the
number this paragraph is about, and used after it they mean the page has not
stopped fetching. Neither is an improvement here.

**And the console is still silent.** The tree is built a second time with
`NODE_ENV=development`, served, and visited at one address per layout plus every
compare-by-seller screen plus an address no route matches; every `console` error
and every uncaught exception across those addresses is counted, and the count is
**zero**. It is zero on the tree in front of you, under React 18. React 19's
development build does not run the same checks React 18's did, and neither does
react-redux 9, so this is worth re-reading rather than assuming: find out what
the new majors say about this application, and if they say something, fix what
they are pointing at.

**Counting is not the same as silencing.** The count is taken from the code this
repository builds. A tree that reassigns, wraps, filters or stubs `console`, or
that catches and swallows what it is reporting, has changed what the count is a
count of and not what it is counting — and so has a tree whose build is
configured to remove those calls, or to replace them with something that does
nothing, before they can run.

---

## 5. What the tree already has, it keeps

This step is a runtime upgrade. Nothing else in the tree is its subject, and
nothing else in the tree is a way of paying for it.

- **The concurrent work stays.** The seller sidebar is still rendered from a
  deferred value the render reads, and the three range changes that hand a range
  down to the screen below are still made inside a transition that is actually
  started. The bindings' new subscription mechanism changes when a selector's
  result reaches a render; it is not a reason to take either of these out.
- **The split stays.** All twelve chart panels are still reached through React's
  own `lazy`, they still render under a `<Suspense>` boundary, the chart library
  is still out of the entry chunk, **and the built site still reaches at least
  forty-two JavaScript chunks**. React 19 changed how `<Suspense>` pre-renders
  siblings; that is a thing to understand, not a boundary to delete.
- **The root stays.** The application still mounts through `createRoot` and
  still renders StrictMode around what it mounts — every root it creates, not
  most of them. The render call `createRoot` replaced does not come back; React
  19 removed it from the package entirely.
- **The component model stays.** Nothing goes back to a class.
- **The data layer stays.** No route grows a `loader`, an `action` or a
  `shouldRevalidate`; no module goes to the server on its own account instead of
  through the data layer's own hooks; **the screens still call those hooks — at
  least thirty call sites among the modules the application actually reaches**;
  and the route table is still the data router's.
- **The icons stay.** No module goes back to reaching an icon through
  `react-icons/lib/…`, nor does the icon package go back below the major the
  earlier step left it on — it is still major 5 or later.
- **The build tool stays.** Vite is still the declared build tool. The entry
  budget above is a claim about what *this* build emits; a tree that answered it
  by changing bundlers answered a different question.

**One thing is out of scope rather than required.** Manual memoisation — the
`useCallback` and `memo` wrappers throughout this tree — is not this step's
subject. Nothing here asks you to add, remove or rework it.

---

## 6. Behavior does not change

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

**The API is fixed.** The mock API under `/app/server` — its routes, its data,
its response shapes and its timing — is part of the interface. Do not change it,
and do not change the paths in `src/constants/actionApi.js` that address it.

**Bugs are behavior.** Where the current application does something odd — a
chart that re-measures itself a beat late, a request fired twice on one
navigation, a selection that survives a navigation it arguably should not, a
sidebar entry that links nowhere, a panel imported by the route table and
rendered by no route — it keeps doing it.

---

## 7. Scope

Every JavaScript file in the repository is in scope, whether or not anything
imports it. A module nothing imports is still a module in the repository.

A file the running application genuinely does not reach may be removed rather
than brought along; anything reachable is brought along, not removed. A module
the route table imports is reachable, whatever the route table does with it
afterwards.

Every JavaScript file still in the repository at the end parses under a current
JavaScript parser.

---

## 8. Build and toolchain

**The build is not replaced.** Vite is the declared build tool, `npm run build`
writes a static site to `/app/dist` including `index.html`, and what it writes
is compiled from this repository's own source by that build tool rather than
copied from a build made earlier. This is a standing requirement of the tree,
not a detail of how it arrived here.

**The site stands on its own.** Every asset the built page references ships
inside `/app/dist`, and none of them is fetched from another host. A script the
page pulls from somewhere else is not part of what this repository builds, and
it is javascript a visitor waits for that no budget of yours is measuring.

**Lockfile.** `package-lock.json` is part of this repository and must agree with
`package.json`. The install is performed from the lockfile: it fails outright,
naming the file, both when the two disagree and when the file is not there at
all. It also fails on an unresolved dependency conflict, which is the first
thing this step will hand you. This step changes the dependency set, so
regenerate the lockfile with the package manager, and never delete it.

---

## 9. Meta requirements

- The implementation budget for this step is 7,200 seconds. Check
  `bash /app/timer.sh`; the configured timeout is authoritative.
- Work in `/app`. Preserve the repository's source, assets and documents except
  where this instruction requires otherwise.
- Do not cheat by using online solutions or hints specific to this task. Work
  only from this workspace and its documentation; do not search the system
  for other implementations or answers.
- Do not write a prose answer, a migration report or an alternate entry point.
  The migrated repository is the deliverable.
