# Step 5 — concurrent rendering

`/app` now holds the application on React 18, mounted through a root with
StrictMode on. It still uses React 18 the way React 16 was used: every state
change is urgent, every render is uninterruptible, and the one thing the root
made available — a scheduler that can tell the update a user is waiting on from
the update that can wait — is not used anywhere.

This step uses it. Three places, each of them a measurement of this application
rather than a demonstration of an API.

**The environment and how the tree runs.** Node 22 is installed. `/app` is the whole
repository: the application under `src/`, the Sass under `src/styles/`, and the
Express mock API under `server/demo1`, which serves both the data routes and —
from its own `public/` directory — the built site. `npm run build` writes the
site to `/app/dist`; putting that output in the server's static root and
starting the server is how the application runs, as `/app/README.md` describes.
The application is a hash-routed single page and `src/constants/actionApi.js`
holds the API base and the path of every request it makes.

React stays on major 18 and react-dom with it. All three of the features below
are React 18's own, and what this step asks for is that this application use
them: moving the dependency is not one of the three and is not a way of doing
any of them.

---

## 1. The seller list is a deferred value

**The 搜索商家 box filters the seller sidebar on every keystroke.** The list it
filters is the whole seller list the store holds, each entry rendered as its own
link, and every character typed re-renders all of them before the character
appears. The keystroke is the update the user is waiting on; the list is not.

So the rows the sidebar renders come from a **deferred** copy of the filtered
list — `useDeferredValue`, with the value it returns being what the rows are
built from. A `useDeferredValue` whose result nothing reads defers nothing.

**Defer the list, not the box.** The filtered list and the text in the box are
two different values and only one of them is safe to derive from the other. The
list is not a pure function of what the box holds: 全部商家 puts every seller
back in the sidebar and leaves the text where it was, and that is behavior this
application has. A rewrite that computes the rows from the search text alone is
smaller, and it loses that.

---

## 2. The range changes are transitions

**A time-range pick changes two things at once, and only one of them is
urgent.** The quick-select strips and the 查询 buttons both set a range; on the
screens where that range is handed down to the screen below — the ones that put
it in an `<Outlet>`'s context, and the compare-by-seller screen that hands its
pair of sellers down the same way — changing it re-renders a chart's worth of
component tree and starts a new request. The click itself is what the user is
waiting on.

So on those screens the state change that the screens below re-render from is
made inside a **transition** — `useTransition`, and the function it gives you
actually called with that update inside it. The state that only marks which
control was clicked is not deferred with it: that is the feedback the click is
for, and a transition around it would delay the one thing that has to be
immediate.

**Only where it is true.** Several screens have the same strip of quick-range
links and do not do this: their handler calls the data layer's trigger directly
and sets nothing the render reads. A transition around a state change nothing
re-renders from is a wrapper, not a migration — read each handler and change the
ones this paragraph describes.

---

## 3. The chart bodies arrive on demand

**The first chunk this application loads is most of a megabyte, and what makes
it that is the chart library.** The routes are already split, and the split is
real: the page reaches dozens of chunks and each route component is one of them.
But every module `src/components/app.js` still imports *statically* is in the
entry chunk with it, and the ones that are left are the chart panels that are
written as a child route's `element` rather than as a route with a component of
its own — a shape a route's `lazy` cannot reach, because `lazy` replaces the
route's component and an element written inline is not one. Each of them imports
echarts, so echarts is in the entry chunk, so every visitor downloads it before
anything renders, including on the screens that draw no chart at all.

**Those panels are loaded when they are needed.** They are reached through
React's own `lazy`, and they render under a `<Suspense>` boundary. Not a route's
`lazy`: these are chart *bodies* inside a shell that stays on the screen — the
sidebar, the seller pickers, the range strip — and the difference is what
happens while the chunk is in the air. A route's `lazy` is awaited as part of
the navigation, so nothing commits until it lands and the screen being left
stays up; a boundary commits the shell at once and fills the body in. The shell
is the part that is already there, so it is the part that should not wait.

**A chunk apiece.** All twelve of the inline chart bodies load on demand, and
each of them arrives on its own. All twelve because a panel left on a static
import keeps the chart library in the entry chunk and the megabyte with it, so
moving most of them changes nothing a visitor waits for. On its own because
twelve panels behind a single module that holds them all is one chunk a visitor
to any one of them downloads whole, and that is not what *when they are needed*
means. So each panel is reached through its own dynamic import, and each of
those bodies adds a chunk of its own on top of whatever this build already
produces: however many JavaScript chunks the built site reaches today, it
reaches at least twelve more than that afterwards.

**And the budget says it worked.** Open the site at an address the application
has no route for, and wait for the application's own no-such-route screen to
appear: the JavaScript the browser had to fetch to get there — all of it,
however it was reached — is under **140 KB gzipped** (140,000 bytes, each file gzipped at maximum compression) afterwards. That is a
measurement of what a visitor waits for, not of how the files are arranged. And
that screen is where the fetching stops: with it on the page the application is
not still pulling the rest of itself down behind it, because a screen painted in
front of a download that is still in flight has moved nothing a visitor waits
for — only when they find out they are waiting. Splitting the same modules into
more chunks the page still pulls in on its way to that screen moves bytes
between files and changes this number by nothing, and neither does putting the
application one import further away, since the browser fetches whatever it finds
there before it has a screen to show. A `<Suspense>` around something that never
suspends, or a `lazy` beside a static import of the same module, leaves the
waiting where it was, and the byte count is what tells those apart from the
work.

**What the entry chunk was quietly lending.** This application does not import
echarts whole; each module imports the echarts core and then the pieces its
option uses, as bare side-effect lines:

```js
import echarts from 'echarts/lib/echarts';
import 'echarts/lib/chart/line';
import 'echarts/lib/component/tooltip';
```

Those lines register the piece into echarts itself, once, globally. They are not
scoped to the module that wrote them: whichever module runs the line first, the
piece is available to every chart drawn afterwards, anywhere on the page. And
because every chart panel has been in the entry chunk together, a module has
never had to import a piece it draws with as long as *some* module in that chunk
did. Count the pieces a screen's option actually asks for and compare them with
that file's own imports: `toolbox`, `grid`, `legend` and `bar` are drawn in far
more places than they are imported. The panels are living off each other.

Moving a panel behind a dynamic import ends the loan in both directions. The
registrations it performed no longer happen at startup, so a screen that never
loads that chunk loses a piece it was borrowing; and the registrations it was
itself borrowing from a neighbour still in the entry chunk keep working, which
is why the damage is uneven and easy to miss. What echarts does about a missing
piece is worse than an error: an unregistered *component* is dropped from the
option silently, so a chart renders without its toolbox and looks nearly right,
while an unregistered *series* throws `Component series.bar not exists. Load it
first.` and the screen does not render at all. Both are §4 failures. Every
module has to import every echarts piece its own option uses — the ones it
already imports and the ones it has been getting for free. This is a list of
what each module must import for itself and not an instruction to register
everything echarts has: a piece nothing in this tree registers today stays
unregistered afterwards.

The loan has an order to it as well. A registration line runs once, when the
module holding it runs, and echarts draws a chart's components in the order they
registered. One entry chunk holding every panel made that order the same on
every screen; panels fetched when they are needed make it depend on which panel
the visitor reached first, so the same chart can come out with every coordinate,
colour and label identical and its parts drawn in a different order. Two modules
that register the same pieces in different orders are what makes that possible,
and the order the application draws in today is the one §4 compares against.

A boundary is not a licence to change what the screen ends up showing: §4 holds
through this as through everything else, and the fallback is what is on the page
while the chunk loads, not after.

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

**This step is about when work happens, not about what work happens.** The
router, the data layer, the component model and the root are where the steps
before this one left them: no route grows a `loader`, an `action` or a
`shouldRevalidate`, no module goes to the server on its own account instead of
through the data layer's own hooks, nothing goes back to a class, the route
table stays the data router's, the application still mounts through a root that
renders StrictMode, and no module goes back to reaching an icon through
`react-icons/lib/…` — nor does the icon package go back below the major the
previous step left it on, which is to say it is still major 5 or later. Those
hooks are also still what the screens call: a screen that stops using its hook
has left the data layer, not moved within it.

**Deferring is not dropping.** A deferred value and a transition both mean a
render happens later, never that it does not happen. The settled screen — after
the request has landed and the chart has drawn — is the same screen it is today,
and that is what is compared.

**StrictMode stays on and stays quiet.** The tree is built a second time with
`NODE_ENV=development`, served, and visited at one address per layout plus every
compare-by-seller screen plus an address no route matches; every `console` error
and every uncaught exception across those addresses is counted, and the count is
zero. It is zero on the tree in front of you. Suspense is where it is easiest
to put back: suspending on an update React was given as urgent is something
React 18 reports, and it is reported in development only.

**The API is fixed.** The mock API under `/app/server` — its routes, its data,
its response shapes and its timing — is part of the interface. Do not change it,
and do not change the paths in `src/constants/actionApi.js` that address it.

**Bugs are behavior.** Where the current application does something odd — a
chart that re-measures itself a beat late, a request fired twice on one
navigation, a selection that survives a navigation it arguably should not, a
sidebar entry that links nowhere, a panel imported by the route table and
rendered by no route — it keeps doing it.

---

## 5. Scope

Every JavaScript file in the repository is in scope, whether or not anything
imports it. A module nothing imports is still a module in the repository.

Two acts clear such a file: converting it, or deleting it. A file the running
application genuinely does not reach may be removed rather than converted;
anything reachable is converted, not removed. A module the route table imports
is reachable, whatever the route table does with it afterwards.

Every JavaScript file still in the repository at the end parses under a current
JavaScript parser.

---

## 6. Build and toolchain

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
all. If this step changes the dependency set, regenerate the lockfile with the
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
