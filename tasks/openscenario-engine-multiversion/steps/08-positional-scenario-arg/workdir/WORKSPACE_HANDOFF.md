# Workspace handoff — step 08

## What you receive

Your `/app` workspace is a working esmini tree at v2.51.1. It configures and builds
offline and headless with the CMake invocation in `TEST_INTERFACE.md`, and that build
produces all six published artifacts today: the executables `esmini`, `esmini-dyn`,
`odrplot` and `dat2csv`, and the shared libraries `libesminiLib.so` and
`libesminiRMLib.so`. The C++ sources are under `EnvironmentSimulator/`, the public
headers are `EnvironmentSimulator/Libraries/esminiLib/esminiLib.hpp` and
`EnvironmentSimulator/Libraries/esminiRMLib/esminiRMLib.hpp`, and the scenario and
road-network corpus is under `resources/`. Nothing is stubbed and nothing is broken:
this step adds capability to a product that already works.

`USE_OSG`, `USE_OSI`, `USE_SUMO`, `USE_GTEST`, `USE_IMPLOT` and `USE_PROJ` are all
`False`, and nothing downloads at configure or build time — there is no network in
either container. Because `USE_OSG` is off there is no viewer and no `replayer`
binary; nothing in this step needs one. `PROFILE.md` describes the machine,
`TEST_INTERFACE.md` describes the contract.

The scenario-engine ABI already publishes, and exports with C linkage, everything the
earlier steps established. Among the parts this step builds on or is measured against:

- the scenario lifecycle — `SE_Init`, `SE_StepDT`, `SE_GetSimulationTime`,
  `SE_GetNumberOfObjects`, `SE_GetObjectState`, `SE_GetQuitFlag`, `SE_GetPauseFlag`,
  `SE_ReportObjectWheelStatus`, `SE_Close`;
- the lookahead this step extends — `SE_GetRoadInfoAtDistance`, filling an
  `SE_RoadInfo` struct;
- the simple vehicle model with its deceleration ceiling —
  `SE_SimpleVehicleCreate`, `SE_SimpleVehicleSetMaxDeceleration`,
  `SE_SimpleVehicleControlBinary`, `SE_SimpleVehicleGetState`,
  `SE_SimpleVehicleDelete`;
- the motion-state queries, the option store, the variable namespace and the
  parameter-distribution entry points from steps 02 through 06.

The road-manager ABI already publishes `RM_Init`, `RM_GetNumberOfRoads`,
`RM_GetIdOfRoadFromIndex`, `RM_GetRoadLength`, `RM_GetRoadNumberOfLanes`,
`RM_GetRoadNumberOfDrivableLanes`, `RM_CreatePosition`, `RM_SetLanePosition`,
`RM_SetWorldXYHPosition`, `RM_GetPositionData`, `RM_GetLaneWidth`,
`RM_GetLaneWidthByRoadId`, `RM_GetLaneType`, `RM_GetLaneTypeByRoadId`, `RM_SetH`,
`RM_SetHMode`, the string road/junction id mapping and `RM_Close`.

What the tree does **not** publish yet is `SE_GetRoadInfoAlongRoute`. On the tool side,
`dat2csv` has no command line to speak of — it reads `argv[1]` as the recording to
convert, derives the output name from the *base* name of that path so the CSV always
lands in the current directory, always prints a one-line `Version: ...` preamble before
the column header, and understands no options at all. The recording format `esmini
--record` writes carries a single version word, and `esmini` requires `--osc` in front
of the scenario filename.

The fixture corpus your build is exercised against lives outside
`/app` — you never see the files, but knowing what is in them tells you what your
implementation has to cope with:

- `straight_500m.xodr` — a single straight road with driving lanes and border lanes
  either side of the centre lane. The plainest network in the corpus.
- `banked_500m.xodr` — `straight_500m.xodr` with a single `<superelevation>` element
  of `a = 0.1` rad applied to its one road, and nothing else changed. The same road,
  lane and `s` can therefore be queried flat and banked and the rotation described in
  requirement 7b measured as a difference between the two. This is the only fixture
  that grades 7b.
- `e6mini.xodr` — a single motorway road with one lane section carrying several
  driving lanes flanked by stop, border and non-driving lanes of differing widths.
  Used for road-geometry sampling and lane-geometry queries. Its lane inventory is
  fifteen lanes in that one section, six of them driving, which is what fixes the
  `odrplot` row counts described under "What carries forward" below.
- `fabriksgatan.xodr` — an urban network: an approach road and several connecting
  roads meeting in a single junction, so a probe pushed past the junction has more
  than one legal continuation.
- `two_car_brake.xosc` — a two-entity scenario on `straight_500m.xodr` in which the
  follower (`Ego`, the first-declared entity, at 20 m/s) brakes towards 5 m/s on a
  timed trigger while the lead vehicle (`Target`, 100 m ahead, at 10 m/s) holds its
  speed, ending on a simulation-time stop trigger. Vehicles are defined inline and the
  road network is referenced by a relative path, so nothing from `resources/` takes
  part. This is the scenario recordings and CSV logs are made from.
- `junction_route_straight.xosc`, `junction_route_left.xosc`,
  `junction_route_right.xosc` — three copies of one scenario on `fabriksgatan.xodr`,
  each parking a single stationary entity on the approach road to the junction and
  assigning it a route through an OpenSCENARIO `RoutingAction/AssignRouteAction`. They
  differ in exactly one thing: the second waypoint of the route, which sends the entity
  straight on, left, or right through the junction. Which successor road each route
  reaches is a property of `fabriksgatan.xodr`, not of the route text: the straight
  route continues onto **road 14**, the left onto **road 15**, and the right onto
  **road 16**. A lookahead long enough to cross the junction therefore leaves the three
  entities on three different roads, while the road manager's own continuation — the
  one the plain, non-route lookahead follows — is the same road for all three of them.
  Push the same three routes further, past the immediate successor, and each lands on
  its own road again: the straight route on road 0, the left on road 1, the right on
  road 3, the last of these holding lane 1 by then.
- `ghost_trail.xosc` — a single-entity scenario on `e6mini.xodr` whose Ego carries an
  inline `FollowGhostController`, so esmini launches a ghost twin with a head start and
  records its path as a "ghost trail". The vehicle and the controller are both declared
  inline and the road network is referenced by a relative path, so nothing from
  `resources/` takes part. The ghost accelerates from a standstill and changes lane
  twice before a simulation-time stop trigger at 12 s, which is deliberate: a ghost
  travelling straight at constant speed traces the *same* polyline no matter how finely
  it is sampled, and a trail read back off it would be identical at every sampling
  interval. This is the only fixture that exercises `--ghost_trail_dt` at runtime.
- `old_v2/sim.dat` — a recording of the two-entity scenario written by the recorder you
  are receiving, in the format you are replacing: a single leading version word, then
  flat per-frame records. It is there so the reader you build can be shown an
  incompatible recording and refuse it.

## What you build

Seven things. All of them are visible from outside the process — through an exported
entry point, a command line, an exit status or a file format — and all of them are
described in more detail in `instruction.md`. Items 6 and 7 each carry a second half
that is graded alongside the headline behaviour; both are called out below and both
are written out in full in `instruction.md`.

**1. The recording format becomes version 4.3.** The `.dat` file written by
`esmini --record <file>` is rewritten around a packet layout. Its header now starts
with two unsigned words, a major version followed by a minor version, and they must
read 4 and 3. Beyond those two words the layout is yours, but the writer in the
scenario gateway and the reader behind `dat2csv` must agree and nothing may be lost:
converting a recording of *n* entities over *k* recorded frames yields *n · k* data
rows. The old unconditional `Version: ...` preamble disappears from the conversion —
by default the first line of the CSV is the column header
`time, id, name, x, y, z, h, p, r, speed, wheel_angle, wheel_rot` and nothing precedes
it.

**2. `dat2csv` writes its output next to its input.** With no `--csv`, the output path
is the *input path* with its extension replaced by `.csv`, so converting
`some/dir/sim.dat` writes `some/dir/sim.csv` and leaves nothing in the current
directory. `--csv <name>` overrides that name verbatim, resolving relative names
against the current directory, and suppresses the default-named file. If the
output path cannot be created, `dat2csv` reports the path, exits non-zero, and
does not fall back to the default location. `--print_csv` writes the CSV to
stdout instead of any file — no file is created even when `--csv` is also given.
A conversion that succeeds exits 0.

**3. `--extended` adds the road-relative columns.** `dat2csv --extended` emits
`roadId, laneId, offset, t, s` between `r` and `speed`, giving the header
`time, id, name, x, y, z, h, p, r, roadId, laneId, offset, t, s, speed, wheel_angle, wheel_rot`.
Those columns carry the recorded road-manager view of each entity at each frame — real
values, not placeholders, so a recording whose entities move along a road must show
them changing. On a straight road that binds the block to arithmetic that can be
checked by hand: `s` equals `x`, `t` equals `y` and `laneId` is the lane the entity
holds. Note that this is a property of the recording, not a promise that every column
moves in every fixture: the two vehicles in `two_car_brake.xosc` hold their lane for
the whole run, so their `laneId`, `offset` and `t` are constant across it while their
`s` advances with their `x`. `roadId` and `laneId` are compared as integers and must
match exactly; every one of the floating-point identities above — `offset == 0`,
`t == -1.535`, `s == x`, `t == y` — is compared with an absolute tolerance of
**0.001**, so the decimal formatting you choose for the column is your business as
long as the value it carries is right to within a millimetre.

**4. `dat2csv` gets a real option parser.** It registers its options with the same
`SE_Options` machinery the player uses, accepts them in any order, and reports what it
does not recognise. The options are `--file <filename>` (also the default argument, see
6), `--csv <name>`, `--extended`, `--file_refs`, `--print_csv`, `--help` (with `-h` as a
synonym) and `--version`. Required behaviour:

- the usage block opens with `Usage: dat2csv [options]` and lists every registered
  option with its argument placeholder, the default-argument one shown bracketed as
  `[--file] <filename>`;
- `--help` and `--version` are answered before any input is read, so neither needs a
  file to be present or valid;
- the exit status carries the answer: asking for help contributes 1, asking for the
  version contributes 2, and the process exits with the sum — so `--help` exits 1,
  `--version` exits 2, and both together exit 3;
- `--version` prints the same build identification the other applications print, the
  `GIT REV`, `GIT TAG`, `GIT BRANCH` and `BUILD VERSION` lines;
- no arguments at all prints the usage block and exits -1, and arguments matching no
  registered option are reported with `Unrecognized arguments:`, naming the argument
  that was not recognised, followed by the usage block, also exiting -1;
- `--file_refs` emits one line before the column header, reporting both version
  components and the file references held in the recording header, in the form
  `Version: <major>.<minor>, OpenDRIVE: <odr filename>, 3DModel: <model filename> GIT REV: <rev>`.
  It goes to the output file when a file is being written, and to stdout under
  `--print_csv`.

**5. An incompatible recording is refused, not misread.** The reader validates the
major version of the header before it emits anything. A recording whose major version
is not the supported one is rejected with a message containing
`Incompatible DAT major file version`, naming both the version found and the version
supported, and the process exits -1. A file that is not a recording at all is rejected
the same way — never parse past a header you did not recognise. Nothing may be produced
for a rejected input: if the output file was opened before the header was validated it
must be left empty, not even a column header in it. A file that cannot be opened at all
is a *different* failure, reported with a message containing `Cannot open file` and
naming the file; a missing input and an incompatible input must not be conflated. Both refusals are argument-independent in one respect: unrecognised options are reported before any input file is touched, so `dat2csv --file <missing> --bogus` is an `Unrecognized arguments:` failure, not a `Cannot open file` one.

**6. The scenario filename becomes a positional argument.** `SE_Options::AddOption` in
`CommonMini` grows a trailing `bool isDefaultArgument` parameter, after
`isSingleValueOption`:

```cpp
void AddOption(std::string opt_str,
               std::string opt_desc,
               std::string opt_arg             = "",
               std::string default_value       = "",
               bool        autoApply           = false,
               bool        isSingleValueOption = true,
               bool        isDefaultArgument   = false);
```

One registered option per application may then be supplied without typing its `--name`
prefix. `esmini` registers `osc` that way and `dat2csv` registers `file`. So
`esmini <scenario.xosc> --headless ...` loads and runs exactly what
`--osc <scenario.xosc>` would, with the same output and the same exit status. The bare
filename may lead the command line or sit between options; the one position it cannot
occupy is directly after the value of a preceding value-taking option (for example
`--fixed_timestep 0.05 <scenario.xosc>`), where the parser has no way to distinguish
a second value from a positional. `--osc` keeps working unchanged — this is a widening, not a replacement. The
help text documents the optionality by bracketing the option name, so the entry reads
`  [--osc] <filename>` rather than `  --osc <filename>`. Everything that is not the one
default argument still needs its `--name`: an unknown option still makes `esmini` report
`Unrecognized arguments` and exit -1 even when a perfectly good scenario filename is
also on the command line, and an invocation with no scenario at all is still an error.

**7. A route-aware lookahead.** `esminiLib` gains

```c
int SE_GetRoadInfoAlongRoute(int object_id, float lookahead_distance, SE_RoadInfo *data,
                             int lookAheadMode, bool inRoadDrivingDirection);
```

exported from `libesminiLib.so` under its own unmangled name. Same arguments, same
`SE_RoadInfo` output and same return-code alphabet as the existing
`SE_GetRoadInfoAtDistance`; the difference is which continuation is probed at a fork.
The existing call follows the road manager's own continuation out of a junction; the new
one follows the route assigned to the object by an
`RoutingAction/AssignRouteAction`. Probed past a junction with a lookahead long enough
to cross it, the `roadId`, `laneId`, `s` and global position it fills in are the route's
successor road — so entities on the same approach road with different assigned routes
report different successor roads, while the plain call reports the same one for all of
them. Where the route happens to continue along the connection the road manager would
have chosen anyway, the two agree. The return value is the road manager's
`Position::ReturnCode`: `0` OK, `1` entered a new road, `2` made a junction choice, and
negatives for the error cases (`-1` generic, `-2` end of road, `-3` end of route, `-4`
off road). Which of the three non-negative codes comes back is not a free choice: it
names the last road boundary the probe crossed, so a probe still on the entity's own road
gives `0`, one that has reached a junction connecting road gives `2`, and one that has
left the connecting road for the road beyond gives `1`. The checker pins both magnitudes
on the junction fixtures — see "Values the checker pins" below.

Requirement 7 also reshapes three things around that lookahead, and adds one road
geometry fact, all of which are graded:

- `SE_AddObject` and `SE_AddObjectWithBoundingBox` take a `const char *model_3d`
  immediately after `model_id`. Both keep C linkage, so the widened parameter list
  is the only trace of the change — it is checked by compiling against your headers,
  not by reading the symbol table. Each returns the id of the object it created,
  which is the next free index, and `SE_GetNumberOfObjects` counts it afterwards.
- `SE_RoadInfo` and the road manager's `RM_RoadLaneInfo` each gain an
  `int lane_type` carrying the OpenDRIVE lane type of the lane the query landed in,
  as the same bitmask `RM_GetLaneType` already returns (`2` driving, `64` border).
  It must agree with `RM_GetLaneTypeByRoadId` for the same lane.
- `RM_GetProbeInfo` and `RM_GetLaneInfo` return a negative code when the lookahead
  runs past the end of the road with no successor to continue onto, rather than the
  `0` a silently clamped probe used to return. The struct is still filled in.
- A road with a `<superelevation>` element is rotated about its reference line, not
  merely given raised edges, so a lane centre's horizontal offset shrinks by
  `cos(superelevation)` while its height falls by `sin(superelevation)`.

Two conventions run through all of this and are easy to get wrong:

- **A non-negative return is success, not just zero.** The road manager reports `1` and
  `2` from position setters and from both lookaheads to say "entered a new road segment"
  and "made a junction choice". Those are successes. Code that tests `== 0` is wrong
  here and stays wrong for the rest of the task.
- **A non-zero process exit is a data channel, not necessarily an error.** `--help`,
  `--version` and `--return_nr_permutations` all report their answer through the exit
  status, and requirement 4 above adds two more values to that list. Do not make every
  tool exit 0 "for consistency".

## What carries forward

Your tree is at v2.59.0 when this step is done. The build profile is unchanged: still
offline, still headless, still `USE_OSG`, `USE_OSI`, `USE_SUMO`, `USE_GTEST`,
`USE_IMPLOT` and `USE_PROJ` all `False`, still nothing downloading at configure or
build time. The same CMake invocation must keep working — preserve build stability, the
next step starts from the tree you leave behind.

The six published artifacts keep their names and keep coming out of the build:
`esmini`, `esmini-dyn`, `odrplot`, `dat2csv`, `libesminiLib.so`, `libesminiRMLib.so`.
`esmini-dyn` is the one of those that is easy to break by accident: it is the
shared-library-driven player, so it loads `libesminiLib.so` at run time rather than
linking it in, and it is checked by running it with no arguments and looking for the
library's identification banner — the block carrying the `GIT REV`, `GIT TAG`,
`GIT BRANCH` and `BUILD VERSION` lines — in its output. If the banner is missing, the
binary did not find the library.
Two of their outputs are **byte-identical** across this step and are re-checked as such:
`odrplot`'s sampled CSV for a given network and sampling step, and the table
`esmini --csv_logger` writes for a given scenario and fixed timestep — the six-line
preamble, the header naming every per-entity column, and one row per simulated frame
numbered consecutively from zero. If either of those files changes, something regressed;
they are not part of what this step rewrites. The `.dat` recording and the CSV
`dat2csv` derives from it are the *only* file formats this step is allowed to move.

`odrplot`'s own console report is part of that unchanged behaviour. On success it
prints two lines — `Created <output name> using stepsize <step to two decimal places>`,
then a hint naming the same file — and for `e6mini.xodr` the counts it produces are
fixed by the fixture rather than by the run: fifteen lanes in one lane section, six of
them driving, and one sample row per metre per lane, so 21975 sample rows at the
default step of 1.0. Nothing in this step changes either number.

### Values the checker pins

Everything in this subsection is a *consequence* of behaviour described elsewhere in
this document or in the step instruction — nothing here is a new obligation. It is
written down because the checker pins these exact values, and a solution that got the
behaviour right but the arithmetic or the spelling wrong would fail on a number it was
never shown.

**The Ego's speed under the fixture's own braking.** The braking the fixture
declares is a *linear rate* action of 3 m/s² triggered on simulation time
greater than 3 s, and the checker reads the Ego's speed through
`SE_GetObjectState` 4.000 s into the run — after 80 steps of 0.05 s. The
deceleration is in force from the first step past its trigger, 0.05 s later, so
by the 4.000 s read it has applied for 0.95 s and the reported speed is

    20 − 3 × 0.95 = 17.15 m/s

compared with a tolerance of `±0.05`. The arithmetic is the assertion: the
trigger semantics decide which step the action begins integrating on, and a tree
that starts it one 0.05 s step later reports 17.30. The Target holds its
commanded 10 m/s throughout, so the pair's separation follows from the two
speeds and the 100 m initial gap.

**The frame count of the reference run.** `two_car_brake.xosc` at
`--fixed_timestep 0.05` runs from `0.000` s to `10.050` s inclusive. That is 201
intervals and therefore **202 sampled instants**, and the scenario has 2 entities. Every
row count in the step follows from that one pair of numbers:

| Artifact | Lines | Made of |
|---|---|---|
| `dat2csv` CSV (plain or `--extended`) | 405 | 1 header + 2 x 202 = 404 data rows |
| `dat2csv --print_csv` on stdout | 405 | the same table, not written to a file |
| `esmini --csv_logger` CSV | 209 | 6 preamble + 1 header + 202 frame rows |

`--extended` adds columns, never rows: its data-row count is 404 as well. The logger's
frame rows are numbered consecutively `0` .. `201`.

**How `dat2csv` renders a row.** Fields are separated by a comma *and a space* (`", "`,
not a bare comma), and `time` carries exactly three decimals — the first data row of the
reference run begins `0.000, 0, Ego,` and the last begins `10.050, 1, Target,`. The
header spelling `time, id, name, ...` is given under requirement 2.

**`--csv_logger` column names.** The six preamble lines are, in order, `esmini GIT REV:`,
`esmini GIT TAG:`, `esmini GIT BRANCH:`, `esmini BUILD VERSION:`, `Scenario File Name:`
and `Number of Vehicles:`. Line 7 is the header. Column names carry their unit in square
brackets and the per-entity ones are prefixed `#N `: the first two columns are
`Index [-]` and `TimeStamp [s]` in that order, and each entity contributes
`#N Entity_Name [-]`, `#N Entity_ID [-]`, `#N Current_Speed [m/s]`,
`#N World_Position_X [m]`, `#N World_Position_Y [m]`, `#N World_Heading_Angle [rad]` and
`#N lane_id`, entity 1's block before entity 2's. The block is **not** limited to those
seven and they are **not** in that order: each entity contributes 31 columns, of which the
checker pins the seven named above by name only, making no assumption about where in the
block they sit. (For the reference two-entity run the header is 2 + 2 x 31 = 65 columns
wide, and `#1 lane_id` is column 25 while `#1 World_Heading_Angle [rad]` is column 27 --
`lane_id` comes first, the reverse of the reading order above.) The only positions pinned
anywhere are `Index [-]` and `TimeStamp [s]` as columns 1 and 2.

**Route-lookahead return codes.** On the junction fixtures the entity stands on road `2`
of `fabriksgatan.xodr` at `s = 280`. Road `2` is `304.194` m long, so the junction mouth
is `24.194` m ahead, and the connecting roads `14`, `15` and `16` are `15.475` m,
`14.865` m and `9.243` m long. A **30 m** lookahead therefore still lands on the
connecting road on all three routes and both `SE_GetRoadInfoAtDistance` and
`SE_GetRoadInfoAlongRoute` return **`2`**; a **40 m** lookahead has left it on all three
and both return **`1`**. Both magnitudes are pinned exactly. The *negative* end-of-road
code from `RM_GetProbeInfo` / `RM_GetLaneInfo` is a separate obligation and is pinned by
sign only.

**Lane-type bitmask values.** The `lane_type` field and `RM_GetLaneType*` report
`roadmanager::Lane::LaneType`, which is a bitmask: `1` none, `2` driving, `4` stop,
`8` shoulder, `64` border. Two consequences are pinned. Lane `0` of `straight_500m.xodr`
is reported as **`1` (none) with width `0`** even though the file declares it `driving`
— the road manager refuses to treat a centre lane as driveable. And lane `-5` of
`e6mini.xodr` is declared `type="stop"`, so it reports **`4`**, with a width of 2.85 m.
There is no shoulder lane anywhere in the corpus.

**`odrplot`'s lane rows.** Each lane row in `track.csv` ends in the lane's drivability as
one of exactly two spellings: `driving`, or **`no-driving`** (one hyphen, no space) for
everything else. For `e6mini.xodr` that is 15 rows, `lane, 0, 0, <id>, <spelling>`, ids
`-7` .. `7`, each id appearing exactly once.

**The log's grammar.** Five lines of the log file are not log entries at all and do not
carry the entry grammar: a four-line build banner at the very top (`esmini GIT REV:`,
`esmini GIT TAG:`, `esmini GIT BRANCH:`, `esmini BUILD VERSION:`) and, a couple of lines
further down, one bare wall-clock line of the form `[2026-01-01 00:00:00]`. Every other
line is a log entry, and every log entry is
`[<stamp>] [<level>] <message>`. `<level>` is a lower-case word (`info`, `warn`,
`error`). `<stamp>` is the simulation time to exactly three decimals — `[0.000]` — once
the scenario is running, and **empty** (`[]`) for entries written before simulation time
exists, such as loading the scenario or echoing the resolved option list. Both forms
appear in a single headless run. The default log file is `log.txt` in the working
directory; `--logfile_path` redirects it and must then leave no `log.txt` behind.

Everything the earlier steps established is still expected to work and is re-checked
here: the road-manager core and its position round-trip, lane width and lane type by
both the handle form and the road-id form, the scenario lifecycle with the pause and
wheel-status queries answering -1 before a scenario is initialised, the simple-vehicle
deceleration ceiling, and a headless run with `--headless`, `--fixed_timestep` and
`--logfile_path` that reaches its stop trigger, logs `Closing` and exits 0 — with a
missing scenario file still an error.

Every fixture named above is still present and still used at v2.59.0:
`straight_500m.xodr`, `banked_500m.xodr`, `e6mini.xodr`, `fabriksgatan.xodr`,
`two_car_brake.xosc`, the three `junction_route_*.xosc` scenarios, `ghost_trail.xosc`,
and `old_v2/sim.dat` — which by then is a recording your own reader must refuse rather
than one it can read.
