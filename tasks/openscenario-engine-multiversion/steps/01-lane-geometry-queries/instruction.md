# Step 1 — road-manager lane geometry and overlap queries

The workspace holds a working ASAM OpenSCENARIO / OpenDRIVE scenario engine
that builds and runs. Your job in this step is to grow its two published C
ABIs so that the road network can be interrogated directly, without going
through a scenario, and so that a host application can report externally
computed wheel state back into the engine.

Read `TEST_INTERFACE.md` first: it states how the checker builds your tree,
which six artifacts it looks for, and how it compiles small programs against
your headers. Nothing in this instruction overrides it.

## 1. Lane width, by position handle and by road id

`esminiRMLib` must expose the width of a lane, in metres, through two entry
points:

```c
float RM_GetLaneWidth(int handle, int lane_id);
float RM_GetLaneWidthByRoadId(int road_id, int lane_id, float s);
```

- `RM_GetLaneWidth` answers for a position handle previously obtained from
  `RM_CreatePosition` and placed with one of the existing position setters; the
  road and the longitudinal offset come from the handle, the lane comes from
  the argument.
- `RM_GetLaneWidthByRoadId` answers the same question with no handle at all,
  taking the road and the longitudinal offset directly.
- Asked about the same road, lane and `s`, the two must return the same number.
- The centre lane (`lane_id` 0) has width 0. That is an answer, not an error.

Widths come from the network's `<width>` entries and must follow them: a road
whose seven lanes have seven different widths must report seven different
numbers.

A `<width>` entry is a cubic in the distance from the start of **its own lane
section**, not from the start of the road:

    w(ds) = a + b*ds + c*ds^2 + d*ds^3,   ds = s - (laneSection s) - sOffset

so all four coefficients have to be evaluated, and the offset they are
evaluated at is the section's. Where a lane section declares `b = c = d = 0`
the width is constant across that section and the same `s` sweep must return
the same number every time; where it declares any of them nonzero the returned
width has to change along `s` exactly as the polynomial says. A road with more
than one lane section restarts `ds` at each section boundary. Answering with
the constant term alone, or measuring `ds` from the road origin, is wrong on a
network of that kind, and the checker uses one.

Widths are metres and are compared as numbers: an answer must be right to
within a millimetre of what the network's coefficients give, not merely the
right order of magnitude.

Nothing is thrown and no error code is reserved. A `lane_id` the road does not
have, a `road_id` the network does not contain, and a handle that
`RM_CreatePosition` returned but no setter has ever placed all answer **0.0**
— the same answer the centre lane gives. 0 is simply "no width here"; it is
not a distinguishable failure signal and callers are not given one.

## 2. Lane type

```c
int RM_GetLaneType(int handle, int lane_id);
int RM_GetLaneTypeByRoadId(int road_id, int lane_id, float s);
```

Both return the lane's OpenDRIVE type as the bit value the road manager uses
internally for `Lane::LaneType`. The values the checker relies on are the ones
already in that enumeration:

| lane type in the network | returned value |
|---|---|
| `none` | 1 |
| `driving` | 2 |
| `stop` | 4 |
| `border` | 64 |

As with the widths, the handle form and the road-id form must agree.

The centre lane (`lane_id` 0) always reports `none` (1), regardless of what
the network's `<lane id="0">` element declares for `type`: the road manager
normalizes the reference line's type on load, and a lookup that trusts the
file's declared attribute for lane 0 disagrees with it.

Unlike the widths, these two do have room for a failure answer, because every
value in the table above is positive. A `lane_id` the road does not have, a
`road_id` the network does not contain, and a handle that was never placed all
report a **negative** value. Which negative value is yours to choose — the
checker asserts the sign, not the magnitude — but it must not be one of the
four table entries, and it must not be 0.

## 3. Roads overlapping a position

```c
int RM_GetNumberOfRoadsOverlapping(int handle);
int RM_GetOverlappingRoadId(int handle, int index);
```

These report which roads' paved surface actually covers the world `(x, y)`
the handle currently holds — not which road `RM_GetPositionData` names as the
position's road. The two questions differ: a position set from world
coordinates always resolves to *some* road (whichever the manager judges
nearest), even at a point no road's surface reaches at all; `RM_GetPositionData`
still names that nearest road, but `RM_GetNumberOfRoadsOverlapping` correctly
reports 0 there.

"Paved surface" is the network's own lanes, not a band around a reference
line: a road covers a point when the point falls inside the lane block that
road's `<laneOffset>` and its lanes' `<width>` entries place on the ground —
the offset is part of where the lanes are — and only within that road's own
`s` range. A point that does so for several roads is covered by several, which
is the junction case below; a point on a road's reference line but outside its
lanes is not covered by that road.

- Inside a junction, several connecting roads' surfaces cover the same
  ground: the count is how many roads are actually under the point, and
  `RM_GetOverlappingRoadId` returns their ids at index `0 .. n-1`, in an
  order this contract does not fix.
- On a plain road, at a point that is on that road's own surface, the count
  is 1 and index 0 is that road.
- At a point no road's surface reaches, the count is 0 — even though
  `RM_GetPositionData` still reports a nearest road there.
- An `index` outside `0 .. n-1` returns `-1`. It must not return a stale id
  and it must not crash.

The count and the ids are one statement, not two, and the checker reads them
together:

- **Every** index in `0 .. n-1` answers. If the count says `n`, then `n`
  successive calls each produce an id; a call that gives up part way through
  the declared range is a failure even though the ids it did produce were
  right.
- **Every id it answers with is a road the loaded network contains** — one of
  the ids `RM_GetIdOfRoadFromIndex(0 .. RM_GetNumberOfRoads()-1)` enumerates
  for the same network. The checker reads the network's inventory back through
  those two inherited entry points and requires each returned id to be in it,
  so an id synthesised from an internal index, a table position or a connector
  slot is caught even when the count is right.
- **No in-range index answers `-1`.** `-1` is reserved for the out-of-range
  case above; returning it for a valid index is how an implementation that
  reports a count it cannot actually service shows up.

Handles are not validated here either: what these two answer for an `int` that
`RM_CreatePosition` never returned is not part of this contract and is out of
scope.

## 4. Moving a position handle to a named road

```c
int RM_SetRoadId(int handle, int roadId);
```

Re-anchors an existing handle onto the road with the given id, keeping its
world coordinates and re-deriving the road-relative ones. Returns 0 on
success.

The point of the call is that it overrides the road manager's own choice: a
position inside a junction resolves to whichever connecting road is closest,
and this is how a caller says "interpret it against that road instead". The
world `x` and `y` do not move — they are preserved to the millimetre, and the
checker compares them before and after at that precision.

What the call does with a `roadId` the network does not contain is **not
specified here and is out of scope**: upstream at this release neither moves the
position nor reports the problem, and pinning that would turn an accident into
a requirement. Give it whatever answer you find natural. The same goes for a
`handle` that `RM_CreatePosition` never returned.

## 5. Externally reported wheel state

`esminiLib` must accept wheel state computed outside the engine:

```c
int SE_ReportObjectWheelStatus(int object_id, float rotation, float angle);
```

- Returns 0 when `object_id` names an entity in the running scenario.
- Returns -1 when no scenario has been initialised yet.
- Returns -1 when `object_id` names nothing.
- The reported `rotation` and `angle` are retained, not discarded once the
  return code is decided: a recording taken with `--record` (or `SE_Init`'s
  own `record` flag) and converted with `dat2csv` carries the last-reported
  values back out in the `wheel_rot` and `wheel_angle` columns for that
  object, sample by sample. `SE_ScenarioObjectState` itself has no wheel
  field — the recording is the only place this is observable.

## 6. Pause state query

```c
int SE_GetPauseFlag();
```

Returns 0 when a scenario is loaded and is not paused, and -1 when no scenario
has been initialised. There is no option to pause a headless run at this
release, so 0 is the only value a loaded scenario reaches.

## 7. A deceleration limit on the simple vehicle

```c
void SE_SimpleVehicleSetMaxDeceleration(void* handleSimpleVehicle, float maxDeceleration);
```

The built-in simple vehicle model gains a ceiling on how fast braking may shed
speed, in m/s². After the call, driving the vehicle with a full brake through
`SE_SimpleVehicleControlBinary` must not reduce its speed by more than
`maxDeceleration * dt` in a step of `dt` seconds. The ceiling is a parameter:
setting it four times larger must shed speed four times faster over the same
elapsed time.

The ceiling *is* the braking rate, not a second cap layered over a rate the
model already had: with the limit in force a full brake sheds exactly
`maxDeceleration * dt` in every step — the limit is reached, not merely
respected — until the speed would cross zero, where the vehicle settles at
standstill rather than going negative. So after `n` steps of `dt` from a
starting speed `v`, and while `v - maxDeceleration * dt * n` is still positive,
that expression is the speed, and the same is true at every value of
`maxDeceleration` a caller chooses. A braking step that would carry the speed
below zero leaves it at exactly 0 instead, and holds it there: going on
braking from standstill keeps the speed at 0 rather than running the vehicle
backwards. Speeds are m/s and decelerations m/s², and the checker compares
both to within 0.05 of the value those expressions give.

The state of the model *before* the call — what ceiling, if any, the simple
vehicle brakes at when `SE_SimpleVehicleSetMaxDeceleration` has never been
invoked on it — is inherited behaviour that this step neither changes nor
brings into scope. Every obligation above begins at the call.

## 8. Widened return codes from a position move

```c
int RM_PositionMoveForward(int handle, float dist, float junctionSelectorAngle);
```

This entry point already exists and already advances a position handle `dist`
metres along its current road, steering into `junctionSelectorAngle` when it
reaches a junction. What this step adds is a report of *what kind* of move
happened, in the return value:

| the move | returns |
|---|---|
| stayed inside the road the handle was already on | `0` |
| carried the position onto a new road segment, an unambiguous continuation with no choice to make | `1` |
| carried the position into a junction, where a connecting road had to be chosen | `2` |

All three are success. The widening *adds* values, it does not retire `0`: a
move that changes nothing about which road the handle is on still returns `0`.
Failures stay negative, and the negative codes the entry point already returned
keep their meanings and their magnitudes.

## What must keep working

Everything the engine already did. In particular the checker re-runs, at this
step, obligations the workspace already satisfies:

- the existing road-manager queries — `RM_Init`, `RM_GetNumberOfRoads`,
  `RM_GetIdOfRoadFromIndex`, `RM_GetRoadLength`, `RM_GetRoadNumberOfLanes`,
  `RM_CreatePosition`, `RM_SetLanePosition`, `RM_SetWorldXYHPosition`,
  `RM_GetPositionData`, `RM_Close` — and the `RM_PositionData` struct
  `RM_GetPositionData` fills, whose `roadId`, `laneId`, `junctionId`, `s`,
  `x`, `y` and `h` members are all read: `junctionId` is `-1` outside any
  junction and the junction's own id inside one;
- the existing scenario-engine entry points the new ones are used alongside —
  `SE_Init` (with its five arguments, the last of which is the `record` flag),
  `SE_Step`, `SE_Close`, and the simple-vehicle set `SE_SimpleVehicleCreate`,
  `SE_SimpleVehicleControlBinary`, `SE_SimpleVehicleGetState` and
  `SE_SimpleVehicleDelete`, together with the `SE_SimpleVehicleState` struct
  and its `speed` member in m/s. These are inherited, not new; they are named
  because the checker calls them by name and links against them, so removing
  or renaming one is a regression whether or not this step's own work is
  complete;
- running a scenario headless from the command line to its stop trigger:
  `--osc <file>` selects the scenario, `--headless` suppresses the viewer and
  `--fixed_timestep <dt>` drives the run at a fixed step, and
  `--logfile_path <path>`, `--csv_logger <path>` and `--record <path>` are the
  three output switches;
- the `.dat` recording format and the `dat2csv` column header — and where the
  output lands: `dat2csv <file>.dat` writes `<file>.csv` into the **current
  working directory**, not beside the `.dat` it read, and a recording asked for
  through `SE_Init`'s own `record` flag is named from the scenario file's
  basename, in the current working directory too;
- `odrplot`'s `track.csv` output, written under that fixed name into the
  current working directory whatever the input network is called.

One caveat that applies from this step onward: a road-manager call that
returns an `int` status signals success with **any non-negative value**, not
only with `0`. Section 8 is where this step makes that concrete — a position
*move* reports **1** for "moved into a new road segment" and **2** for "moved
into a junction and made a choice" in addition to **0** for plain success. The
position *setters* (`RM_SetLanePosition`, `RM_SetWorldXYHPosition`,
`RM_SetRoadId`) return a plain `0` at this release and are not where the
widened codes come from; later releases extend the same convention to them,
which is exactly why callers must not narrow it. Treat any value `>= 0` from
any of them as success. Code that tests `== 0` is wrong here and stays wrong
for the rest of the task.

## Exact output that must not change

"Everything the engine already did" includes the bytes the tools print and the
statuses they exit with. The checker reads the following literally. None of it
is something this step asks you to add or alter; it is listed so that "do not
change it" is a written obligation rather than something you have to infer.

**Exit status.** A headless run that reaches its stop trigger exits **0**.
`esmini` exits **non-zero** when the file given to `--osc` does not exist: a
missing scenario file is an error, never a silent success. `odrplot` exits
**non-zero** on an input that is not an OpenDRIVE network.

**A scenario shutting down** logs the line `Closing`.

**The log.** A run writes a log file. Left alone it is `log.txt` in the
current working directory; `--logfile_path <path>` — that spelling, not
`--logfile` — redirects it to the given path instead, and is the only option
that does. Each entry is one line formatted `%.3f: %s`: the
simulation time to three decimals, then a colon, then a space, then the
message. The same entries go to stdout, which is where the final `Closing` is
visible even when the log has been redirected elsewhere. (`--disable_log` and
`--disable_stdout` suppress the file and the stdout copy respectively; both
exist at this release, neither changes the format, and no check here passes
either.)

There is **no command-line option at this release that changes that entry
format**, and in particular there is no `--trace`: `esmini --trace` is
rejected with `Unrecognized arguments:`. A second, longer entry format does
exist inside the logger, selected by the call site that emits the entry rather
than by anything a caller can ask for, so no invocation the checker can make
reaches it and no check looks for it. Reproducing the logger's internal
structure is not part of this step; reproducing the one line format above,
the default file name and the one option that redirects it, is.

**`esmini --help`** prints a usage line beginning `Usage: esmini [options]`,
then one line per option indented by two spaces, `  --osc <filename>` among
them. Its *exit status* is deliberately not part of this contract — upstream
returns a non-zero status for `--help` at this release and the checker does not
pin that number.

**`esmini-dyn` with no arguments** prints the standard build banner, which
includes a line matching `esmini BUILD VERSION: `.

**`--csv_logger <path>`** writes six preamble lines, in this order:

    esmini GIT REV: <rev>
    esmini GIT TAG: <tag>
    esmini GIT BRANCH: <branch>
    esmini BUILD VERSION: <version>
    Scenario File Name: <path>
    Number of Vehicles: <n>

then one column-header line that begins, byte for byte,

    Index [-] , TimeStamp [s] , #1 Entitity_Name [-] , #1 Entitity_ID [-] , #1 Current_Speed [m/s] , 

and continues with `#1 Wheel_Angle [deg] , `, `#1 Wheel_Rotation [-] , `, the
world position, velocity and acceleration columns, and `#1 collision_ids , `,
the whole `#N `-prefixed block repeating once per entity. Every separator is a
space, a comma and a space, each block ends with a trailing `, `, and
**`Entitity` is misspelled** in the two columns that carry it. The misspelling
is part of the format: correcting it changes the format and is a regression.
One sample row follows per simulated step.

**`dat2csv`** with no argument prints `Usage: <program> <filename>` and exits
non-zero. Given a `.dat`, it writes a first line
`Version: 2, OpenDRIVE: <network file>, 3DModel: <model file>` and then the
column header, byte for byte:

    time, id, name, x, y, z, h, p, r, speed, wheel_angle, wheel_rot

**`odrplot`** prints `Created track.csv using stepsize 1.00` on a successful
run at the default sampling distance, then a second line
`To plot it, run EnvironmentSimulator/Applications/odrplot/xodr.py track.csv`,
naming the file it just wrote. Invoked with no arguments it prints
`Usage: ordplot openDriveFile.xodr [Output file, default=output.csv] [Sampling_step, default=1.0]`
— `ordplot` is misspelled there and, like
`Entitity` above, the misspelling is part of the string. Its `track.csv`
carries one `lane, <road>, <section>, <lane>, driving` or `..., no-driving`
line per lane per lane section, each followed by one `x, y, z, h` sample line
per sampling step along that lane.

## Scope

Do not switch on OpenSceneGraph, OSI, SUMO, PROJ, ImPlot or the unit-test
targets, and do not add anything that downloads at configure or build time.
The checker builds offline with all of those switched off; see `PROFILE.md`.

This environment has no network access, and everything the build needs is
already installed. The upstream project's source, its releases, mirrors and
package registries are unreachable, so a copy cannot be fetched by any
route.
