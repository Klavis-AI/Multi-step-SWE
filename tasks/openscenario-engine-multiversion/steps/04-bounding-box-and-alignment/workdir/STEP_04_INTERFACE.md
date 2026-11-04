# Interface contract — step 04

This document describes the published interfaces **as they already stand in the
tree you received at the start of step 04**. Everything here is inherited: it
is what v2.27.5 already builds and exports, and none of it is a task. Your
task is `instruction.md`; this file exists so that you never have to guess the
name, the argument order, the field spelling or the success convention of an
existing entry point that `instruction.md` refers to but does not repeat.

Three rules govern the whole surface:

- **Failure is reported by SIGN, not by magnitude.** Every entry point below
  that returns an `int` status reports failure with a *negative* value.
  Nothing in this package promises which negative value, and no check anywhere
  requires a particular one: refusals are graded on the sign alone. Do not
  write code that depends on a specific error code.
- **Success, on this build, is exactly `0` — and the checker requires it.**
  The rule above is about failures; successes are not left open in the same
  way, and it would be misleading to imply they are. Every entry point in this
  document that returns a *status* returns `0` on success as the tree you
  received already builds it, and because none of it may change during step 04
  (see the next rule) the checker pins that `0` exactly rather than merely
  requiring non-negativity. Concretely, `SE_Init`, `SE_GetObjectState`,
  `SE_ReportObjectPos`, `SE_ReportObjectPosXYH`, `SE_GetRoadInfoAtDistance`,
  `SE_DeleteObject`, `SE_GetDistanceToObject` and `RM_Init` are each asserted
  as `== 0` on success by cases in this step. This is not a new requirement — it is the value your tree already
  returns — but it is stated here so that an unnecessary "improvement" to a
  success code is recognised as the regression it would be.
  Entry points that return a *value* rather than a status are the obvious
  exception: `SE_AddObject` returns an object id, `SE_GetIdByName` returns an
  id, `SE_GetNumberOfObjects` returns a count. Those are non-negative on
  success and are graded against the value the scenario makes predictable, not
  against `0`.
- **Nothing below changes during step 04.** The checker re-runs these
  surfaces at this step to confirm that the bounding-box and alignment work
  landed without disturbing anything else. If a value you read here changes,
  that is a regression.

The two headers that declare all of this are
`EnvironmentSimulator/Libraries/esminiLib/esminiLib.hpp` and
`EnvironmentSimulator/Libraries/esminiRMLib/esminiRMLib.hpp`; both are in your
tree and both remain the authority on anything this document does not state.

## 1. Scenario engine lifecycle — `libesminiLib.so`

```c
int  SE_Init(const char *oscFilename, int disable_ctrls, int use_viewer, int threads, int record);
int  SE_StepDT(float dt);
int  SE_GetQuitFlag();
void SE_Close();
```

`SE_Init` loads an OpenSCENARIO file and returns a non-negative value on
success. `use_viewer` must be `0` in this build — OpenSceneGraph is switched
off (see `PROFILE.md`). `record` is `0` for no recording.

`SE_StepDT` advances the simulation by a fixed timestep and returns a
non-negative value while the scenario is still running. `SE_GetQuitFlag`
reports `0` while the storyboard is still running and becomes non-zero once
the scenario's own stop trigger has fired. `SE_Close` releases the scenario;
it returns nothing and is safe to call once per `SE_Init`.

Entry points that operate on a scenario reject being called before `SE_Init`
has succeeded, and report that rejection with a negative return value.

## 2. Object enumeration and state readback

```c
int         SE_GetNumberOfObjects();
const char *SE_GetObjectName(int object_id);
int         SE_GetIdByName(const char *name);
int         SE_GetObjectState(int object_id, SE_ScenarioObjectState *state);
int         SE_GetRoadInfoAtDistance(int object_id, float lookahead_distance,
                                     SE_RoadInfo *data, int lookAheadMode,
                                     bool inRoadDrivingDirection);
```

Object ids are assigned in the order the scenario declares its entities,
starting at `0`. `SE_GetNumberOfObjects` returns how many a running scenario
holds. `SE_GetIdByName` maps an entity's scenario name to that id and returns
a negative value for a name no entity carries. `SE_GetObjectState` fills the
caller's struct and returns a non-negative value on success; an object id
outside the range the scenario declares is **rejected with a negative return**,
not silently accepted.

`SE_ScenarioObjectState` is declared in `esminiLib.hpp`. The fields this step
reads are:

| Field | Type | Meaning |
| --- | --- | --- |
| `id` | `int` | the object's own id, matching the id you passed in |
| `x`, `y`, `z` | `float` | position in the global coordinate system |
| `h`, `p`, `r` | `float` | heading, pitch and roll in the global coordinate system, in radians |
| `roadId` | `int` | OpenDRIVE road id the object resolves onto |
| `junctionId` | `int` | junction id, or `-1` when not in a junction |
| `laneId` | `int` | OpenDRIVE lane id |
| `laneOffset` | `float` | lateral offset from the centre of that lane |
| `s` | `float` | longitudinal position along the road reference line |
| `t` | `float` | lateral position in the road coordinate system |
| `speed` | `float` | current speed |
| `centerOffsetX`, `centerOffsetY`, `centerOffsetZ` | `float` | bounding-box centre relative to the object's reference point, in the object's own local frame |
| `width`, `length`, `height` | `float` | bounding-box dimensions |

**The six bounding-box fields already exist in this struct in the tree you
received**, and entities declared by a scenario already report their declared
box through them. Step 04 does not add them; what step 04 adds is a way to
supply a box for an object created at runtime, and the box columns in the CSV
recording. See `instruction.md` §1 and §3.

### `SE_GetRoadInfoAtDistance` and `SE_RoadInfo`

`SE_GetRoadInfoAtDistance` looks a fixed distance *along the road* ahead of an
object and describes the point it arrives at. It is inherited, it is unchanged
across step 04, and a case in this step re-reads it to confirm exactly that.
The argument order is the one in the code block above and is easy to get
wrong, so it is spelled out:

| Argument | Meaning |
| --- | --- |
| `object_id` | the object to measure from |
| `lookahead_distance` | how far ahead, in metres, measured along the road — **not** in a straight line |
| `data` | caller's `SE_RoadInfo`, filled in on success |
| `lookAheadMode` | `0` = along the lane centre, `1` = along the road reference line, `2` = along the current lane offset |
| `inRoadDrivingDirection` | `true` looks along the lane's driving direction; `false` looks in whichever direction is closest to the object's own heading |

The fields of `SE_RoadInfo` this step reads:

| Field | Type | Meaning |
| --- | --- | --- |
| `roadId` | `int` | OpenDRIVE road id of the looked-up point |
| `junctionId` | `int` | junction id of that point, or **`-1` when the point is not inside a junction** — not `0`, and not the road id |
| `laneId` | `int` | OpenDRIVE lane id at that point |
| `s` | `float` | longitudinal position of that point along the road reference line |

**Return value.** `0` means the lookahead completed on the road it started on,
and that is the value this step's case asserts, because it is what the fixture
produces: a straight single-road network with the object far enough from the
end that a 10 m lookahead stays on the same road and out of any junction. The
entry point *can* also return small positive values in other situations —
`1` when the lookahead crossed into a new road, `2` when it had to pick a way
through a junction — and negative values for the failure cases in the header's
own comment (off road, end of route, end of road, generic). Those are real
parts of the entry point's contract; they simply do not arise on the fixture
this step measures. Do not change which value comes back for the case that
does arise.

## 3. Reporting an externally driven position

```c
int SE_ReportObjectPos(int object_id, float timestamp, float x, float y, float z, float h, float p, float r);
int SE_ReportObjectPosXYH(int object_id, float timestamp, float x, float y, float h);
```

Both place an object at a world coordinate the caller chooses, overriding
whatever the scenario would have done with it for that step. The argument
order is positional and is exactly as shown: the id first, then a timestamp,
then the coordinates.

The difference between them is what they leave unsaid:

- `SE_ReportObjectPos` supplies **all six** degrees of freedom, including an
  explicit elevation `z` and an explicit pitch `p` and roll `r`.
- `SE_ReportObjectPosXYH` supplies only the **horizontal** ones — `x`, `y` and
  heading. It says nothing about elevation, pitch or roll.

This distinction is what `instruction.md` §5 is about: the alignment mode
decides what happens to the components the caller did not pin down, and (for
`ALIGN_HARD`) what happens to the ones it did.

## 4. The road-manager handle API — `libesminiRMLib.so`

`libesminiRMLib.so` reads an OpenDRIVE network on its own, with no scenario
and no scenario engine. It is the library `instruction.md` §6 refers to when
it speaks of "a bare road-manager handle".

```c
int   RM_Init(const char *odrFilename);
int   RM_Close();

int   RM_GetNumberOfRoads();
int   RM_GetIdOfRoadFromIndex(int index);
float RM_GetRoadLength(int id);
int   RM_GetRoadNumberOfLanes(int roadId, float s);

int   RM_CreatePosition();
int   RM_SetLanePosition(int handle, int roadId, int laneId, float laneOffset, float s, bool align);
int   RM_SetWorldXYHPosition(int handle, float x, float y, float h);
int   RM_GetPositionData(int handle, RM_PositionData *data);

float RM_GetLaneWidth(int handle, int lane_id);
float RM_GetLaneWidthByRoadId(int road_id, int lane_id, float s);
int   RM_GetLaneType(int handle, int lane_id);
int   RM_GetLaneTypeByRoadId(int road_id, int lane_id, float s);
```

Roads are addressed by **id**, and ids are not indices: use
`RM_GetIdOfRoadFromIndex` to turn a `0`-based index in `[0,
RM_GetNumberOfRoads())` into the id every other call expects.

`RM_CreatePosition` allocates a position handle and returns it; handles are
handed out from `0` upwards. A position handle is a cursor into the network
that you move with a setter and read back with `RM_GetPositionData`.

`RM_PositionData` carries `x`, `y`, `z`, `h`, `p`, `r`, `hRelative`, `roadId`,
`junctionId` (`-1` outside a junction), `laneId`, `laneOffset` and `s`.

The `ByRoadId` and handle-based spellings of the lane-width and lane-type
lookups answer the same question two ways — one against an explicit
`(road, lane, s)` triple, one against wherever a handle currently sits — and
agree with each other for the same place on the network.

**`RM_SetWorldXYHPosition` is a snap, not an inverse.** It resolves an
arbitrary world coordinate onto the nearest acceptable place on the network.
Feeding it the `x`/`y`/`h` that `RM_SetLanePosition` just produced for a given
lane does **not** in general return that lane: on a multi-lane road the
resolved point may legitimately land in a neighbouring one. This is
long-standing behaviour and is not something step 04 changes. Which lanes
count as acceptable is exactly what `RM_SetSnapLaneTypes` controls — see
`instruction.md` §6.

## 5. The CSV recording

`esmini --csv_logger <file>` writes the per-entity recording that
`instruction.md` §1 is about. Two things about its shape that §1 assumes
rather than states:

- Entity blocks are numbered from **`#1`**, and `#1` is the scenario's first
  entity — the one with object id `0`. The numbering is a column-block index,
  not an object id.
- The preamble that precedes the header row is six lines, and it is written
  before any entity data: the four `esmini GIT …` / `esmini BUILD VERSION`
  banner lines, then a `Scenario File Name:` line and a `Number of Vehicles:`
  line. (The "four" and the "six" count different things — four banners out of
  six preamble lines — and `instruction.md` §1 lists all six.) In this build
  the four banner values are placeholders, because the tree is built from a
  source export rather than from a git checkout.

## 6. The `esmini` command line

The options this package uses, all of which already exist in the tree you
received:

| Option | Effect |
| --- | --- |
| `--osc <filename>` | the OpenSCENARIO file to run (required) |
| `--headless` | run with no viewer window — mandatory in this build |
| `--fixed_timestep <seconds>` | run decoupled from realtime at a fixed step |
| `--csv_logger <filename>` | write the per-entity CSV recording |
| `--record <filename>` | record position data into a `.dat` file for later replay |
| `--logfile_path <path>` | where the text log goes; **the default is `log.txt` in the working directory** |
| `--disable_stdout` | suppress messages on stdout |

`esmini` with no arguments prints a usage banner that begins
`Usage: esmini [options]` and lists each option on its own line, indented two
spaces.

**The banner IS checked**, and this is the one place in this document where a
console message is graded rather than merely described. The reason is that
`esmini` ignores options it does not recognise (see Exit status, below), so an
option's presence or absence cannot be detected by running it — the banner is
the only place the binary states what it accepts. The checker therefore reads
the banner as the CLI's declared surface. It does not compare the whole block
and it does not depend on line numbers or ordering; it requires these lines,
each at the start of a line and spelled exactly as shown:

```
Usage: esmini [options]
  --osc <filename>
  --headless
  --fixed_timestep
  --logfile_path <path>
```

and it requires that no line begins `  --log_level` or `  --log_meta_data`.
Those two options arrive in a later release; emitting them here would be
inventing a capability this step does not have, and it is checked as such.

Everything else in the banner is free to drift: other options may be listed,
in any order, with any wording. Keep the five lines above and the two
absences, and treat the option table above as the set this package depends on.

**Exit status.** A run that reaches the scenario's own stop trigger and shuts
down exits `0`. A run that cannot start — an `--osc` file that does not exist,
for instance — exits **non-zero**. An option the build does not recognise is
ignored rather than diagnosed, so a typo in an option name produces a
successful run that quietly did not do what was asked.

**The text log.** Messages that carry a simulation time are written as the
time in seconds to three decimal places, a colon, a space, and then the
message — `0.000: Starting teleport Action`. Lines emitted before the clock
starts (file loading, parameter counts) carry no timestamp. The log also
repeats the four `esmini GIT …` / `BUILD VERSION` banner lines at its head.

Storyboard elements announce their own state changes on the same timestamped
line, as `<element name> <from> -> <transition> -> <to>`. The checks read one
of these: the `Ego` entity's Init teleport, which at time zero produces

```
0.000: Init Ego TeleportAction standbyState -> startTransition -> runningState
```

The three state and transition spellings (`standbyState`, `startTransition`,
`runningState`) and the ` -> ` separator are what is read, together with the
element name in the position shown. Other storyboard elements print lines of
the same shape with their own names and their own states; nothing constrains
how many such lines a run emits, or their order relative to each other.

`esmini-dyn` prints `Closing` on its own line when it shuts the library down.
Note what that line does **not** tell you: it is printed, and the process
still exits `0`, even when the scenario failed to load. A run that really
happened is distinguished instead by the absence of `Failed to load` and
`Failed to initialize scenario`, and by the storyboard and position lines
above actually appearing.

## 7. The other three artifacts

- **`dat2csv <file.dat>`** converts a recording made with `--record` into a
  CSV. It writes that CSV into the **current working directory**, under the
  recording's basename with the suffix changed, and says nothing at all on
  stdout when it succeeds. The CSV opens with a
  `Version: …, OpenDRIVE: …, 3DModel: …` line describing the recording,
  followed by a header row naming the per-sample columns.
- **`odrplot <file.xodr>`** samples a road network's geometry into `track.csv`
  in the **current working directory** and reports what it wrote and at what
  stepsize. The file is a flat CSV: one `lane, …` header line per lane
  section, then one sampled point per line.
- **`esmini-dyn`** takes the same `--osc <filename>` a scenario run needs and
  plays it through `libesminiLib.so` rather than through the statically linked
  player. It is the shared-library-driven player: it does not start unless
  that library is on the loader path, which is what makes it the artifact that
  notices if the library's published surface stops loading. It does **not**
  replay a `.dat` recording — that is `replayer`, which this profile does not
  build — and it exits `0` even when the file it was handed could not be
  loaded, so its exit status alone is not evidence that it ran anything.

## 8. What step 04 adds on top of all of this

Nothing in this document is your task. The entry points, struct fields, column
spellings and behaviours you are asked to build are stated in
`instruction.md`, which is the only document that describes new work.
