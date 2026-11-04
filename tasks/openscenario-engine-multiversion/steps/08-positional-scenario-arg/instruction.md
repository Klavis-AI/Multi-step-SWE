# Step 08 — the recording format, the `dat2csv` command line, and a positional scenario argument

The workspace holds a working ASAM OpenSCENARIO / OpenDRIVE scenario engine that
builds and runs. Your job in this step is to rewrite the binary recording format
and the tool that converts it, to let the scenario filename be given to the
player without naming the option it belongs to, and to add a route-aware
lookahead to the published scenario-engine C ABI.

Read `TEST_INTERFACE.md` first: it states how your tree is built, which six
artifacts are looked for, and how small programs are compiled against your
headers. Nothing in this instruction overrides it.

## 1. The recording format is version 4.3

The `.dat` recording written by `esmini --record <file>` is rewritten. Its
header now begins with two unsigned words — a **major** version followed by a
**minor** version — and those must read **4** and **3**. The old format carried
a single version word and is not a valid recording any more (requirement 5
covers what happens when one is offered).

Beyond the two version words the layout is yours: what is required is that the
writer in the scenario gateway and the reader used by `dat2csv` agree, and that
nothing is lost. A conversion of a recording must contain one row for every
entity at every recorded frame — a run of *n* entities stepped for *k* recorded
frames yields *n · k* data rows.

The conversion's own preamble changes with it:

- By default the first line of the CSV is the column header and nothing
  precedes it. The old unconditional `Version: ...` line is gone.
- With `--file_refs` (requirement 4) one line is emitted *before* the column
  header, reporting both version components and the file references stored in
  the recording header:

  ```
  Version: <major>.<minor>, OpenDRIVE: <odr filename>, 3DModel: <model filename> GIT REV: <rev>
  ```

  It goes to the output file when a file is being written and to stdout under
  `--print_csv`.
- The default (non-extended) column header is exactly

  ```
  time, id, name, x, y, z, h, p, r, speed, wheel_angle, wheel_rot
  ```

## 2. The converted CSV is written next to its input

`dat2csv` chooses its output path from its input path, not from the working
directory:

- With no `--csv`, the output is the **input path** with its extension replaced
  by `.csv`. Converting `some/dir/sim.dat` therefore writes `some/dir/sim.csv`,
  and leaves no `sim.csv` in the current directory. (The previous release kept
  only the base name, so the file always landed in the current directory; that
  is the behaviour being changed.)
- With `--csv <name>`, `<name>` is used verbatim — relative names resolve
  against the current directory — and no default-named file is created
  alongside the input.
- If the output file cannot be created — `--csv` naming a path in a directory
  that does not exist, for instance — say so on the way out, naming the path
  that could not be written, and exit non-zero. Do not create the missing
  directory, and do not fall back to the default output path: a conversion that
  was asked for one destination and silently used another is a worse outcome
  than a refusal.
- `--print_csv` writes the CSV to stdout **instead of** to a file: under it no
  output file is created, not next to the input and not in the working
  directory, and that stays true when `--csv <name>` is also given.
- A conversion that succeeds exits 0.

## 3. `--extended` adds the road-relative columns

`dat2csv --extended` emits five further columns, inserted between `r` and
`speed`, giving the road-manager view of each entity at each frame:

```
time, id, name, x, y, z, h, p, r, roadId, laneId, offset, t, s, speed, wheel_angle, wheel_rot
```

The relative order of the columns is part of the format: the entity identity
columns stay in front, the road-relative block sits between the orientation
block and `speed`, and the extended header is strictly wider than the default
one. The extra columns must carry the recorded road, lane, lane offset, `t` and
`s` of the entity — they are data, not placeholders, so a recording whose
entities move along a road must show them changing. For a straight road whose
reference line starts at the origin with heading zero — `straight_500m.xodr` is
one — that binds the block to arithmetic anyone can check: `s` equals `x`, `t`
equals `y`, `roadId` is the road the entity is on, and `laneId` is the lane it
holds. A padded `--extended` that repeats one constant row is not a conversion.

## 4. `dat2csv` parses a real command line

`dat2csv` stops treating `argv[1]` as the one thing it understands and registers
its options with the same option machinery the player uses, so that it accepts
them in any order and reports what it does not recognise. The options are:

| option | argument | meaning |
|---|---|---|
| `--file` | `<filename>` | the recording to convert; also the **default argument**, so the filename may be given bare (requirement 6) |
| `--csv` | `<name>` | output filename, see requirement 2 |
| `--extended` | — | wider column set, see requirement 3 |
| `--file_refs` | — | emit the version / file-reference line, see requirement 1 |
| `--print_csv` | — | write the CSV to stdout instead of to a file |
| `--help` | — | print the usage block; `-h` does the same |
| `--version` | — | print the build identification and quit |

Required behaviour of the usage and exit statuses:

- The usage block starts with `Usage: dat2csv [options]` and then lists every
  registered option with its argument placeholder, one per entry, the
  default-argument one shown with its option name bracketed — `[--file]
  <filename>` — and the plain ones shown as `--csv <name>`, `--extended`,
  `--print_csv` and so on.
- `--help` and `--version` are answered **before** anything is read, so neither
  needs an input file to be present or valid.
- Help and version report themselves through the exit status: asking for help
  contributes 1, asking for the version contributes 2, and the process exits
  with the sum of what was asked for. So `--help` exits 1, `-h` exits 1,
  `--version` exits 2, and both together exit 3.
- `--version` prints the same build identification the other applications print:
  the `GIT REV`, `GIT TAG`, `GIT BRANCH` and `BUILD VERSION` lines.
- An invocation with no arguments at all prints the usage block and exits -1
  (255 as the shell sees it) rather than mistaking the absence of a filename for
  something it can convert.
- Arguments that match no registered option are reported with
  `Unrecognized arguments:`, **naming the offending argument on a line of its
  own beneath that heading**, followed by the usage block, and exit -1. The
  report is the one the argument parser already emits everywhere else, and the
  one requirement 8 has `dat2csv` reuse. Consuming a bare filename as the
  default argument is not "unrecognised".

## 5. A recording in an older format is refused, not misread

The reader validates the major version of the recording header before it emits
anything:

- A recording whose major version is not the supported one is rejected with a
  message containing `Incompatible DAT major file version`, naming both the
  version found and the version supported, and the process exits -1.
- A file that is not a recording at all — arbitrary bytes whose leading words
  are not the supported major version — is rejected the same way. Never parse
  past a header you did not recognise.
- **Nothing** may be produced for a rejected input. If the output file is opened
  before the header is validated it must be left empty; not a single data row
  and not even a column header may reach it. A partial CSV from a misparsed
  recording is worse than no CSV at all.
- A file that cannot be opened at all is a *different* error, reported with a
  message containing `Cannot open file` and naming the file, and it too exits -1
  (255 as the shell sees it). A missing input and an incompatible input must not
  be conflated.
- Both refusals are argument-independent in one respect: an option the parser
  does not recognise is reported before any input file is touched, so
  `dat2csv --file <missing> --bogus` is an `Unrecognized arguments:` failure and
  not a `Cannot open file` one.

## 6. The scenario filename is a positional argument

The option machinery in `CommonMini` gains the notion of a **default argument**:
one registered option per application may additionally be supplied without
typing its `--name` prefix. `esmini` uses it for `osc`, and `dat2csv` for `file`
(requirement 4).

Upstream spells that by growing a trailing parameter on `SE_Options::AddOption`:

```cpp
void AddOption(std::string opt_str,
               std::string opt_desc,
               std::string opt_arg             = "",
               std::string default_value       = "",
               bool        autoApply           = false,
               bool        isSingleValueOption = true,
               bool        isDefaultArgument   = false);
```

That block is shown so you can follow upstream if you want to, and for no other
reason. **It is not itself required, and cannot be**: `CommonMini.hpp` is not one
of the two headers this task publishes as the interface under test, and no check
in this project reads your source. Reach the behaviour below by whatever internal
means you prefer — a different parameter name, an overload, a separate registration
call, a flag set after the fact. What is required is only the consequences:

- `esmini <scenario.xosc> --headless ...` loads and runs exactly the scenario
  `--osc <scenario.xosc>` would, with the same output and the same exit status.
- The bare filename does not have to lead the command line: it may also sit
  between options, as in `esmini --headless <scenario.xosc> --fixed_timestep
  0.05`. The one position it cannot occupy is directly after the *value* of a
  preceding value-taking option — `--fixed_timestep 0.05 <scenario.xosc>` — where
  the parser has no way to tell a second value from a positional and the run
  fails. Nothing is asked of you there; it is called out only so that the
  promise above is not read as "any position whatsoever".
- `esmini <scenario.xosc> --headless` with no further options also works: the
  bare filename is not something only a longer command line can carry.
- `--osc <filename>` keeps working unchanged. This is a widening, not a
  replacement.
- The help text documents the optionality by bracketing the option name:
  the entry reads `  [--osc] <filename>`, not `  --osc <filename>`.
- The parser still rejects what it does not understand: an unknown option makes
  `esmini` report `Unrecognized arguments` and exit -1, even when a perfectly
  good scenario filename is also on the command line. Everything that is not the
  one default argument still needs its `--name`.
- An invocation with no scenario at all — no `--osc`, no bare filename — is
  still an error and exits -1, which the shell sees as **255**, exactly as the
  no-argument path above does.
- `--help` prints the usage block and exits -1, as before.

The bracket notation is general, not a one-off for `--osc`: it marks every
option whose *value* may be omitted, and this step introduces one.

- `esmini` accepts `--ghost_trail_dt [dt]`, which sets the sample spacing of
  the ghost trail. Its value may be omitted, in which case it is `0.2` s, and
  the usage block says so: the entry reads exactly

  ```
    --ghost_trail_dt [dt]  (default if value omitted: 0.200000)
  ```

  with the description line beneath it. The `dt` is the interval at which the
  engine records a vertex of the ghost's trail.

  The trail is read back through the ghost-trail lookahead, and this step
  fixes its declaration: the checker compiles against exactly the signature
  below, whatever the tree in your workspace declares today, so a declaration
  that stops after `speed_ghost` has to be extended with the trailing
  out-parameter.

  ```c
  int SE_GetRoadInfoAlongGhostTrail(int object_id, float lookahead_distance,
                                    SE_RoadInfo *data, float *speed_ghost,
                                    float *timestamp);
  ```

  The first four arguments are the ones the call already takes: the entity,
  the lookahead distance, the `SE_RoadInfo` to fill in, and the out-parameter
  that receives the ghost's speed at that point. The fifth receives the
  **timestamp** of the point the call resolved — the simulation time at which
  the ghost's own trail passed it. The call reports like the other
  road-information entry points: a non-negative code when it could answer (an
  object id that names nothing is a negative one).

  The `dt` of the option is **not** the spacing of those `timestamp` values:
  the call interpolates along the recorded polyline, so the timestamp it
  returns for a given lookahead distance is a function of the ghost's
  trajectory and of the sampling interval together, and it is not even
  monotone in `dt`.

  Both halves of the option are required, by different means. That it is
  registered, and that the usage block states the default it falls back to,
  are read off `--help` and `docs/commands.txt` as above. That the value
  actually reaches the sampler is checked at runtime and without pinning any
  timestamp: one scenario is run twice with nothing changed but
  `--ghost_trail_dt`, and the trail is read back at the same lookahead
  distances through `SE_GetRoadInfoAlongGhostTrail`. A coarsely sampled trail
  is a different polyline from a finely sampled one, so the two runs have to
  disagree; an implementation that parses the option into a variable nothing
  reads produces two identical runs and fails. Only the disagreement is
  required, and only for the one pair of intervals the checker passes: how far
  apart two trails land is a property of the pair, and pairs that are close
  together do not separate.
- `docs/commands.txt` is the usage block written down, and it is kept in step
  with the rest: the scenario argument is spelled `  [--osc] <filename>` there
  — with the bracket notation requirement 1 introduces — and the entries for
  the options this step adds are added to it, `  --ghost_trail_dt [dt]` among
  them. It is checked by reading that file at that path in your tree — it is a
  published artifact of this project, like `docs/`'s other contents, not an
  internal source file, and the path is part of the contract. Its **total line
  count is not part of the contract** and is not asserted: the reference file
  grows by more entries than this step's own options account for, and a
  candidate cannot be asked to reproduce entries for options this step does not
  ask them to add.

## 7. A route-aware lookahead

`esminiLib` gains a lookahead that follows the route the scenario assigned to an
entity:

```c
int SE_GetRoadInfoAlongRoute(int object_id, float lookahead_distance, SE_RoadInfo *data,
                             int lookAheadMode, bool inRoadDrivingDirection);
```

Same arguments, same `SE_RoadInfo` output struct and same return-code alphabet
as the existing `SE_GetRoadInfoAtDistance`. `lookAheadMode` is the same
three-valued selector the plain call already takes, and it keeps the same
meaning here: `0` measures the lookahead along the centre of the current lane,
`1` along the road's reference line, and `2` along the current lane offset.
Nothing outside `0`, `1`, `2` is defined. The difference between the two calls
is which continuation is probed at a fork:

- `SE_GetRoadInfoAtDistance` follows the road manager's own continuation — the
  most straightforward connection out of a junction.
- `SE_GetRoadInfoAlongRoute` follows the route assigned to `object_id` by an
  OpenSCENARIO `RoutingAction/AssignRouteAction`. Probing past a junction with
  a lookahead long enough to cross it, the filled-in `roadId`, `laneId`, `s` and
  global position must be those of the *route's* successor road. Three entities
  on the same approach road with three different assigned routes must report
  three different successor roads, while the plain lookahead reports the same
  one for all three. Where the route happens to continue along the connection
  the road manager would have picked anyway, the two calls agree.
- Which successor road a route reaches is a property of the road network, not of
  the route text: on `fabriksgatan.xodr` the straight route from the junction's
  approach road continues onto road `14`, the left route onto road `15`, and the
  right route onto road `16`. A probe pushed past the immediate successor lands
  on road `0`, `1` and `3` respectively, the right-hand route holding lane `1`
  by then.
- The return value uses the road manager's `Position::ReturnCode`, exactly as
  the plain call does: `0` OK, `1` entered a new road, `2` made a junction
  choice, and the negative values `-1` generic error, `-2` end of road, `-3` end
  of route, `-4` off road. Values `1` and `2` are *successes*, not failures, and
  the two calls report the same kind of code for the same probe.
- **Which of `0`, `1` and `2` comes back is decided by the last road boundary
  the probe crossed**, so for a given fixture and a given lookahead distance it
  is fixed by the road geometry rather than being a free implementation choice.
  A probe that never leaves the entity's current road reports `0`. A probe whose
  last crossing put it onto a junction *connecting* road reports `2`, because
  getting there meant choosing a connection. A probe that has since left the
  connecting road for the ordinary road on the far side reports `1`. The
  geometry that decides it on `fabriksgatan.xodr` is this: the entity sits on
  road `2` at `s = 280`, road `2` is `304.194` m long, so the junction mouth is
  `24.194` m ahead; the connecting roads `14`, `15` and `16` are `15.475` m,
  `14.865` m and `9.243` m long, so they end `39.669` m, `39.059` m and
  `33.437` m ahead. A **30 m** lookahead is therefore still on the connecting
  road whichever of the three routes is assigned, and **both calls return `2`**;
  a **40 m** lookahead has passed the end of the connecting road on all three,
  and **both calls return `1`**. Those two magnitudes are required exactly, on
  both the plain and the route-following call. (Contrast 7a below, where only
  the *sign* of the end-of-lane code is required; the two obligations are
  different and the note there says which applies where.)
- The entry point is exported from `libesminiLib.so` under its own unmangled
  name, like every other `SE_` entry point.

### 7a. What else the road-information surface gains

The same release that adds the route-aware lookahead reshapes three things
around it. All three are required in this group, because this step has no room
for a group of their own.

- **`model_3d` on the object-injection entry points.** Both calls take the
  name of a 3D model file immediately after `model_id`; `nullptr` means "no
  model", which is what a headless build always passes:

  ```c
  int SE_AddObject(const char *object_name, int object_type, int object_category,
                   int object_role, int model_id, const char *model_3d);
  int SE_AddObjectWithBoundingBox(const char *object_name, int object_type,
                                  int object_category, int object_role,
                                  int model_id, const char *model_3d,
                                  SE_OSCBoundingBox bounding_box, int scale_mode);
  ```

  Each returns the id of the object it created — the next free index, so the
  first add into a two-entity scenario returns `2` — and `SE_GetNumberOfObjects`
  counts it from then on. Both keep C linkage, so the parameter list is the
  only place the change is visible; it is checked by compiling against your
  headers.

- **A lane-type member on the road-information structs.** `SE_RoadInfo` and
  the road manager's `RM_RoadLaneInfo` (and therefore
  `RM_RoadProbeInfo.road_lane_info`) each gain an `int lane_type`, carrying
  the OpenDRIVE lane type of the lane the query landed in, as the same
  `roadmanager::Lane::LaneType` bitmask `RM_GetLaneType` already returns —
  `1 << 1 = 2` for `driving`, `1 << 6 = 64` for `border`. The new member must
  agree with `RM_GetLaneTypeByRoadId` for the same lane. The aggregate
  `LANE_TYPE_ANY_ROAD` also gains `SHOULDER` and `PARKING`; that part is not
  separately required, because with the viewer and OSI switched off nothing in
  this build observes it.

- **An end-of-lane return code from the lookahead.** `RM_GetProbeInfo` and
  `RM_GetLaneInfo` must report failure when the lookahead distance runs past
  the end of the road and there is no successor to continue onto: they return
  a **negative** `Position::ReturnCode` (`ERROR_END_OF_ROAD`, `-2`) instead of
  the `0` that previously came back from a silently clamped probe. The output
  struct is still filled in, clamped to the end of the road, so the code is
  extra information and not a refusal to answer. Only the sign is required here:
  the documents promise a negative code, not a particular one, so a tree that
  returns some other negative value passes. That is deliberately weaker than
  requirement 7's positive codes above, whose magnitudes the fixture geometry
  fixes and which are required exactly.

### 7b. Superelevation rotates the road

A road carrying a `<superelevation>` element is **rotated** about its reference
line, not merely given raised edges. The consequence is that a lane centre's
horizontal offset from the reference line shrinks by `cos(superelevation)`
while its height falls by `sin(superelevation)`: on a road banked `0.1` rad,
the centre of a 3.07 m lane sits at `y = -1.535 * cos(0.1) = -1.52733` and
`z = -1.535 * sin(0.1) = -0.15324`, not at the flat `y = -1.535`. The reported
roll equals the superelevation, and `x`/`s` are unaffected. On a road with no
`<superelevation>` nothing about the coordinates changes.

## What must keep working

Everything the engine already did. In particular the following are re-checked at
this step and none of them moves:

- **Road-manager core**: `RM_Init`, `RM_GetNumberOfRoads`,
  `RM_GetIdOfRoadFromIndex`, `RM_GetRoadLength`,
  `RM_GetRoadNumberOfDrivableLanes`, `RM_CreatePosition`, `RM_SetLanePosition`,
  `RM_SetWorldXYHPosition`, `RM_GetPositionData`, `RM_Close` — including the
  round trip that re-derives road, lane and `s` from a position's own world
  coordinates.
- **Road-manager lane geometry**: `RM_GetLaneWidth`, `RM_GetLaneWidthByRoadId`,
  `RM_GetLaneType`, `RM_GetLaneTypeByRoadId`, with the handle form and the
  road-id form still agreeing, and the centre lane still answering "type none,
  width zero" rather than failing.
- **Scenario lifecycle and state**: `SE_Init`, `SE_StepDT`,
  `SE_GetSimulationTime`, `SE_GetNumberOfObjects`, `SE_GetObjectState`,
  `SE_GetQuitFlag`, `SE_GetPauseFlag`, `SE_ReportObjectWheelStatus`, `SE_Close`,
  and the simple-vehicle model with its deceleration ceiling
  (`SE_SimpleVehicleCreate`, `SE_SimpleVehicleSetMaxDeceleration`,
  `SE_SimpleVehicleControlBinary`, `SE_SimpleVehicleGetState`,
  `SE_SimpleVehicleDelete`). `SE_GetPauseFlag` and
  `SE_ReportObjectWheelStatus` still answer -1 before a scenario is
  initialised, and the wheel report still answers -1 for an object id that
  names nothing.
- **The player's command line**: a headless run with `--headless`,
  `--fixed_timestep` and `--logfile_path` still runs a scenario to its stop
  trigger, logs `Closing` and exits 0; a scenario file that is not there is
  still an error. `--disable_stdout` suppresses console output without
  changing the exit code. `--record <stem>` appends `.dat` to the stem and
  writes the recording there. `esmini --help` prints `Usage: esmini [options]`
  and exits -1.
- **The published artifacts**: `esmini`, `esmini-dyn`, `odrplot`, `dat2csv`,
  `libesminiLib.so`, `libesminiRMLib.so`. All four executables are ELF
  executables and both libraries are ELF shared objects.
- **`esmini-dyn` still identifies itself.** `esmini-dyn` is the
  shared-library-driven player: it loads `libesminiLib.so` at run time, and run
  with no arguments at all it prints the library's identification banner —
  the block whose `BUILD VERSION` line is the one asserted — before it reports
  that it has nothing to run. That banner, and the fact that a bare
  `esmini-dyn` produces it, is the proof that the binary really did find and
  load the library; it is not a claim about the exit status, which is not
  asserted.
- **`odrplot`**: its command line and its console output. Run with no arguments
  it prints its usage text, which opens with the line `Usage: ordplot
  openDriveFile.xodr` — the misspelling of the program's name is upstream's and
  is present at both ends of this step, so it is kept rather than corrected —
  and documents which arguments are optional by bracketing them, as in
  `[Output file, default=output.csv]` and `[Sampling_step, default=1.0]`.
- **`odrplot`** honours both optional arguments: an explicit output filename and
  an explicit sampling step. On success it reports what it did on two lines
  before exiting 0 — a completion line naming the file it wrote and the
  sampling step it used, and a hint naming the same file for the plotting
  script:

  ```
  Created <output file> using stepsize <sampling step, two decimals>
  To plot it, run EnvironmentSimulator/Applications/odrplot/xodr.py <output file>
  ```

  So `odrplot <network> mine.csv` prints `Created mine.csv using stepsize 1.00`
  and `odrplot <network> s5.csv 5.0` prints `Created s5.csv using stepsize
  5.00` — the step is echoed with exactly two decimals.
- **`odrplot`**'s `track.csv` shape: one `lane, <road>, <section>, <id>, <type>`
  row per lane and one sample row per sampling step per lane. Sampling
  `e6mini.xodr`, whose single lane section carries lanes −7…7, is the worked
  example: 15 lane rows for the 15 lanes, 6 of them `driving`; 21975 sample
  rows at the default step of 1.0, and about 4395 at a step of 5.0 — roughly
  five times fewer, the lane inventory unchanged.
- **`odrplot`**'s refusal of inputs that are not OpenDRIVE networks, and of
  files that are not there at all: neither succeeds.
- **`--csv_logger`**: the six-line preamble, the header naming every per-entity
  column, and one row per simulated frame numbered consecutively from zero.

Two carried-over cautions, both still live here:

- The road manager's position setters *and* both lookaheads report `1` and `2`
  in addition to `0` on success. Treat any value `>= 0` as success; `== 0` is
  wrong. (Requirement 7 says which of the three a given probe yields and why.)
- A non-zero exit status is not automatically a failure in this project. Several
  options — `--help`, `--version`, `--return_nr_permutations` — use the exit
  status as their data channel, and requirement 4 adds two more values to that
  list.

## Scope

Do not switch on OpenSceneGraph, OSI, SUMO, PROJ, ImPlot or the unit-test
targets, and do not add anything that downloads at configure or build time. The
build is offline with all of those switched off; see `PROFILE.md`. Because
`USE_OSG` is off there is no viewer and no replayer binary, and nothing in this
step depends on one.

Requirements 1 to 7 above, including 7a and 7b, are what is required. Between
them they cover every behaviour this step asks you to implement, and each one
is checked by at least one case.

A handful of changes that landed in this version range are **out of scope**, and
none of them is required for this step. They are listed here so the
boundary is explicit rather than left to be inferred from the absence of a
check, and each is followed by the reason it cannot be observed from outside
your process with this build profile:

- **Ghost restart timestep** (R08.25). This step does ship a scenario with a
  ghost controller — `ghost_trail.xosc`, used for the runtime half of
  requirement 6 — so the fixture is no longer the obstacle. The measurement is:
  the restart timestep only shows itself in a run whose timestep is *not* the
  `0.05` the old code hardcoded, and every run this step makes is at
  `--fixed_timestep 0.05`, which is that same value. Nothing here separates the
  two behaviours, so R08.25 is out of scope here.
- **The exact ghost trail spacing** (the remaining part of `--ghost_trail_dt`
  in requirement 6). That the option changes the recorded trail *is* required, as
  requirement 6 describes. What is out of scope is any particular timestamp or
  any particular spacing, and the reason is not shyness: the returned value is
  interpolated along the trail polyline, so it is fixed by the ghost's
  trajectory and the sampling interval jointly, it is not monotone in `dt`, and
  it is not derivable from anything these documents publish. Pinning one would
  be pinning an implementation detail, so only the disagreement between two
  intervals is required.
- **`SE_SetSnapLaneTypes` taking effect** (R08.22). Not because the mask does
  nothing — it does. Forcing a world-coordinate re-snap after setting it moves the
  reported lane exactly as you would expect (mask `64` puts the Ego on the border
  lane `-2`, mask `1` on the centre lane `0`, masks `2` and `-1` leave it on `-1`).
  The reason is that it does *the same thing at both ends of this step*: that
  sequence is identical at v2.51.1 and at v2.59.0, so no check on it could tell a
  tree that did this step's work from one that did not. It is also invisible
  without that forced re-snap — setting the mask alone changes nothing the
  scenario itself ever observes. Do the work; nothing here measures it.
- **`SHOULDER` and `PARKING` in `LANE_TYPE_ANY_ROAD`** (R08.28). The aggregate
  is consumed in exactly one place at runtime — marking the outermost lane of
  a section as the road edge — and that flag is read only by the OSI output,
  which is switched off. See 7a.
- **Relative `--path` resolution** (R08.26). `--path` is not asserted anywhere
  in this step, deliberately: it comes from the shared option store rather than
  from any binary under test here, and pinning its behaviour would require the
  wrong component.
- **Replacing a fatal `exit` with an exception** (R08.29). From outside, both
  spellings end the same way: `esmini` still exits non-zero on a load failure,
  and `SE_Init` still returns `-1` to a caller that survives the call, at both
  ends of this step.
- **The `SE_SetDatFilePath` log side effect** (R08.34). The side effect it
  removes is a re-assertion of the log path that was already in force, so the
  log file keeps its name and its contents either way.
- **Deleting `scripts/dat2csv.py` and `scripts/dat.py`** (R08.30). Whether a
  Python helper is present in your tree is not visible through an exit status,
  through stdout or stderr, or through any file a run produces, and checks in
  this project do not read your source. Delete them; nothing here looks.
- **`osc_coverage.txt`** (R08.36) and the viewer-only options R08.14 to R08.16,
  which REQUIREMENTS.md already marks out of scope.

This environment has no network access, and everything the build needs is
already installed. The upstream project's source, its releases, mirrors and
package registries are unreachable, so a copy cannot be fetched by any
route.
