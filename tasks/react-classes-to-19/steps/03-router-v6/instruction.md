# Step 3 — react-router 6, and a route table that is data

`/app` now holds the React 16.8 function-component application on Redux
Toolkit that the previous two steps produced. Its routing is still the one
written in 2017: `react-router` 3, driven by `hashHistory`, with a single JSX
route table in `src/components/app.js` that nests dozens of `<Route>` rows
under one root; `<IndexRoute>` and `<IndexRedirect>` for the landing pages;
seven `<Redirect>` rows; route components named through `getComponent`
callbacks that resolve a dynamic `import()`; layouts that receive the matched
child as `this.props.children` and hand it data by cloning it; screens that
read the shop id out of a `params` prop the router injected; and links that
mark themselves current through `activeClassName`.

Replace all of it with react-router 6, without changing a single thing the
application does.

**The environment and how the tree runs.** Node 22 is installed. `/app` is the whole
repository: the application under `src/`, the Sass under `src/styles/`, and the
Express mock API under `server/demo1`, which serves both the data routes and —
from its own `public/` directory — the built site. `npm run build` writes the
site to `/app/dist`; putting that output in the server's static root and
starting the server is how the application runs, as `/app/README.md` describes.
The application is a hash-routed single page and `src/constants/actionApi.js`
holds the API base and the path of every request it makes.

---

## 1. The router

**react-router 6 is the project's declared router.** `react-router-dom` is a
dependency of `/app/package.json`, and the router that is installed when the
install finishes is version 6.4 or later within major 6 — the router factories
and the provider the next paragraph asks for arrived in 6.4, so a 6.0–6.3 range
does not have the route table this step describes, and a later major is a
different migration from the one this step is. The DOM package is the one the application
declares; `react-router` on its own is not the dependency a web application
takes.

**The route table is a description handed to a router, not an element tree that
is rendered.** The routes are turned into a router object by one of the
package's own router factories, and that object is handed to the package's
provider, which is what the application renders. Hash addressing is part of
this: see §5.

**The retired API is gone.** No module imports `Router`, `IndexRoute`,
`IndexRedirect`, `IndexLink`, `RouterContext`, `applyRouterMiddleware`,
`withRouter`, `Switch`, `Redirect`, `hashHistory`, `browserHistory`,
`createMemoryHistory`, `useHistory`, `useRouteMatch` or `match` from the router
package, and no element in the repository carries an `activeClassName` or an
`activeStyle` attribute.

**No route names its component through a loader callback.** `getComponent` and
`getComponents` are gone. What replaces them is §4's business: the route
components go on being fetched when they are first needed, and how that is
spelled is yours to choose.

---

## 2. The route table

**Layouts render an outlet.** Eight components in this application are layouts:
they render a frame — a header, a sidebar, a date picker, a tab strip — around
whichever child route matched. Today they get that child as a `children` prop the router
handed them and render it directly. After this step each one of the eight renders
the router's own outlet in that place.

**What the layouts used to clone in travels through the outlet.** Five of those
layouts do not merely render their child; they clone it to inject props — the
selected time range, the shop name setter, the active-route setter, the pair of
shops being compared, a default state. `React.cloneElement` is called nowhere in
the repository afterwards. The router provides a channel from a layout to the
route rendered inside its outlet, and that is the channel this data travels
through. Every value that is cloned in today arrives at the same component with
the same meaning afterwards.

**The dynamic part of the address is read from the router.** The shop id in
`/sellers/:id/...` reaches its screens today as a `params` prop that the router
injected into the route component and that the route component then threaded
down by hand. It is read from the router instead, by the components that need
it.

**The seven redirecting addresses still redirect, declared the new way.** Six
`<Redirect from … to …>` rows and one `<IndexRedirect>` are what make `#/`,
`#/firstPage`, `#/sellers`, `#/sellers/:id`, `#/sellers/:id/history`,
`#/statistics` and `#/compare` land where they land. Each of them still lands in
exactly the same place, expressed with the new major's own way of saying "this
address sends you to that one" rather than with a route row that no longer
exists.

**The index routes stay index routes.** Every `<IndexRoute>` in the present
table renders the same component at the same address afterwards.

**The catch-all stays.** `<Route path="*">` renders the not-found page today on
any address no other route matches, including nested ones; it goes on doing so.

---

## 3. Links

**Links that mark themselves current are the router's own navigation link.**
The four top-menu links and the forty-odd sidebar links say they are current
with `activeClassName="active"` today. After this step that marking comes from
the package's own navigation link component rather than from a prop the retired
major supplied.

**The markup a link renders does not change.** This is the sharpest edge in the
step and it is worth reading twice. The new major's navigation link is not a
drop-in for the old one:

- It renders an `aria-current` attribute when it considers itself current. The
  retired major's `Link` never rendered one. That attribute is part of the DOM
  this step must not change, so it is not there afterwards either.
- It decides what is current from the **address**, by prefix. The retired major
  decided from the **routes that matched**. The difference shows on an address
  no route matches at all: `#/statistics/nosuchchart` renders the not-found page
  and marked *nothing* current, where a link to `/statistics` is a prefix of
  that address. Both halves are the requirement: a link is current when the
  address says so **and** the route it points at is one of the routes that
  actually matched.
- Matching is case-insensitive, as the retired major's was. `#/Statistics/total`
  matched the `/statistics/total` route and marked its link current; it still
  does. A migration that opts the router into case-sensitive matching changes
  what the page renders.
- A trailing slash does not change what matched. `#/statistics/total/` and
  `#/statistics/total` are the same route and mark the same link current.
- An address may carry percent escapes where the link that points at it is
  built from a decoded id. They still agree.
- Where both a static class and the current-class are present, the class
  attribute reads the same as it does today, in the same order.

**Links that never marked themselves current stay plain links.** The brand logo
in the header, the "back to the front page" link on the not-found page and the
shop names in the seller table are ordinary links today and render exactly what
they render today — including *not* growing an `aria-current`.

---

## 4. Code splitting survives

The route components are loaded on demand today: every `getComponent` callback
resolves a dynamic `import()`, and the build emits a chunk per route group. The
retired prop is going away and the loading must not go with it. After this step
the route components are still fetched when they are first needed, and the built
page still reaches at least thirty JavaScript files.

Which of the several shapes the new major and the build tool offer you use to
express that is not this step's business. The requirement is a property of the
built output: a tree that keeps its lazy loading and a tree that inlines forty
route components into one bundle differ there, not in the source.

---

## 5. Behavior does not change

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

**The application stays hash-routed.** Every address this application has is
`http://host/#/something`, and the fragment is part of what must stay
identical, at every route, interaction and navigation. A migration that moves to
history URLs, or to a router that keeps its own address somewhere other than
the fragment, changes every address in the application at once.

**Every address resolves to the same page.** The present table is nested three
deep in places, has a route whose path is a parameter, has index routes, has a
catch-all, and matches case-insensitively. Addresses at the edges of that are
part of the specification too: an unknown leaf under a known section, an unknown
shop id, a numeric one, a very long one, a percent-encoded one, a trailing
slash, a doubled slash, a `..` segment, a query string inside the fragment, an
empty fragment. Each of them renders afterwards what it renders today.

**The set, not the sequence.** *Which* API paths a screen asks for is compared;
how many times is not. A route rewrite should not change either, but a data
layer that serves a second visit from cache is not a change in behavior.

**This step is about routing and nothing else.** The tree already has one place
that owns its requests, and every screen reads its data through that layer's own
hook for it. The new major offers a route its own way to fetch data before it
renders; using it would move requests back out of that layer, and that is not
what this step is for. So no route declares a `loader`, an `action` or a
`shouldRevalidate` — neither as a key in a route description nor as an export of
a module a route loads, which is the other way this router picks those three up,
so no module in this repository exports a `loader`, an `action` or a
`shouldRevalidate` at all — and no module calls `useLoaderData`,
`useRouteLoaderData`, `useActionData`, `useFetcher`, `useSubmit`,
`useAsyncValue`, `useAsyncError` or `defer`. Leave the data layer where it is:
the screens go on reading their data through that layer's own hooks, and those
hooks stay how the running page gets its data rather than a remnant beside
something else — so nothing in this repository goes to the server on its own
account either, not through the browser's `fetch`, an `XMLHttpRequest` or an
HTTP client package. Consolidating two reads of the same thing into one is yours
to make; moving a read out of that layer is not. And leave the component model
alone.

**Components do not start rendering more often than they do now.** Two
components run an effect with no dependency list, so it runs after every commit.
A router construct that publishes navigation state a component did not
previously observe makes that component commit where it did not before, and
those effects then run more often than they did.

**The API is fixed.** The mock API under `/app/server` — its routes, its data,
its response shapes and its timing — is part of the interface. Do not change it,
and do not change the paths in `src/constants/actionApi.js` that address it.

**Bugs are behavior.** Where the current application does something odd — a
chart that re-measures itself a beat late, a request fired twice on one
navigation, a selection that survives a navigation it arguably should not, a
sidebar entry that links nowhere — it keeps doing it. This step changes how the
code is written, not what it does.

---

## 6. Scope

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

## 7. Build and toolchain

**The build is not replaced.** Vite is the declared build tool, `npm run build`
writes a static site to `/app/dist` including `index.html`, and what it writes
is compiled from this repository's own source by that build tool rather than
copied from a build made earlier. This is a standing requirement of the tree,
not a detail of how it arrived here.

**Lockfile.** `package-lock.json` is part of this repository and must agree with
`package.json`. The install is performed from the lockfile: it fails outright,
naming the file, both when the two disagree and when the file is not there at
all. This step changes the dependency set — a router package leaves and another
arrives — so the lockfile is regenerated by the package manager rather than
hand-edited, and never deleted.

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
