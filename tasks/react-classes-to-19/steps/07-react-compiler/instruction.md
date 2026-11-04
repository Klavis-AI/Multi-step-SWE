# Step 7 — the React Compiler

`/app` now holds the application on **React 19**: function components
throughout, the store bindings on react-redux 9, the router on the data router,
the root created through `createRoot` with StrictMode on, the chart panels
loaded on demand behind `lazy`, and the validation layer the runtime stopped
reading taken out.

It is also an application that memoises by hand. Ninety-odd `useMemo`,
`useCallback` and `memo` wrappers are spread across two thirds of its files,
each one a dependency list that a human has to keep correct and that nothing
checks. The previous step put them out of scope. This step is about them.

This step turns on the **React Compiler** and makes the application compilable.
Those are two jobs and the second is the larger one: the compiler does not
transform a component it cannot prove is safe — it skips it, quietly — so a
tree that switches the compiler on and changes nothing else ships a build that
looks compiled and is two thirds compiled. What makes a component safe is that
it obeys the rules of React, and this application breaks one of them in about
thirty places.

**The environment and how the tree runs.** Node 22 is installed. `/app` is the whole
repository: the application under `src/`, the Sass under `src/styles/`, and the
Express mock API under `server/demo1`, which serves both the data routes and —
from its own `public/` directory — the built site. `npm run build` writes the
site to `/app/dist`; putting that output in the server's static root and
starting the server is how the application runs, as `/app/README.md` describes.
The application is a hash-routed single page and `src/constants/actionApi.js`
holds the API base and the path of every request it makes.

---

## 1. The compiler compiles this application

**The React Compiler is part of this project's build.** It is a build-time
package — `babel-plugin-react-compiler` — and it is a **declared dependency of
this repository**, so that the next person to install this tree gets the same
build. A compiler that is configured but not declared is a build that works on
one machine.

**And it runs on the javascript the site serves.** Not on a development build
only: `npm run build` produces a site whose components were compiled, and the
compiler's own output is in the javascript that ships. This is a statement about
the output, not about the configuration — a compiler that is switched on in a
file nothing builds with has not compiled anything.

**The way it is switched on is plain.** The options the compiler is given are
options it accepts, and no module opts a component in or out — not behind a
run-time flag, and not with one of the compiler's own directives written into
its source. Both follow from the paragraph above: a configuration the compiler
rejects produces an uncompiled build, and a build that decides per caller
whether to hand back the compiled version or the original is not a build whose
components were compiled — it is a build that kept both.

---

## 2. Every component obeys the rules of React

The compiler is allowed to cache a component's work because React guarantees
certain things about a component, and it verifies those things before it
transforms anything. Where it cannot, it **bails out of that component and says
nothing about it at build time**. So "the compiler is on" and "the compiler
compiled this" are different claims, and the second is the requirement.

**After this step no component in this repository breaks a rule of React.**
Concretely, and in the order they matter here:

- **A ref is not read or written while a component renders.** A ref exists so
  that a component can hold something across renders *without* the render
  depending on it; touching it during render is exactly the case the compiler
  cannot cache. **This is the one this application does at scale** — the
  calendar reading during render the value it paints from, each
  compare-by-seller panel remembering which seller it is showing — and it is
  most of the work of this step. Roughly thirty sites. Each one wants the same
  question asked: what is this ref being used for at this moment, and does the
  component need it *during* render or after it?
- **Render is pure.** A component's render produces its output from its props,
  its state and its context, and does nothing else: it does not mutate anything
  it did not create during that render, does not write to a module-level or
  global value, and does not reach outside itself for something that can change.
- **State is not set while rendering.** Setting state during render is a loop
  the compiler cannot reason about, with one exception React makes itself: a
  component that notices during its own render that something it derives from
  has changed may adjust its state before it returns.
- **Props, state and the values derived from them are not mutated.** They are
  treated as read-only, including the arrays and objects inside them.
- **Hooks are called unconditionally, at the top level of a component or of
  another hook** — not inside a condition, a loop, a callback or a `try`.
- **A component or a hook is not declared inside another component's body.**
- **JSX is not built inside a `try`/`catch` in order to catch a child's
  errors.** Creating an element does not render it — React renders it later,
  after the statement that created it has finished — so a `catch` wrapped
  around the place the element was written never sees a failure from rendering
  it. What catches that is an error boundary around the component.
- **A memo hook's callback returns the value it is for and does only that.** It
  is not a place to run an effect, and it is not called for what it does on the
  way.
- **The syntax stays within what the compiler supports**, and the packages the
  components use are packages that work with it.

**A rule you switch off is a rule you broke.** A comment that disables a
rules-of-React check at a site counts here as the violation it hides: the point
of this step is that the compiler can compile the component, and the compiler
does not read comments.

**This section and §1 carry the rest of the step with them.** A tree whose
shipped javascript carries almost none of the compiler's output, or that still
breaks the rules of React in more than a handful of places, has not met §4 or
§5 either: what the tree already had does not satisfy them again for a build
the compiler was not let near.

---

## 3. The hand-written memoisation goes

**Remove the `useMemo`, `useCallback` and `memo` wrappers.** This is what the
compiler is for. Every one of them is a dependency list maintained by hand, and
the compiler will not fix a wrong one — it *preserves* manual memoisation
rather than replacing it, so a stale list stays stale and now hides behind a
tool that was supposed to make it unnecessary.

**The instruction is "remove", not "remove most".** If some site genuinely needs
a stable identity for a reason outside React — a value handed to a library that
keys on object identity, say — then keep that one and be able to say why; a
handful of justified survivors is not the difference between a step done and a
step not done. Nothing else in this tree is one, so expect to remove them all.

**A wrapper you keep has to be one the compiler can honour.** The compiler
preserves hand-written memoisation rather than overriding it, and it will only
compile a component at all when what it infers about that component matches or
exceeds what the wrapper already promised. So a survivor whose dependency list
does not say what the code actually depends on is not a harmless leftover: it
is a component the compiler declines to compile, which is the opposite of what
keeping it was for.

**Removing a wrapper is not the same as deleting the value.** The body of a
`useMemo` is computation the render still needs; it moves out of the hook and
stays in the render. The body of a `useCallback` is a function the child still
gets. A `memo` wrapper comes off the component, not the component off the tree.

---

## 4. What the compiler must not cost

**The entry cost is still under budget.** Open the site at an address the
application has no route for, and wait for the application's own no-such-route
screen to appear: the JavaScript the browser had to fetch to get there — all of
it, however it was reached — is under **140 KB gzipped** (140,000 bytes, each file gzipped at maximum compression). That screen is also
where the fetching stops: with it on the page the application is not still
pulling the rest of itself down behind it.

The compiler is not free here. It adds a runtime import and a cache prologue to
every component it compiles, and the entry closure holds a lot of them — so
this is the step where the budget is most obviously in tension with the work.
It fits, and with room; the way to keep it fitting is to not undo the split
that made it fit. **The budget does not move.**

**And the console is still silent.** The tree is built a second time with
`NODE_ENV=development`, served, and visited at one address per layout plus every
compare-by-seller screen plus an address no route matches; every `console` error
and every uncaught exception across those addresses is counted, and the count is
**zero**.

This is the reading most likely to move under this step, and it is worth
welcoming rather than working around. A compiled component's cache is discarded
and rebuilt by StrictMode's second invocation on purpose, so a render that was
quietly impure — the thing §2 is about — has a good chance of announcing itself
here instead of in production. If something starts talking, it is telling you
about §2.

**Counting is not the same as silencing.** The count is taken from the code this
repository builds. A tree that reassigns, wraps, filters or stubs `console`, or
that catches and swallows what it is reporting, has changed what the count is a
count of and not what it is counting — and so has a tree whose build is
configured to remove those calls, or to replace them with something that does
nothing, before they can run.

**And the dependency arrays are not left worse than you found them.** Effects
and the remaining hooks keep dependency lists that are at least as complete as
the ones in front of you now, and no more effects set state than do today.
Rewriting an effect's dependencies to quieten something §2 is about is not a fix
of what §2 is about. This is held to a number, and the number is a fixed one
rather than a count of your own tree: across the repository, the dependency
lists that do not say what their hook depends on -- such as a value from the
component's scope the hook reads but its list leaves out, an entry the list
does not need, or an entry that is a new value on every render -- together with
the effects that set state straight from their own body, number at most
**97** once this step is done. That is how many the tree this step is meant
to start from has. A comment that switches such a check off does
not take its site out of the count.

---

## 5. What the tree already has, it keeps

This step is a compiler and the compliance it needs. Nothing else in the tree is
its subject, and nothing else in the tree is a way of paying for it.

- **The runtime stays.** React is still on major 19, and react-dom is still on
  the major that matches it. The store bindings are still react-redux 9, at 9.2
  or newer — the range whose own manifest says it supports React 19. Moving
  that package on to a different major is not part of this step, and a tree
  that does it has changed the thing the step before this one was about.
- **The validation layer stays gone.** No component declares a `propTypes`
  object, no module imports `prop-types`, and it is not a dependency of this
  repository.
- **The concurrent work stays.** The seller sidebar is still rendered from a
  deferred value the render reads, and the three range changes that hand a range
  down to the screen below are still made inside a transition that is actually
  started. Neither is the compiler's job, and neither is redundant now that it
  is on: one is about *when* a render happens and the other about *how much* of
  it is recomputed.
- **The split stays.** All twelve chart panels are still reached through React's
  own `lazy`, they still render under a `<Suspense>` boundary, the chart library
  is still out of the entry chunk, **and the built site still reaches at least
  forty-two JavaScript chunks**. A compiled tree re-renders less, which makes
  the reason for a boundary less visible; the reason is the entry budget in §4,
  and it has not changed.
- **The root stays.** The application still mounts through `createRoot` and
  still renders StrictMode around what it mounts — every root it creates, not
  most of them. The render call `createRoot` replaced does not come back.
- **The component model stays.** Nothing goes back to a class. The compiler does
  not compile classes, so a class is a component this step did not reach.
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

This is the constraint that decides how §2 is done. "A ref is not read during
render" has a lazy answer — move the read into an effect and let the first paint
go out without it — and that answer changes what the first paint contains. Fewer
renders is not a behavior change; a different first paint is.

---

## 7. Scope

Every JavaScript file in the repository is in scope, whether or not anything
imports it. A module nothing imports is still a module in the repository, and
the compiler is asked about it too.

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
all. This step changes the dependency set, so regenerate the lockfile with the
package manager, and never delete it.

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
