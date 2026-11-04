# Step 8 — react-router 7, and what the application costs to open

`/app` now holds the application on **React 19** with the **React Compiler** in
its build: function components throughout, the store bindings on react-redux 9,
the route table written as the data router's, the root created through
`createRoot` with StrictMode on, the chart panels loaded on demand behind
`lazy`, the validation layer gone, and the hand-written memoisation gone with
it.

Two things it still carries from before any of that. Its router is
**react-router-dom 6** — a package that, from version 7 on, is a re-export of
`react-router` and nothing else. And many of its effects are not
effects: they watch a value and write a second copy of it into state, one render
behind the value they copied. That shape is what a class component's
`componentWillReceiveProps` turns into when it is converted a line at a time,
and it is the last of the class-era code in this tree.

This step moves the router on to **version 7** and takes the derived state out
of the effects. Then it asks what the result costs to open.

**The environment and how the tree runs.** Node 22 is installed. `/app` is the whole
repository: the application under `src/`, the Sass under `src/styles/`, and the
Express mock API under `server/demo1`, which serves both the data routes and —
from its own `public/` directory — the built site. `npm run build` writes the
site to `/app/dist`; putting that output in the server's static root and
starting the server is how the application runs, as `/app/README.md` describes.
The application is a hash-routed single page and `src/constants/actionApi.js`
holds the API base and the path of every request it makes.

---

## 1. The router is react-router 7

**The router this application runs on is `react-router`, at major version 7.**
Version 7 specifically, and installed rather than merely asked for: what routes
the application is what the package manager put in `node_modules`, and a
manifest range is a request. react-router's newest release is a later major
than this one, so installing "the latest" is not this requirement.

**`react-router-dom` is not a dependency of this repository any more.** Since
version 7 that package is a re-export shim with no code of its own, and a tree
that renamed its imports and left the package declared still ships something it
does not use and still pins a second copy of the router that can drift from the
first. One copy of the router is installed, not two.

**And no module imports from `react-router-dom`.** Every name this tree reaches
for through it is exported by `react-router` itself, so this is a rename and not
a rewrite. Every module that reaches for the router does so through one such
import; the requirement is all of them, and a tree that leaves one behind
has left a package installed that the paragraph above says should not be.

---

## 2. The effects that are not effects

An effect is for reaching outside React: a subscription, a timer, a chart
library that owns a piece of the DOM. It is not for computing a value the render
could have computed.

This application does the second at scale. The shape repeats: a component holds
a piece of state, an effect watches a value from the store or from props, and
the effect's body writes that value — sliced, reversed, defaulted, or simply
copied — into the state. The render then reads the copy. It is one render
behind, it stores the same information twice, and every one of them is a
dependency list that has to be kept correct by hand.

**After this step, a value the render can compute is computed by the render.**
Not stored in state and not written by an effect. State is for what the
component is *told* — what the user typed, which tab is open, what the store
sent — and a value derived from those is derived where it is used.

The shape this is about is a `setState` called straight from an effect's own
body. A piece of state the user also edits is state the component is told, even
when it starts life from a default the store supplies, and the effect that seeds
it is not what this asks you to remove; neither is a `setState` that happens
because something outside React said so rather than because a render did — a
response arriving, a subscription reporting, an interval doing the waiting the
component exists to do.

What decides that is where the value comes from, not where the call is written.
A write the render caused does not stop being one by being scheduled instead of
performed. If the only thing being waited for is the end of the work already in
hand, nothing has been waited for: the value is still derived, still held in
`useState`, still a render behind, and it is read here as what it is, however
the scheduling is spelled and whatever it is routed through. Scheduling that is
waiting for something is the other case — a reply, a repetition, an event, a
pause long enough to be a pause and written down as one.

**And the effects that remain say what they read.** Every effect that stays
because it genuinely reaches outside React carries a dependency list naming
every value from the component's own scope that its body reads. Not a shorter
list that happens to work today. Some effects are right to have no list at all:
the ones that have to run after every commit because running after every commit
is what they are for — keeping a ref pointed at the latest value of a prop or a
callback, or pushing the current props at something outside React that has no
opinion about which of them changed. An effect like that writes no state, so
running every time cannot feed it. Any other effect that loses its list has not
answered the question of what it depends on, it has arranged for nobody to ask.

These two are one requirement in practice, because they are the two halves of
the same construct: the derived-state effect is the reason the dependency lists
are wrong, and a list made honest around an effect that should not exist is
still an effect that should not exist.

**A rule you switch off is a rule you broke.** A comment that disables either of
these checks at a site counts here as the violation it hides. So does an effect
rewritten to read fewer values so that its short list becomes true — that
changes what the effect does, and §5 is about that.

**Deriving is not deleting.** The value the effect used to compute is a value
the render still needs, and it still needs it in the same shape: the same
slice, the same order, the same fallback when the store has not answered yet.
The arrays it derives from belong to the store, so a derivation that reverses
or sorts one works on a copy of its own.

**This section carries the rest of the step with it.** Most of what follows is
either this step's own leap or an invariant from earlier in this migration that
still has to hold, and that second kind — and the two budgets in §3 with
it — is not met by a tree that has left most of this construct standing, that
is not on the router §1 names, or whose components the compiler no longer
compiles. Getting most of these effects out comes first, and it is held to a
fixed number rather than a count of your own tree: across the repository, the
effects that set state straight from their own body together with the
dependency lists that do not say what their hook depends on -- such as a value
from the component's scope the hook reads but its list leaves out, an entry the
list does not need, or an entry that is a new value on every render -- must
number at most **21**, a quarter of the 85 the tree this step is meant to start
from has. A comment that switches such a check off counts as one more. A tree that
installs the router, holds both budgets and keeps every earlier invariant, but
has done only a little of §2, is a tree part of the way through this step
rather than a tree that finished it. Work through all of them.

---

## 3. What the application costs to open

Both of these hold of the built site served the way §7 describes.

**The entry cost.** Open the site at an address the application has no route
for, and wait for the application's own no-such-route screen to appear: the
JavaScript the browser had to fetch to get there — all of it, however it was
reached — is under **140 KB gzipped** (140,000 bytes, each file gzipped at maximum compression). That screen is also where the fetching
stops: with it on the page the application is not still pulling the rest of
itself down behind it.

**This is the same number you were handed, and §1 breaks it.** The step before
this one asked for 140 KB and the tree met it with room to spare. The router
you are installing is larger than the one it replaces, by more than that room:
change nothing else and the built entry goes over this wall. So the budget is
work here rather than an invariant to leave alone. It is not paid for by §2,
and it is not paid for by measuring differently — what is in the entry has to
change.

**The chart screen.** Open the site at `#/statistics/total` — the visitor-count
panel under 商城统计 — and wait for the chart library to come up in that panel:
the JavaScript fetched to get *there*, from the first byte to the chart, is
under **296 KB gzipped** (296,000 bytes, counted the same way). The budget above is measured on a screen with no chart on it, which
is the one place a split entry chunk is easiest to make look good; this one is a
screen that renders the application's own data through the chart library, and it
is what says whether the split the earlier steps built still pays on a real
screen. As above, that is also where the fetching stops.

**And the console is still silent.** The tree is built a second time with
`NODE_ENV=development`, served, and visited at one address per layout plus every
compare-by-seller screen plus an address no route matches; every `console` error
and every uncaught exception across those addresses is counted, and the count is
**zero**.

This is the reading most likely to move under §2. An effect that computed state
and a render that computes the same value are not equally forgiving about a
store that has not answered yet: the effect ran after a render that had already
guarded itself, and the render has to guard itself. If something starts talking,
it is telling you about §2.

**Counting is not the same as silencing.** The count is taken from the code this
repository builds. A tree that reassigns, wraps, filters or stubs `console`, or
that catches and swallows what it is reporting, has changed what the count is a
count of and not what it is counting — and so has a tree whose build is
configured to remove those calls, or to replace them with something that does
nothing, before they can run.

---

## 4. What the tree already has, it keeps

This step is a router major, a construct, and two budgets. Nothing else in the
tree is its subject, and nothing else in the tree is a way of paying for it.

- **The runtime stays.** React is still on major 19, and react-dom is still on
  the major that matches it. The store bindings are still react-redux 9, at 9.2
  or newer.
- **The compiler stays.** `babel-plugin-react-compiler` is still a declared
  dependency and still runs on the javascript the site serves, no module opts
  itself in or out, and the hand-written `useMemo`, `useCallback` and `memo`
  wrappers stay gone. §2 removes effects; it does not bring memoisation back to
  replace them.
- **The rules of React still hold.** No component reads or writes a ref while it
  renders, mutates its props or state, sets state during render outside the
  adjustment React allows, calls a hook conditionally, or declares a component
  inside another component's body. The compiler verifies this before it
  transforms anything, and a component it bails out of is a component this step
  made worse.
- **The validation layer stays gone.** No component declares a `propTypes`
  object, no module imports `prop-types`, and it is not a dependency of this
  repository.
- **The concurrent work stays.** The seller sidebar is still rendered from a
  deferred value the render reads, and the three range changes that hand a range
  down to the screen below are still made inside a transition that is actually
  started.
- **The split stays.** All twelve chart panels are still reached through React's
  own `lazy`, they still render under a `<Suspense>` boundary, the chart library
  is still out of the entry chunk, and the built site still reaches at least
  forty-two JavaScript chunks.
- **The root stays.** The application still mounts through `createRoot` and
  still renders StrictMode around what it mounts — every root it creates, not
  most of them. The render call `createRoot` replaced does not come back.
- **The component model stays.** Nothing goes back to a class.
- **The data layer stays.** No route grows a `loader`, an `action` or a
  `shouldRevalidate`; no module goes to the server on its own account instead of
  through the data layer's own hooks; the screens still call those hooks — at
  least thirty call sites among the modules the application actually reaches;
  and the route table is still the data router's. §1 changes which package the
  data router comes from, not that it is one.
- **The icons stay.** No module goes back to reaching an icon through
  `react-icons/lib/…`, nor does the icon package go back below the major the
  earlier step left it on — it is still major 5 or later.
- **The build tool stays.** Vite is still the declared build tool. The budgets
  above are a claim about what *this* build emits; a tree that answered them by
  changing bundlers answered a different question.

---

## 5. Behavior does not change

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

This is the constraint that decides how §2 is done. A derived-state effect is
one render behind, and the render that used to read its result read the old
value on the render the value changed. Where that shows on screen, it shows;
where it does not, it does not. A value computed in the render is current, and
"current" is the same as "one behind" everywhere the intermediate frame was
never painted — which is most places here, and not all of them. The ones where
it is not are the ones to look at.

**Version 7 is a major.** Its defaults are not its predecessor's. Whatever the
route table relied on from the version it is leaving, it relies on afterwards or
it is written to not need it.

---

## 6. Scope

Every JavaScript file in the repository is in scope, whether or not anything
imports it. A module nothing imports is still a module in the repository.

A file the running application genuinely does not reach may be removed rather
than brought along; anything reachable is brought along, not removed. A module
the route table imports is reachable, whatever the route table does with it
afterwards.

Every JavaScript file still in the repository at the end parses under a current
JavaScript parser.

---

## 7. Build and toolchain

**The build is not replaced.** Vite is the declared build tool, `npm run build`
writes a static site to `/app/dist` including `index.html`, and what it writes
is compiled from this repository's own source by that build tool rather than
copied from a build made earlier.

**The site stands on its own.** Every asset the built page references ships
inside `/app/dist`, and none of them is fetched from another host. A script the
page pulls from somewhere else is not part of what this repository builds, and
it is javascript a visitor waits for that no budget of yours is measuring.

**Lockfile.** `package-lock.json` is part of this repository and must agree with
`package.json`. The install is performed from the lockfile: it fails outright,
naming the file, both when the two disagree and when the file is not there at
all. This step changes the dependency set, so regenerate the lockfile with the
package manager, and never delete it.

---

## 8. Meta requirements

- The implementation budget for this step is 7,200 seconds. Check
  `bash /app/timer.sh`; the configured timeout is authoritative.
- Work in `/app`. Preserve the repository's source, assets and documents except
  where this instruction requires otherwise.
- Do not cheat by using online solutions or hints specific to this task. Work
  only from this workspace and its documentation; do not search the system
  for other implementations or answers.
- Do not write a prose answer, a migration report or an alternate entry point.
  The migrated repository is the deliverable.
