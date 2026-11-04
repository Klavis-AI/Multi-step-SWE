# Workspace handoff — entering step 2

Three files in the workspace are task-owned rather than upstream:
`TEST_INTERFACE.md`, `PROFILE.md` and this file. They travel with the workspace
across every step. Leave them in place, and read `TEST_INTERFACE.md` for the
contract the checker holds you to.

## What you receive

An esmini tree at v2.23.1, already configured and building offline and
headless. `USE_OSG`, `USE_OSI`, `USE_SUMO`, `USE_GTEST`, `USE_IMPLOT` and
`USE_PROJ` are all `False`, `FORCE_DOWNLOAD_BINARIES` and `DOWNLOAD_EXTERNALS`
are `False`, and nothing reaches the network during a build. The build produces
`esmini`, `esmini-dyn`, `odrplot`, `dat2csv`, `libesminiLib.so` and
`libesminiRMLib.so`. Because OSG is off, `replayer` and `odrviewer` are not
built; that is deliberate and must stay that way.

Working at this tag: the road-manager query surface (road enumeration, road
length, lane count, lane width and lane type by road id and through a position
handle, lane and world-XYH position setters), the simple-vehicle model with its
deceleration limiter, the scenario engine's init/step/close cycle with object
state readback, and the command-line entry point with `--osc`, `--headless`,
`--fixed_timestep`, `--disable_controllers`, `--record` and `--csv_logger`.

The library side of that cycle is `SE_Init` (five arguments, the last of them
the record flag), `SE_Step`, `SE_StepDT`, `SE_Close`, the index-based
`SE_GetId` and `SE_GetObjectState` filling an `SE_ScenarioObjectState` whose
`speed` member is already there. Those declarations are not this step's
subject, but the checks reach this step's new work through them, so they have
to keep their v2.23.1 spellings; `instruction.md`'s "Pre-existing entry points
the checks also use" reproduces them.

Not present at this tag: any notion of a parameter distribution; no way to
count or select permutations; no `--param_dist`, `--param_permutation` or
`--save_xosc`; no double-precision simulation clock; no name-to-id lookup for
entities; no way to redirect the recording path from the library; and no
storyboard-element state-change callback. The older `SE_RegisterEventCallback`
is still present and still exported.

## What you build

The parameter-distribution capability, end to end: a distribution file can be
loaded and reset from the library, the permutations it generates can be counted,
selected by index and reported, an out-of-range index is rejected rather than
clamped, and the same capability is reachable from the command line — where
`--param_dist` alone sweeps every permutation from one invocation, and
`--save_xosc` writes the resolved scenario back out as a loadable OpenSCENARIO
file. `instruction.md` §§2-4 give the enumeration order and the filename
convention those runs must follow; both are graded.

Alongside it, three library additions — a settable recording path, an
entity-name-to-id lookup that reports a negative value for an unknown name, and
a double-precision simulation clock that agrees with the existing
single-precision one — and a storyboard-element state-change callback that
receives an element's name, type and new state. `instruction.md` §6 gives the
numeric `type` and `state` codes that callback must report; they are not the
numbering the starting tree's internal enum uses. §6 also states what the
callback is counting — state changes the storyboard already makes and already
prints, not one call per element per transition — and names the one element
whose participation this step changes: at this tag a manoeuvre is driven by
neither its group nor the storyboard and so never changes state, which is why
it is absent from the console trace a run prints today.

One more symbol, `SE_SetOffScreenRendering`, is added to the exported surface.
Because OSG is off in this profile it has no observable behaviour here and none
is under test: only its presence in the C-linkage symbol table is checked.

One removal: the event callback entry point goes away entirely, including from
the library's exported C-linkage symbol table.

All of the new entry points must be exported under their own unmangled names.

## What carries forward

The tree ends at v2.27.0 with the same build profile, the same CMake
invocation and the same six artifacts. The road-manager query surface, the
lane-geometry queries, the simple-vehicle deceleration model and the headless
command-line run all behave exactly as they did at v2.23.1 and are expected to
keep doing so.

The figures below are what v2.23.1 reports today, and what a solution that
preserves the surface therefore still reports. They are listed because they are
checked, and a number is easier to preserve than to rediscover:

* `straight_500m.xodr` — one road, id `1`, 500.000 m long, 2 lanes; lane width
  3.0700 m and lane type `2` through both the road-id and the position-handle
  query. A world-XYH round trip of a lane position at s = 250 stays on that
  road and on lane `-1` — the round trip there does return the lane it started
  from, unlike e6mini below.
* `e6mini.xodr` — one road, id `0`, 1464.434 m long, 6 lanes. A world-XYH
  round trip onto this road lands on lane `-2`, not the lane it started from;
  that snap is the existing behaviour and is expected to stay.
* `fabriksgatan.xodr` — 16 roads, one junction; road 0 reports **2 lanes** at
  s = 10, lane width 3.5000 m there, and lane type `2`.
* **The lane-type code for a driving lane is `2`**, on both the road-id and the
  position-handle spelling of the query. It is the road manager's own lane-type
  numbering, it is the same at v2.23.1 and v2.27.0, and it is checked as a
  literal value, so renumbering that enumeration is a regression. (The
  storyboard-element renumbering `instruction.md` §6 *does* ask for is a
  different enumeration and does not reach here.)
* **Position handles are numbered from `0`.** The first handle
  `RM_CreatePosition` returns in a freshly initialised road manager is `0`, and
  further handles follow in creation order.
* The simple-vehicle deceleration cap — 0.5 m/s per step, 5.0 m/s² implied,
  from an initial 30 m/s down to 20 m/s.
* The `dat2csv` output — its `Version: %d, OpenDRIVE: %s, 3DModel: %s` line,
  followed by the header

      time, id, name, x, y, z, h, p, r, speed, wheel_angle, wheel_rot

  and then one `%.3f, %d, %s, …` row per sample. The `.dat` recordings it
  decodes stay at format version `2`. `dat2csv <recording>.dat` derives its
  output name from the **basename** of its input and writes `<recording>.csv`
  into the **current working directory** and exits `0` — the tool's output
  lands where the tool was run, not beside the recording it read. The
  `Version:` line is that file's first line and the column header its second,
  so the header is not the CSV's first line.
* **Entity ids are assigned in declaration order starting at `0`.** In both
  `param_cars.xosc` and `two_car_brake.xosc` the first declared entity `Ego`
  therefore has id `0` and the second, `Target`, has id `1`, and the
  index-based `SE_GetId(0)` returns that same `0`. A recording's `name` column
  carries the scenario's own entity names, so decoding a recording of either
  fixture gives a first sample that begins `0.000, 0, Ego, `.
* A successful `esmini` run **exits `0`** — a headless run that loads its
  scenario, plays it to its stop trigger and closes returns zero status. The
  usage banner is the one documented exception (`instruction.md` states its
  status is deliberately unpinned), and a *refused* `--param_permutation` is
  required by `instruction.md` §3 to exit non-zero.
* The `--csv_logger` column header — it still begins, byte for byte,

      Index [-] , TimeStamp [s] , #1 Entitity_Name [-] , #1 Entitity_ID [-] , #1 Current_Speed [m/s] , 

  with the `#N `-prefixed block repeating once per entity, every separator a
  space-comma-space, and `Entitity` misspelled in the two columns that carry it.
  Six banner lines come first, in this order: `esmini GIT REV: `,
  `esmini GIT TAG: `, `esmini GIT BRANCH: `, `esmini BUILD VERSION: `,
  `Scenario File Name: <the file as it was named on the command line>` and
  `Number of Vehicles: <count>` — so a two-entity scenario gives
  `Number of Vehicles: 2`. The values after the four version-line colons depend
  on how the tree was built and are not checked; the line prefixes are.
* The `esmini` usage banner — the `Usage: esmini [options]` line, the
  `Options: ` line, and one two-space-indented line per option, with
  `  --osc <filename>`, `  --headless` and `  --fixed_timestep <timestep>`
  among them. The options this step adds join that list rather than replacing
  it.
* A headless run's console trace — `Loading <file> (v1.0)` for a v1.0
  scenario named on the command line, and one
  `0.000: Init <entity> <action> standbyState -> startTransition -> runningState`
  line (with its matching `runningState -> endTransition -> completeState`) per
  init action.
* `odrplot` with no arguments — `Usage: ordplot openDriveFile.xodr [Output
  file, default=output.csv] [Sampling_step, default=1.0]`, upstream's `ordplot`
  misspelling included.
* Positions read back through a handle render `s` with three decimals
  (`250.000`, `100.000`), so a settable `s` keeps that formatting.

## What the next step receives

Everything under `/app` except `bin/`, `build/`, `cmake_build/`, `.git/` and
the `externals/` subdirectories the task's artifact exclusions list
(`osg`, `osi`, `proj`, `sumo`, `googletest`, `implot`). Your sources, your
CMake changes and anything you vendored under `/app` carry forward; your build
output does not, and the next step's checker rebuilds from source in a fresh
container. A file you leave behind anywhere else under `/app` is visible to
every later step — scratch work and notes are not graded, but they are not
cleaned up for you either.

## What is not in the workspace

The checks, their fixtures and their expected values. The checker mounts its
own corpus at grading time and never reads your source. There is no reference
binary in either container, so nothing in `/app` can be compared against a
"correct" tree — the only thing that is graded is what your build does when it
is run.

The checks use the fixtures below. They live in the verifier and **are not in
your workspace**; they are described here only so you know what shapes of input
your implementation has to handle.

`straight_500m.xodr`, a single 500 m straight road with two lanes;
`e6mini.xodr`, a single 1464 m motorway road with six lanes;
`fabriksgatan.xodr`, a 16-road urban network with one junction;
`two_car_brake.xosc`, a two-entity braking scenario on the straight road;
`param_cars.xosc`, a scenario declaring three parameters — two entity speeds
and a start station — with two entities; and six parameter-value-distribution
files, every one of them written in the dialect `instruction.md` §2 writes
out (an OpenSCENARIO document whose `ParameterValueDistribution` holds the
distributions under a `Deterministic` block, each single-parameter one naming
its parameter with the `parameterName` attribute):

* `dist_range5.xosc` — one `DistributionRange`, five steps: five permutations.
* `dist_set6.xosc` — a two-element `DistributionSet` declared before a
  three-element one: six permutations. The first is on **`EgoSpeed`**, with
  values `12` and `24` in that order; the second is on **`TargetSpeed`**, with
  values `5`, `10` and `15` in that order. `EgoSpeed` is declared first, so by
  §2's ordering rule it varies slowest.
* `dist_mixed.xosc` — a `DeterministicMultiParameterDistribution` holding a
  single `ParameterValueSet`, crossed with a two-element
  `DeterministicSingleParameterDistribution`: two permutations. It carries no
  range.
* `dist_none.xosc` — a `ParameterValueDistribution` with a `ScenarioFile` and
  nothing else: no distribution at all.
* `dist_badrange.xosc` — a `Range` whose `lowerLimit` exceeds its `upperLimit`.
* `dist_malformed.xosc` — not well-formed XML; its elements are never closed.
