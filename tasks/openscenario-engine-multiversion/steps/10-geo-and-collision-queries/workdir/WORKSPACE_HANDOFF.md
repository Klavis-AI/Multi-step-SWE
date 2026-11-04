# Workspace handoff

## What you receive

An esmini tree at the previous release, already configured and building offline
and headless. `USE_OSG`, `USE_OSI`, `USE_SUMO`, `USE_GTEST`, `USE_IMPLOT` and
`USE_PROJ` are all `False`, `FORCE_DOWNLOAD_BINARIES` and `DOWNLOAD_EXTERNALS`
are `False`, and nothing reaches the network during a build. The build produces
`esmini`, `esmini-dyn`, `odrplot`, `dat2csv`, `libesminiLib.so` and
`libesminiRMLib.so`. Because OSG is off, `replayer` and `odrviewer` are not
built, and no scene graph is generated or loaded; that is deliberate and must
stay that way.

Working at this tag: the whole scenario-engine and road-manager public API as
it stands. Initialising from a file and from a scenario held in memory as a
string; stepping and closing; object state readback; the motion-state reporters
and getters; road, lane and network enumeration; position handles with road,
lane, s, heading and world setters, and their error paths; parameter-value
distributions and permutation selection; reading `VariableDeclarations` with
type checking; collision detection with the count-and-partner bookkeeping for
overlaps that are happening now; the `.dat` recording and its decoder; the CSV
logger; `odrplot`'s output; and the command line.

Not present at this tag: any query that looks ahead of the current instant or
outside the metric plane the scenario runs in. There is no way for an embedder
to ask how long it is until two entities meet, no way to ask whether an entity
may legally leave the lane it is in, no way to read back the state of a
vehicle's lamps — the `LightStateAction` element does not exist at this tag, so
a scenario using one does not describe anything the engine can act on — and
no way to obtain a geodetic position for an entity. On the tooling side the
recording decoder writes floating-point columns at one fixed number of
decimals, with no way to ask for more or fewer. And an entity given no route
of its own picks its way through a junction arbitrarily when it drives into
one, so the same scenario run twice need not follow the same path. That is
about the path driven, not about what a lookahead query reports: lookahead has
its own junction handling and its own caller-supplied selector, and is not part
of this.

## What you build

Those queries, added to the public C API of the scenario-engine library; the
scenario action that drives light state, made to take effect on the entity it
names; and a precision control on the recording decoder. The recorded layout
changes as a result, so the recording format version moves with it. One piece
of existing engine behaviour moves with them: a routeless entity reaching a
junction goes straight on rather than picking an exit arbitrarily. The exact
signatures, return conventions, enumerations, failure behaviour and option
spelling are in the instruction; it is the authority, not this file.

## Ground rules

- `esminiLib.hpp` is `extern "C"` and stays that way. Every new entry point is
  C-callable under the name the instruction gives it, resolvable from the
  shared library by that plain name.

- The new prediction query is an addition alongside the existing collision
  bookkeeping, not a replacement for it. Both surfaces must work afterwards.

- Failure paths are part of the contract, not an afterthought. An entity id
  that does not exist has a defined, non-crashing answer everywhere. A request
  the engine cannot resolve is a different case and not necessarily an error at
  all: the instruction says what each unresolvable argument yields, and for the
  prediction query it is a success return that *does* write a stand-in value
  through the caller's pointer. What no entry point may do is write through a
  **null** pointer the caller supplied. A null out-parameter is a defined
  failure on the two entry points whose instruction text says so — `SE_GetTimeToCollision` and
  `SE_GetGeoPos`, which both return -1 and write nothing. It is **not** a
  promise made for `SE_GetObjectLightState`: that call requires a non-null
  `light_state`, and nothing is required of it when given a null one.

- Except where the instruction says otherwise, nothing the engine already does
  may change: the road manager's queries and their error paths, the collision
  bookkeeping, parameter-value distributions, initialising from a string,
  variable readback, the CSV logger's output, `odrplot`'s output, and the
  published set of artifacts with their usage text. The part of that list which
  is observed by reading text out of the artifacts — which entry points the two
  shared libraries export, what each of the four tools prints when asked to
  describe itself, and the exact shape of `odrplot`'s `track.csv` — is written
  out in the instruction's closing section, so "unchanged" is checkable against
  a stated form rather than against your memory of the tool.

- The build stays offline, headless and reproducible with the CMake options
  listed above; nothing you are asked to build needs a previously-off option
  turned on or a dependency fetched. Nothing inspects your CMake cache, so this
  is context rather than a checkable deliverable — what is observed is how the
  artifacts behave when built this way. The projection library is the case
  where that has teeth: with it off, an entry point that needs it is still
  required to exist, to be callable, and to report cleanly that it cannot
  answer.

- The graded tests drive the engine through the public libraries and the
  command-line tools only, using their own scenarios and road networks. They do
  not read your build system, your source layout or your internal helpers.
