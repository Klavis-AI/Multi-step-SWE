# Workspace handoff

## What you receive

An esmini tree at v2.59.0, already configured and building offline and
headless. `USE_OSG`, `USE_OSI`, `USE_SUMO`, `USE_GTEST`, `USE_IMPLOT` and
`USE_PROJ` are all `False`, `FORCE_DOWNLOAD_BINARIES` and `DOWNLOAD_EXTERNALS`
are `False`, and nothing reaches the network during a build. The build produces
`esmini`, `esmini-dyn`, `odrplot`, `dat2csv`, `libesminiLib.so` and
`libesminiRMLib.so`. Because OSG is off, `replayer` and `odrviewer` are not
built; that is deliberate and must stay that way.

Working at this tag: the whole scenario-engine and road-manager public API as
it stands. Everything the earlier steps established is published, exported with
C linkage, and already does what it says — what this step changes is the width
of the numbers it carries, not the set of entry points. The parts this step is
measured against:

- the scenario lifecycle and clock — `SE_Init`, `SE_StepDT`,
  `SE_GetSimulationTime`, `SE_GetSimTimeStep`, `SE_GetNumberOfObjects`,
  `SE_GetObjectState` filling an `SE_ScenarioObjectState`, `SE_Close`;
- the two lookaheads — `SE_GetRoadInfoAtDistance` and the route-aware
  `SE_GetRoadInfoAlongRoute` step 08 added, both filling an `SE_RoadInfo`;
- the motion-state getters — `SE_GetObjectOdometer`,
  `SE_GetObjectAcceleration`, `SE_GetRouteTotalLength`,
  `SE_GetObjectVelocityGlobalXYZ`, `SE_GetObjectAngularVelocity`,
  `SE_GetObjectAccelerationGlobalXYZ`,
  `SE_GetObjectAccelerationLocalLatLong`;
- the reporters — `SE_ReportObjectPos`, `SE_ReportObjectPosXYH`,
  `SE_ReportObjectPosMode`, `SE_ReportObjectRoadPos`, `SE_ReportObjectSpeed`,
  `SE_ReportObjectVel`, `SE_ReportObjectAngularVel`, `SE_ReportObjectAcc`,
  `SE_ReportObjectLateralPosition`, `SE_ReportObjectWheelStatus`, each
  answering `-1` for an object id that does not exist and before a scenario is
  loaded;
- the road-manager core — `RM_Init`, `RM_GetNumberOfRoads`,
  `RM_GetIdOfRoadFromIndex`, `RM_GetRoadLength`, `RM_GetSpeedLimit`,
  `RM_GetRoadNumberOfLanes`, `RM_GetRoadNumberOfDrivableLanes`,
  `RM_GetLaneWidth`, `RM_GetLaneWidthByRoadId`, `RM_GetLaneTypeByRoadId`,
  `RM_Close`;
- road-manager position handles — `RM_CreatePosition`, `RM_SetLanePosition`,
  `RM_SetRoadPosition`, `RM_SetWorldXYHPosition`, `RM_SetWorldPositionMode`,
  `RM_SetS`, `RM_SetH`, `RM_SetHMode`, `RM_PositionMoveForward`,
  `RM_GetPositionData`, `RM_SubtractAFromB` filling an `RM_PositionDiff`, and
  the lookahead pair `RM_GetLaneInfo` / `RM_GetProbeInfo` filling an
  `RM_RoadLaneInfo` and an `RM_RoadProbeInfo`;
- the `.dat` recording format at version 4.3 and the `dat2csv` that reads it —
  its option parser, `--file_refs` with its `Version: <major>.<minor>, ...`
  preamble line, and its refusal of a recording whose major version it does not
  support. That refusal already has a shape, and the instruction keeps it: the
  words `Incompatible DAT major file version` followed by the version found, a
  non-zero exit status, and a `<basename>.csv` left with nothing in it. The CSV
  logger; `odrplot`'s sampled output; and the command line, including naming a
  scenario as a bare positional argument.

**Which parameter lists move, and which do not.** For every entry point in the
list above that the instruction does not re-declare, the parameters, their order
and their count are inherited verbatim — including the trailing `int` and `bool`
arguments the two lookaheads and the two lane/probe queries carry, and the
`SE_RoadInfo *` / `RM_RoadLaneInfo *` destination in the middle of them — and
the only thing this step changes is the width of the numeric ones, `float` to
`double`.

The reporters are the exception, and the instruction is the authority on it:
requirement 4 writes out the parameter list each one carries when the step is
done, and several of those lists are **shorter by one** than the lists the tree
carries today. What goes is the leading `timestamp` argument — the reporter
surface loses it in this release, and the instruction's lists are the ones the
checker compiles against. Concretely, three of them keep their arity and seven
lose the timestamp:

- `SE_ReportObjectPos` becomes `(int, double ×6)` — the id and the six
  coordinates x, y, z, h, p, r, no timestamp;
- `SE_ReportObjectPosXYH` becomes `(int, double ×3)`,
  `SE_ReportObjectPosMode` becomes `(int, double ×6, int)`, and
  `SE_ReportObjectRoadPos` becomes `(int, id_t, int, double, double)`;
- `SE_ReportObjectVel`, `SE_ReportObjectAngularVel` and `SE_ReportObjectAcc`
  take `(int, double ×3)` where the tree takes `(int, float, float ×3)` today;
- `SE_ReportObjectSpeed` `(int, double)`, `SE_ReportObjectWheelStatus`
  `(int, double, double)` and `SE_ReportObjectLateralPosition` `(int, double)`
  keep the parameter count they have today — none of the three carries a
  timestamp — and widen only.

So keeping every parameter the tree carries today is not the conservative
reading: for those seven, the timestamp has to go, and for everything else the
arity stands.

Not present at this tag: 64-bit precision on the numeric surface this step
moves. Across the entry points and structs listed above, the coordinates,
distances, headings, velocities, timestamps, lane widths, curvatures and
lookaheads are 32-bit `float` — struct members, function parameters,
out-parameters and return values alike, with one exception noted below — and
the implementation behind them accumulates in single precision, the simulation
clock included. A caller cannot currently recover more than about seven
significant decimal digits from any of those values.

That is a statement about the surface this step moves, not about the headers as
a whole. `esminiLib.hpp` already carries `double` elsewhere, on entry points
this step does **not** move and you should leave alone: the parameter and
variable accessors (`SE_GetParameterDouble`, `SE_SetParameterDouble`,
`SE_GetVariableDouble`, `SE_SetVariableDouble`), `SE_SetOSITolerances`,
`SE_CropOSIDynamicGroundTruth`, the five `SE_AddCustom*Camera` helpers,
`SE_SimpleGetDistanceToObject` (its `tracking_limit`, `distance` and
`timestamp`), the `timestamp` passed to the `SE_RegisterConditionCallback`
callback, the `dt` of all four `SE_SimpleVehicleControl*` functions — and with
it the control values of the `Analog`, `AccAndSteer` and `Target` three, which
are `double` here already — and the `SE_OverrideActionList` members `value` /
`maxRate` / `maxTorque`.
`esminiRMLib.hpp` declares no `double` at all at this tag.

The exception is worth knowing about because the instruction asks you to remove
it. `esminiLib.hpp` already declares `double SE_GetSimulationTimeDouble()`
alongside the float `SE_GetSimulationTime()`. It is the one 64-bit entry point
that this step's widening supersedes, and it does not survive it: the widened
`SE_GetSimulationTime()` is the single entry point for the clock, so the
duplicate goes. Do not read the paragraph above as "nothing here is 64-bit" —
read it as the surface to widen, minus the one symbol that widens itself out of
existence.

## What you build

The widening of that surface to `double`, in both public headers and in the
implementation behind them, so that a value written in through the API and read
back out again is not rounded to single precision on the way, and so that a
quantity the engine accumulates over many steps does not drift the way a float
accumulator does. The specifics — which members, which signatures, and the
precision that must survive a round trip — are in the instruction.

## Ground rules

- Both public headers are `extern "C"` and stay that way. Every symbol keeps
  the name it has now; this widens existing entry points rather than adding a
  parallel set, so do not leave float-typed duplicates behind.

- Do not change what the engine already does, except where the instruction
  says otherwise. The CSV logger's output, `odrplot`'s output, the log output
  — `[<seconds to three decimals>] [<level>] <message>`, written to standard
  output and to a default `log.txt` in the working directory — and the
  command-line surface the graded runs use (the options listed under "Options
  the graded runs pass" below, and both ways of naming a scenario) all behave
  exactly as they do now, and the published set of artifacts is unchanged.
  "Exactly as they do now" includes the decimal
  widths those outputs print: the CSV logger writes every real-valued field to
  six decimals and `odrplot` echoes its sampling step to two. They are print
  formats, not precision, and the widening leaves them where they are.

- The road manager's position setters signal success with a non-negative return
  code; 0, 1 and 2 all mean the position was set. That convention does not
  change here, and neither does which code means what: **0** is a position set
  on the road and lane asked for, **1** is a position that moved into a new
  road segment, **2** a junction choice and **3** the end of a lane. Setting a
  lane position on a road and lane that exist answers 0; following that
  position forward off the end of its own road answers 1.

- The road manager's *query* entry points that fill an out-parameter are
  stricter, and that does not change either: `RM_GetLaneWidth`,
  `RM_GetLaneWidthByRoadId` and `RM_SubtractAFromB` answer exactly `0` on
  success and a negative code on any error. A zero-width centre lane is a
  success with a width of 0, not an error. Lane *type* is the odd one out:
  `RM_GetLaneTypeByRoadId` returns the type itself, a `Lane::LaneType` bit
  value, with `1` meaning "no lane / reference lane" and `2` meaning a driving
  lane.

- The build must stay offline, headless and reproducible with the CMake options
  listed above. Do not turn a previously-off option on, and do not add a
  dependency that has to be fetched.

## Options the graded runs pass

None of these is new work: they are options the engine already accepts at this
tag. They are listed because the graded runs pass them, so a widening that
changes how they are parsed, or what they do, breaks runs that have nothing to
do with the numeric ABI. `esmini --help` prints the full option list; these are
the ones that are exercised.

- `--osc <filename>` — load the scenario named by the next argument.
- **the bare positional form** — the same scenario file named with no option in
  front of it. `--osc` is the default argument, so `esmini <file>` loads it
  exactly as `esmini --osc <file>` does. **Both forms are exercised, in
  different graded runs**, so neither may stop working; `--help` renders this by
  bracketing the flag, as `  [--osc] <filename>`.
- `--headless` — run without opening a viewer window.
- `--fixed_timestep <seconds>` — advance the clock by a fixed step rather than
  by wall-clock time. The graded runs pass `--fixed_timestep 0.05`.
- `--record <filename>` — write a `.dat` recording of the run to that path.
- `--csv_logger <filename>` — write the per-step CSV log to that path.
- `--help` — print the usage banner and the option list, then exit.

`dat2csv`'s `--file_refs` is named in the recording paragraph above.

`--fixed_timestep` deserves a sentence of its own, because a check can see its
effect directly and a run that omits it does not merely go slower — it produces
a different, unrepeatable answer. Measured on `two_car_brake.xosc`, the ten
second two-entity fixture, at this tag:

- **with `--fixed_timestep 0.05`**: the `--csv_logger` file carries 202 sample
  rows and the `--record` recording decodes to 404 (202 per entity), the run
  finishes in a few hundredths of a second, and repeating it gives the same
  counts every time;
- **without it**: the scenario runs in real time — about ten seconds — and the
  row count tracks how many frames the machine managed. Two consecutive runs
  here produced 8920 and 8731 rows.

Graded runs that record or log assert a row count, so the option is load-bearing
rather than cosmetic.

## Output text the graded runs compare literally

`TEST_INTERFACE.md` §3 says that where a step document names a literal, that
literal is part of the contract and is compared literally. These are step 09's.
Every one is text that `esmini`, `esmini-dyn`, `odrplot` or `dat2csv` already
prints at this tag, and the widening is not supposed to disturb any of it: it
changes the precision *behind* these numbers, not the text around them.

- `esmini --help` prints `Usage: esmini [options]` on a line of its own, and
  lists the scenario option on a line of the form `  [--osc] <filename>` —
  two leading spaces, with the flag shown bracketed because naming the
  scenario positionally is legal. The line's presence and shape are what
  matter here; the brackets themselves are not pinned.
- `esmini-dyn`, run with no arguments, identifies the build on a line beginning
  `esmini BUILD VERSION`.
- The `--csv_logger` file opens with a preamble carrying lines that begin
  `esmini GIT REV: `, `Scenario File Name: ` and `Number of Vehicles: ` — the
  last reading `Number of Vehicles: 2` for the two-entity fixture.
- That file's column header begins
  `Index [-], TimeStamp [s], #1 Entity_Name [-], #1 Entity_ID [-], ` and ends
  `#2 collision_ids, ` — note the trailing comma and space at the end of the
  line, and `Entity_Name` spelled out in full. The separator is a comma
  followed by a space; it is not uniform across the whole header (two of its 64
  commas are followed immediately by the next field name, and none of them has
  a space before it), so reproduce the header as it stands rather than
  regularising it.
- The first sample row of that file, for the two-entity fixture, begins
  `0, 0.000000, Ego, 0, 20.000000, `.
- A run that reaches the end of its storyboard logs
  `storyBoard runningState -> stopTransition -> completeState`.
- `odrplot` announces completion with `Created track.csv using stepsize 1.00`,
  and its `track.csv` carries a lane header row per lane per lane section of
  the form `lane, 1, 0, <lane id>, driving` — or `no-driving` for a lane that
  is not drivable.
- `dat2csv --file_refs` opens the CSV it derives with a preamble line that
  begins `Version: <major>.<minor>, ` and carries, later on that same line, an
  `OpenDRIVE: ` field naming the road network the recording was made against,
  `.xodr` extension intact. The `OpenDRIVE: ` field is not something this step
  changes; the two version numbers are, and R7 fixes what they must become.

Keep all of this exactly as it stands. A comma moved, a field renamed, a word
respelled or a banner reworded fails a graded run even when every number behind
it is correct.

## Fixtures available under the fixtures directory

`straight_500m.xodr` — a single straight 500 m road, id 1, one lane section.
It declares seven lanes, ids 3 down to -3: driving lanes at ±1, border lanes at
±2 (1.68 m), border lanes at ±3 (6 m), and a zero-width centre lane at id 0. All
seven are real to the road manager, and nothing in the file is hidden from it —
but a lane count is only meaningful next to the mask that produced it.
`RM_GetRoadNumberOfLanes` answers 7 for this road under `LANE_TYPE_ANY` and 2
under the drivable default, and `odrplot` writes one `lane` record per lane, all
seven. The `-1` driving lane is 3.07 m wide and the border lane beside it
1.68 m, neither a binary fraction. The plainest network in the corpus.
`e6mini.xodr` — a single motorway road, id 0, whose one lane section carries
seven lanes either side of the zero-width centre lane — fifteen in all, ids 7
down to -7 — mixing driving and non-driving types at differing widths. Three of
the seven on each side are driving lanes (ids 2, 3, 4 and -2, -3, -4), so a
drivable-lane query on this road reports six.
`fabriksgatan.xodr` — an urban network: sixteen roads, an approach and several
connecting roads meeting in a single junction. Road 0 is a little over 93 m
long and carries a 60 km/h speed limit; its driving lanes are 3.5 m wide,
which — unlike straight_500m's 3.07 — a float represents exactly. Road 5 is one
of the connecting roads: about 14.7 m long, the same 60 km/h limit, one
drivable lane at id -1 and 3.5 m wide, beside the usual zero-width centre lane
at id 0. It is short enough that following a position along it soon leaves it,
which is how a caller sees a setter report a road transition rather than a
plain success.
`two_car_brake.xosc` — a two-entity scenario, ten seconds long, in which the
lead vehicle brakes.
`recording_format4.dat` — a recording in the format the engine writes today:
major version 4, minor version 3, the format step 08 established. It is there
so the reader you build can be shown a recording it must refuse.
