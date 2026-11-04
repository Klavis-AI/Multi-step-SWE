# Step 07 - Drivable Lane Queries

Upgrade the esmini scenario engine from v2.46.4 to v2.51.1 (release 2.51.1).

This step reworks the road-manager lane query surface. Lane enumeration becomes
type-aware, the two lane-width queries stop returning a width as their return value and
start reporting a status with the width in a caller-supplied out parameter, road-relative
positioning gains an explicit heading-alignment mode, the road manager learns to
initialise from an XML string, and the scenario-engine option store gains indexed and
enum-addressed readers. The simple vehicle model gains a combined acceleration/steering
control entry point.

## Requirements

1. **RM_InitWithString** - `int RM_InitWithString(const char* odrAsXMLString)`. Initialise
   the road manager from the full text of an OpenDRIVE document held in memory, rather than
   from a path on disk. It must report success the same way the path-based initialiser does
   and must leave the manager in a state where every ordinary road query works. A caller
   that passes text which is not a valid OpenDRIVE document must get a failure status back
   rather than a crash or a half-initialised manager.

2. **RM_GetRoadNumberOfDrivableLanes / RM_GetDrivableLaneIdByIndex** -
   `int RM_GetRoadNumberOfDrivableLanes(id_t roadId, float s)` and
   `int RM_GetDrivableLaneIdByIndex(id_t roadId, int laneIndex, float s, int* lane_id)`.
   Enumerate only the drivable lanes of a road at a given s. The count must exclude lanes
   that are not drivable, indices must run from 0 to count-1, and the id must be delivered
   through the out parameter with a status as the return value. The index runs over the
   matching lanes in the same order as the general enumeration of requirement 4. An index
   past the end and an undefined road id must each be reported as a failure, and a failing
   call must leave the caller's storage untouched: it has to decide that the call cannot be
   answered before it writes anything, so a request that fails still returns the failure
   status even when the caller supplied no destination at all. The out parameter is written
   only by a call that succeeds, so a caller that expects an answer must supply somewhere to
   put it.
   A lane is drivable exactly when its type is one the all-driving mask selects, so this pair
   and the general pair of requirements 3 and 4 called with `LANE_TYPE_ANY_DRIVING`
   (`1966594`, requirement 5) must report the same count and deliver the same lane ids in the
   same order.

3. **RM_GetRoadNumberOfLanes** - `int RM_GetRoadNumberOfLanes(id_t roadId, float s, int type_mask)`.
   The lane count becomes type-aware: it gains a trailing lane-type mask and reports how many
   lanes of the requested types exist at that s. Callers asking for every lane type must still
   see the whole lane section.

4. **RM_GetLaneIdByIndex** -
   `int RM_GetLaneIdByIndex(id_t roadId, int laneIndex, float s, int type_mask, int* lane_id)`.
   Lane addressing by index becomes type-aware and status-returning in the same way: the index
   runs over the lanes matching the mask, and the lane id is delivered through the out
   parameter. Indexing must be consistent with the count reported for the same mask.
   The enumeration keeps the lane section's own ordering - the order the lanes were declared
   in, left to right across the section, which is descending lane id - restricted to the
   lanes the mask selects. So index 0 is the leftmost matching lane, index count-1 the
   rightmost, and the reference line (lane id 0) appears in that sequence whenever the mask
   selects it. What it is selected *by* follows from the normalisation the lane-type query
   already reports: a section whose `<lane id="0">` declares `driving` has that type
   normalised to `none` on load, and `none` is bit 0 - a member of `ANY` and of no typed
   mask - so the reference line is counted by `ANY` and by neither `ANY_DRIVING` nor
   `ANY_ROAD`. On `straight_500m` road 1 the section enumerates as `3 2 1 0 -1 -2 -3` under
   `ANY`, a count of 7. This is the ordering the pre-existing enumeration already used; it is not a
   new convention, and reusing the lane section's iteration order is what produces it.
   A negative index, an index at or past the count, and an undefined road id are each a
   failure that leaves the caller's storage untouched.

5. **Lane type masks** - The lane-type mask is a bit set over the `Lane::LaneType` enumeration
   already declared in the road-manager header (`Modules/RoadManager/RoadManager.hpp`), one bit
   per lane type. The composite constants that enumeration already computes are the ones the
   mask parameter takes, and this step does not change them: `LANE_TYPE_ANY_DRIVING = 1966594`
   (`LANE_TYPE_DRIVING | LANE_TYPE_ENTRY | LANE_TYPE_EXIT | LANE_TYPE_OFF_RAMP |
   LANE_TYPE_ON_RAMP | LANE_TYPE_BIDIRECTIONAL`), `LANE_TYPE_ANY_ROAD = 1966726`
   (`LANE_TYPE_ANY_DRIVING | LANE_TYPE_RESTRICTED | LANE_TYPE_STOP`) and `LANE_TYPE_ANY = -1`.
   Those are the spellings the header declares; where this document writes the short forms
   `ANY_DRIVING`, `ANY_ROAD` and `ANY` it means these same three constants. Both numbers are
   what the existing enumeration in your own tree already evaluates to - read them there
   rather than retyping them. Querying with these three masks on the same road and s must be mutually consistent:
   the driving lanes are a subset of the road lanes, which are a subset of all lanes, and the
   counts must order accordingly.

6. **RM_GetLaneWidth** - `int RM_GetLaneWidth(int handle, int lane_id, float* width)`. The
   width query addressed through a position handle stops returning the width as a float and
   returns a status instead, writing the width into the out parameter. Every failure mode -
   a handle that was never created, a null destination, a query after the network was closed -
   must be a failure status, and must leave the caller's storage untouched rather than
   writing a sentinel width into it. Those are failures of the *query*, not verdicts on the
   lane: a lane id the section does not carry is a successful query whose answer is zero, which
   requirement 7 states for both width forms.

7. **RM_GetLaneWidthByRoadId** -
   `int RM_GetLaneWidthByRoadId(id_t road_id, int lane_id, float s, float* width)`. The
   road-addressed width query converts the same way. For a lane a caller can reach both ways,
   the two forms must report the same width.

   Both forms distinguish *no such lane* from *no such query*. A lane id the section does not
   carry is not an error: the road exists, the s is on it, and the answer to "how wide is that
   lane here" is simply zero, so both forms must return a success status and write `0.0` into
   the out parameter. The reference line (lane id `0`) behaves the same way, and so does a lane
   id outside the section's range such as `-9`. That is different from the failure cases of
   requirement 6 - an uncreated handle, an undefined road id, a null destination, a query after
   the network was closed - where there is no road to measure against at all and the call must
   report a failure status and leave the caller's storage untouched. Widening the zero-width
   answer into a failure status, or narrowing the failure cases into a zero width, are both
   regressions.

8. **RM_SetRoadPosition** -
   `int RM_SetRoadPosition(int handle, id_t roadId, float s, float t, bool align)`. Place a
   position handle by road id, s and lateral offset t. The `align` flag selects what happens
   to the handle's heading: when set, the heading is aligned to the road's driving direction
   at that point; when clear, the heading the handle already carried is preserved. The
   resulting x, y, s, road id and lane id must be identical either way - only the heading may
   differ. An undefined road id and a handle that was never created must each return a
   failure status.

9. **SE_GetOptionValuesCount / SE_GetOptionValueByIndex** -
   `int SE_GetOptionValuesCount(const char* name)` and
   `const char* SE_GetOptionValueByIndex(const char* name, unsigned int index)`. An option
   that has been given several values becomes readable one value at a time by index, with the
   count reported separately. An index past the end and an option name that was never set must
   each yield a null pointer rather than a stale or fabricated string. The *count* for such a
   name is `0` - whether it was unset or never registered at all. That is a count, not a
   rejection, so it is reported as `0` and not through the negative-on-failure convention used
   elsewhere in this document.

   The order is part of the contract, not an implementation accident: the store keeps its
   values most-recently-set first, so after setting `dirA`, then `dirB`, then `dirC` on the
   same option, index 0 reads `dirC`, index 1 `dirB` and index 2 `dirA`. The reason is that
   the last value given for an option is the one that takes priority - a later `--path` is
   meant to win over an earlier one - so the priority order and the index order are the same
   order, and index 0 is always the value in force. An insertion-ordered store, which would
   read `dirA` at index 0, is a different and observably wrong contract.

10. **SE_GetOptionValueByEnum** - `const char* SE_GetOptionValueByEnum(unsigned int enum_value)`.
    The same option store becomes addressable by the option's entry in the configuration
    enumeration, so a caller holding the enum value need not know the option's spelling. For an
    option that carries values, this must return the same text the indexed reader returns for
    that option's first index - which, by requirement 9, is the value most recently set. An
    enum value that names no option must yield a null pointer.

    The enumeration itself is new in this step and has no counterpart in your starting tree, so
    the numbering is part of what you are being asked to produce rather than something to read
    off the existing code. Declare it as `enum CONFIG_ENUM` in namespace `esmini_options`, in a
    new configuration header under `EnvironmentSimulator/Modules/CommonMini/` (upstream calls
    it `EnumConfig.hpp`), with one entry per registered command-line option, numbered from
    `OSC = 0` upwards in the order the options are registered. The numbers are an ABI: a caller
    that compiled against this enumeration passes an integer across the C boundary, so the
    value assigned to an option is externally visible and callers depend on it. **The entry for
    the `path` option has the value `51`** - that is the one ordinal this step's callers use by
    number, and a differently ordered enumeration that gives `path` some other value is not
    interchangeable with this one. An out-of-range value such as `9999` names no option and
    must yield a null pointer rather than reading off the end of the table.

11. **SE_SimpleVehicleControlAccAndSteer** -
    `void SE_SimpleVehicleControlAccAndSteer(void* handleSimpleVehicle, double dt, double acceleration, double steering_angle)`.
    Drive the simple vehicle model by commanded acceleration and steering angle, rather than
    by the existing throttle/steering-rate control. A commanded acceleration must integrate
    into speed linearly over the step - after n steps of dt at acceleration a the speed must
    have changed by a*n*dt - and a commanded steering angle must turn the vehicle's heading
    while leaving its speed alone when the commanded acceleration is zero.

   The reference case is a vehicle created with `SE_SimpleVehicleCreate` using a wheelbase of 4.0 m and a start speed of 10 m/s. The heading model used is a weighted-axle bicycle model whose reference point sits 15% along the wheelbase, and it is the heading that model predicts from those parameters that is required — a solver can derive it from the sentence above rather than from any test. The tolerance it is compared within is wide enough to admit the other standard bicycle models (the rear-axle one and the centre-of-gravity one), so the choice among them is not constrained either.

   The entry point joins the existing simple-vehicle control family, and it inherits that family's handling of a handle it cannot use: a null handle is **ignored**, not dereferenced — the call returns without doing anything, as the neighbouring `SE_SimpleVehicleDelete` and `SE_SimpleVehicleSetSpeed` already do. A caller that never created a vehicle must not be able to crash the process.

## Notes

- The build profile for this step is offline and headless: OpenSceneGraph, OSI, SUMO,
  googletest, ImPlot and PROJ are all disabled, and no external binaries or submodules are
  downloaded during the build. Do not add a dependency that needs any of them.
- **How the road manager's `int` returns are read.** Every road-manager entry point in this
  step returns an `int`, and the callers that check it read those returns as follows. There
  are three families, and they differ in what success looks like:
  - **Counting queries** - `RM_GetRoadNumberOfLanes` and `RM_GetRoadNumberOfDrivableLanes` -
    return the number they counted, which is zero or more. A road that exists but has no lane
    matching the mask counts `0`; that is a successful count of nothing, not a failure.
  - **Status-returning entry points** - `RM_InitWithString`, `RM_GetLaneIdByIndex`,
    `RM_GetDrivableLaneIdByIndex`, `RM_GetLaneWidth` and `RM_GetLaneWidthByRoadId` - return
    exactly `0` on success and deliver their result, where they have one, through an out
    parameter. Do not return a non-zero "success" code from these.
  - **Position setters**, `RM_SetRoadPosition` among them, have returned a family of
    non-negative success codes since 2.21.2, not just zero. Preserve that: callers
    distinguish success from failure by the sign, and narrowing success back to exactly zero
    is a regression. This applies to the setters; it does not license a non-zero return from
    the status-returning entry points above.

  **Failure is negative in all three families** - an unknown road id, an index outside the
  range the count reported, a negative index, a null out parameter, a call made before the
  network was loaded or after it was closed. The checks assert the **sign** of a failing
  return, not its magnitude: upstream answers `-1`, but nothing in this step requires that
  particular value, and a build that answers some other negative number is equally acceptable. An
  out parameter is left untouched when a call fails.
- Existing behaviour that this step does not mention must keep working unchanged, including
  the path-based road-manager initialiser, the legacy distance queries, the option store's
  set/get/unset surface, and the command-line tools and their output formats. One exception
  inside the option store: the status returned for *setting* an option name that was never
  registered does change across this step's range, so it is not part of the inherited contract
  and no check pins it in either direction. The rest of that surface is pinned, and one clause
  of it is worth stating because an earlier step left it open: unsetting a name **drops the
  values stored under it**, so `SE_GetOptionValue` reports nothing for that name afterwards
  rather than handing back the value that was there before. That is requirement 6 of the
  previous step's count rule - "after unsetting, the count must return to 0" - read through the
  single-value getter, and the check that reads it stores a value, unsets it, and reads again.
- Every C++ driver the checker runs links your `libesminiLib.so` or `libesminiRMLib.so`
  directly, so each of those runs also reads whatever default configuration file your tree
  ships next to the binaries. Keep any default `config.yml` free of settings that change how a
  run executes or what it logs - a default `fixed_timestep` or `log_level` left in the tree
  silently changes the behaviour of groups that have nothing to do with configuration files,
  and the failure surfaces far from its cause.

## Artifacts and entry points that must survive

Two further things are checked, both of them inherited behaviour rather than new work.

**The full exported entry-point set.** Both shared libraries must continue to export every
entry point the corpus calls: the entry points introduced by the requirements above, plus the
inherited set named here. In `libesminiLib.so`: `SE_Init`, `SE_Step`, `SE_StepDT`, `SE_Close`,
`SE_GetSimulationTime`, `SE_GetNumberOfObjects`, `SE_GetObjectState`,
`SE_GetDistanceToObject`, `SE_SimpleGetDistanceToObject`, `SE_GetObjectOdometer`,
`SE_GetObjectVelocityGlobalXYZ`, `SE_GetObjectAngularVelocity`,
`SE_GetObjectAngularAcceleration`, `SE_SetOptionValue`, `SE_GetOptionValue`, `SE_GetOptionSet`,
`SE_SetOption`, `SE_UnsetOption`, `SE_GetOptionValuesCount`, `SE_SimpleVehicleCreate`,
`SE_SimpleVehicleDelete`, `SE_SimpleVehicleControlBinary`, `SE_SimpleVehicleControlAnalog`,
`SE_SimpleVehicleGetState`. In `libesminiRMLib.so`: `RM_Init`, `RM_Close`, `RM_CreatePosition`,
`RM_SetLanePosition`, `RM_SetWorldXYHPosition`, `RM_GetPositionData`, `RM_SetH`, `RM_SetHMode`,
`RM_GetNumberOfRoads`,
`RM_GetIdOfRoadFromIndex`, `RM_GetRoadLength`, `RM_GetLaneType`, `RM_GetLaneTypeByRoadId`,
`RM_GetNumberOfRoadsOverlapping`, `RM_GetOverlappingRoadId`, `RM_SetLogFilePath`. Declaring a
symbol in a header is not enough - each must be defined and visible in the shared object's
dynamic symbol table, and visible **under exactly these names**: every entry point is exported
with **C linkage**, so the name in the dynamic symbol table is the plain name above. A C++-mangled
export does not count as the entry point, even though the function is there and the header
declares it. Do not strip the libraries.

The signatures above are the ones the checker's own C++ drivers are compiled against, so a
changed parameter list or return type is a link failure, not a lenient mismatch. `id_t` in those
signatures is the `typedef uint32_t id_t;` both headers already publish; it is inherited and this
step does not change it.

**The tool surfaces.** `dat2csv` and `odrplot` must keep their command-line behaviour and
output shapes. `dat2csv` with no arguments prints a usage line naming the program and
`<filename>`, and exits non-zero. On a recording it prints a line containing
`dat version: 2`, and writes a CSV whose first line is
`Version: <n>, OpenDRIVE: <file>, 3DModel: <model>` and whose second line is the fixed header
`time, id, name, x, y, z, h, p, r, speed, wheel_angle, wheel_rot`. Two details of *where* the
output goes are part of the contract: the CSV is written into the **current working
directory**, not beside the input file, and it is named from the **basename** of the input
(with the extension replaced by `.csv`), not from its full path. `odrplot <road>.xodr` writes
`track.csv` into the working directory, whose first line is a lane descriptor of the form
`lane, <road_id>, <lane_section>, <lane_id>, <drivability>` — the **road** id comes first and the
**lane** id third, and the last field is a drivability word, `driving` or `no-driving` (one
hyphen, no space), not the OpenDRIVE type name. For `straight_500m.xodr`, whose road id is `1`,
that first line is `lane, 1, 0, 3, no-driving`. The remaining lines are comma-separated samples
with six decimal places.

The recording container itself is still **format version 2** at this tag, and a `.dat`
produced by a much older release in this ladder must still convert. Do not change the
container version in this step.

**The player's logging envelope.** The logging backend is replaced in this step, and the
envelope the messages are wrapped in must not change. Every diagnostic line the player emits
begins with a bracketed time and a bracketed level: `[<time>] [<level>] <message>`, where
`<level>` is one of `info`, `warn`, `error` or `debug`. Before the simulation clock advances
the time bracket is **empty** (`[] [info] ...`); once the clock is running it holds seconds
formatted to **three decimal places** (`[0.050] [info] ...`). Both forms must appear in an
ordinary headless run, and in both of them a message body follows the level tag - the
envelope is a prefix, not the whole line. The message *text* after the level tag is not part
of the contract and is free to change; that a message is there at all is.

The envelope has a second variant, selected by `--log_meta_data`, which inserts the source
location between the level tag and the message: `[<time>] [<level>] [<file>::<function>::<line>] <message>`.
Both time-bracket forms occur in this variant too. The field is off by default - an ordinary
run must not carry it - and its contents are out of scope: file names, function names and line
numbers move between releases and this step does not pin them. What must survive the backend
swap is that the option still produces the three-bracket form, with all three parts present
and a message after them.

**The `--csv_logger` table.** `esmini --csv_logger <file>` must keep writing the per-frame
table it writes today, in the same shape: a six-line preamble, the column header on line 7,
and one comma-separated row per simulated frame, every row carrying the same number of fields
as the header. The header names an index column `Index [-]`, a time column `TimeStamp [s]`,
and then one block of columns per entity in the scenario, each block prefixed with `#<n> `
and beginning with `Entity_Name [-]`. The column names and the field count are the contract;
which entities appear depends on the scenario.

One detail of the header's spelling **does** change in this step: the space that v2.46.4
prints *before* each comma is gone, so where the older tree writes
`Index [-] , TimeStamp [s] , #1 Entity_Name [-] , ` this one writes
`Index [-], TimeStamp [s], #1 Entity_Name [-], `. The single space *after* each comma stays.
The check that reads the header's shape leaves the spacing free, so it passes either way; a
separate check requires the new spelling. Write the header exactly as above.

**Command-line flags.** `--osc`, `--headless`, `--save_xosc <mode>`, `--record <file>`,
`--csv_logger <file>`, `--log_meta_data`, `--fixed_timestep <seconds>` and `--help` must all
keep working. `--fixed_timestep <seconds>` decouples the run from realtime and advances the
simulation by the number of seconds it names on each frame; it is what makes a run's recorded
output reproducible, and the checks in this step pass `--fixed_timestep 0.05` to every
scenario they run. `--help` prints a usage block describing the registered options: a
`Usage:` line carrying `[options]`, and an entry for each option, `--osc <filename>` and
`--headless` among them. `--save_xosc quit` must stop the run at the first frame, exit zero and
write the resolved scenario to `<base>_<index>_of_<count>.xosc` in the current working
directory, named from the input's basename, where `index`
and `count` come from the parameter-distribution machinery and are **both `0`** for a scenario
that declares no distribution file — so `two_car_brake.xosc` gives `two_car_brake_0_of_0.xosc`
however many entities it has; when `quit`
stops the run at the first frame like that, `--record` must not leave a recording behind, because the simulation never stepped.
`--save_xosc` takes two modes, `quit` and `continue`, and `continue` is what it does when the
flag is given with no value; under `continue` the resolved scenario is still written out, and
the run goes on to the end rather than stopping. `--record`'s filename defaults to `sim.dat`
in the working directory. `--osc` has no default: it is the required argument. Most runs in
this corpus take the shape `--osc <file> --headless --fixed_timestep <seconds>`; a few also
pass `--record`, and the `--help` run passes neither `--osc` nor `--headless`. `--headless`
and `--record` are unchanged by this step.

## Out of scope

The following requirements are deliberately **out of scope** in this step. Each was measured
against both tags, and each is either not observable through the documented test interface or
not a change in this range. They are listed so the absence of a check is a decision rather
than an oversight.

- **End-of-road stopping** - not a checkable claim in this range. The stop position and the
  `EndOfRoadCondition` delay are indistinguishable between v2.46.4 and v2.51.1 for the
  scenarios the corpus can drive.
- **Controller-domain policy by OpenSCENARIO version** - not a change in this range.
- **Minimum step duration** - the ≥1 ms per-step clamp is a wall-clock property, so any
  threshold assertion would be nondeterministic under load and would fail the Oracle at
  random. Not observable through this interface.
- **Vehicle-catalog `maxSteering` units** - this *is* a real change in this range (bicycle and
  motorbike axles move from a legacy degree value to radians), but it is observable only by
  reading the shipped `VehicleCatalog.xosc`, which the test interface explicitly does not do:
  checks read exit status, stdout, stderr and files the candidate writes, never the
  candidate's source or shipped data files. Asserting it would require the checker to read
  the workspace, which the interface forbids.
- **Custom road-manager log path** - the requirement as originally drafted is **inverted**
  against the actual behaviour. Measured in both call orders, v2.46.4 honours a custom path
  set through `RM_SetLogFilePath` and v2.51.1 does not, because the road manager's
  initialiser now overwrites the path with its own default. Requiring "a custom path is
  respected" at the target tag would fail a correct implementation. `RM_SetLogFilePath`
  itself must still be exported - that much is asserted with the entry-point set above, which
  reads the dynamic symbol table - but nothing in this step calls it, so its behaviour, and its
  precedence relative to the initialiser, are out of scope.

## Scope

This environment has no network access, and everything the build needs is
already installed. The upstream project's source, its releases, mirrors and
package registries are unreachable, so a copy cannot be fetched by any
route.
