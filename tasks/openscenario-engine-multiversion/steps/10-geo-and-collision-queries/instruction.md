# Add geometry, collision and light-state queries, and recording controls

The scenario-engine library can already tell an embedder where an entity is and
whether it is currently overlapping another one. It cannot tell the embedder
anything about what is *about* to happen, or about what the vehicle is
signalling. There is no way to ask how long it will be until two entities
collide, no way to ask whether an entity is allowed to leave its lane, no way
to read the state of a vehicle's lamps, and no way to convert a scenario
position into geodetic coordinates. On the tooling side, `dat2csv` decodes a
recording at one fixed number of decimals, which is either too few to see what
the engine actually computed or too many to read.

Add those queries to the public C API, make `LightStateAction` take effect on
the entity it names, and give the recording decoder a precision control. The
recorded layout changes with this work, so the recording format version moves
with it. One piece of engine behaviour changes alongside them, because a
prediction is worthless on top of a path the engine picks at random: an entity
with no route reaching a junction now goes straight on instead of choosing an
exit arbitrarily.

## Requirements

1. **Time to collision is a query on the public C API.**

   ```c
   int SE_GetTimeToCollision(int object_a_id, int object_b_id, int cs,
                             SE_RelativeDistanceType dist_type, bool free_space,
                             double *ttc);
   ```

   It mirrors the arithmetic behind `TimeToCollisionCondition`: the closing
   speed between the two entities applied to the distance between them.
   Returns 0 on success and -1 on failure — an object id that does not exist on
   either side, or both, and a null `ttc` pointer, are failures and must not be
   crashes.

   On success `*ttc` is the time in seconds until the two meet, and **-1.0 when
   that is undefined** — when the pair is not closing on one another at all:
   they are moving apart, or the closing speed along the line between them
   works out to zero or less, so the quotient would not be a time to meet. A
   target travelling at least as fast as the asking object's own approach is
   covered by that same condition; there is no separate rule for a target that
   merely sits behind the asking object, and no case depends on one. Two
   entities parked on the same network report -1.0, not a huge number and not
   zero.

   `free_space` selects what the distance is measured between: `false` measures
   reference point to reference point, `true` measures between the bounding
   boxes. For two five-metre cars closing at 10 m/s the free-space answer is
   therefore exactly 0.5 s earlier than the reference-point answer.

   `cs` takes a `roadmanager::CoordinateSystem` value and `dist_type` takes an
   `SE_RelativeDistanceType`. The two enumerations are these, in this order:

   - `CoordinateSystem`: `CS_UNDEFINED` 0, `CS_ENTITY` 1, `CS_LANE` 2,
     `CS_ROAD` 3, `CS_TRAJECTORY` 4, `CS_WORLD` 5.
   - `SE_RelativeDistanceType`: `REL_DIST_UNDEFINED` 0, `REL_DIST_LATERAL` 1,
     `REL_DIST_LONGITUDINAL` 2, `REL_DIST_CARTESIAN` 3, `REL_DIST_EUCLIDIAN` 4.

   Which of them the engine can resolve is fixed and is part of the contract.
   On a network of roads and lanes, entity, lane, road and trajectory
   coordinate systems all see the same longitudinal closing pair on a straight
   road; `CS_UNDEFINED` and `CS_WORLD` are the two the engine cannot resolve,
   world coordinates being a frame this C API is never given. Of the distance
   types, `REL_DIST_LATERAL`, `REL_DIST_LONGITUDINAL`, `REL_DIST_CARTESIAN` and
   `REL_DIST_EUCLIDIAN` resolve; `REL_DIST_UNDEFINED` is not a measurement and
   does not. `REL_DIST_LATERAL` on a pair that only closes longitudinally never
   closes, so it is the undefined -1.0; longitudinal, cartesian and euclidian
   all give the closing time.

   An unresolvable coordinate system or distance type — one of the two
   coordinate systems above, `REL_DIST_UNDEFINED`, or a value outside the
   defined range altogether — is **not** an error: the call still returns 0.
   What fails is only the distance measurement, and a distance the road manager
   could not measure is left at its stand-in for "immeasurably far" rather than
   being reported as a failure.

   The two answers compose in a fixed order, and the order is part of the
   contract. The closing speed is tested **first**: a pair that is not closing
   reports -1.0 whatever was asked, so an unresolvable coordinate system or
   distance type on a parked pair is -1.0 and not a large number. An
   unresolvable request only surfaces on a pair that *is* closing, and then it
   surfaces as that stand-in distance divided by the closing speed — a
   positive time so large that no real prediction could produce it: at least
   1e8 seconds, over three years, for any pair closing at a plausible road
   speed. That is how a caller tells an unresolvable request from a resolved
   one that found no collision. It is a magnitude, not a constant: it scales
   with the pair's closing speed, so a caller tests that the answer is
   absurdly large and never compares it to a fixed number.

   The query is symmetric for a straight closing pair: asking from the target
   about the source gives the same time.

   **The fixture the dynamic checks drive, and what its times work out to.** The
   closing pair they sample is the two-car braking scenario: the Ego starts
   100 m behind the Target, at 20 m/s against 10 m/s, and from the first
   simulation step past 3 s a *linear rate* action decelerates it towards 5 m/s
   at 3 m/s². At the 0.05 s step those runs use, that leaves the Ego at
   14.15 m/s at 5.000 s — the deceleration has applied for 1.95 s — so the
   reference-point gap there is about 55.85 m and the time to collision is that
   gap divided by the 4.15 m/s closing speed, about 13.46 s. The samples at
   0 s and 2 s are the initial geometry, before any of that applies: 100 m at a
   10 m/s closing speed, two seconds eaten at 2 s. Which step the deceleration
   starts on is the whole of the arithmetic at 5 s, so it is written down here
   rather than left to be inferred from a run.

2. **Lane-change legality is a query on the public C API.**

   ```c
   int SE_ObjectCanChangeLanes(int object_id);
   ```

   Returns a bit mask describing which way the entity may legally leave its
   current lane, judged from the drivable lanes that neighbour it and the road
   marks on the boundaries it would cross:

   - `0` — neither direction, including when the entity is not in a driving lane;
   - `1` — a lane change to the left is possible;
   - `2` — a lane change to the right is possible;
   - `3` — both;
   - `-1` — error: an object id that does not exist, negative or positive.

   "Left" and "right" are named by the road's own lane numbering, not by which
   way the entity happens to be facing: bit 1 is the neighbouring lane whose id
   is one **greater** than the entity's, bit 2 the one whose id is one **less**.
   On the carriageway whose lane ids are negative — the one an entity driving
   along increasing `s` uses — the two readings coincide with the driver's own
   left and right.

   The centre lane, id 0, is never a lane-change target. It is the reference
   line the other lanes are numbered from rather than somewhere to drive, so an
   entity in lane 1 or in lane -1 never gets the bit that points at it, whatever
   lane type and road mark the network happens to declare for lane 0. A road
   with one drivable lane each side of the centre line therefore answers 0 on
   both of them even where the centre line is marked as crossable.

   The answer must come from the entity's **current** position on the road
   network. Moving an entity to another lane with `SE_ReportObjectPos` and
   re-querying must give the code that belongs to the new lane, so on a
   carriageway of three drivable lanes the innermost, middle and outermost lanes
   answer differently from one another. A single drivable lane in each direction
   answers 0 for every entity on it.

3. **`LightStateAction` takes effect, and light state is readable.**

   ```c
   typedef struct { int light_mode; bool emitting; } SE_LightState;
   int SE_GetObjectLightState(int object_id, SE_VehicleLightType light_type,
                              SE_LightState *light_state);
   ```

   `light_mode` is 0 off, 1 on, 2 flashing, 3 unknown. `emitting` says whether
   the lamp is actually emitting light. The call returns 0 on success and -1 for
   an object id that does not exist. Unlike requirements 1 and 4, this one makes
   no promise about a null out-parameter: `light_state` must point at storage
   the call may write, and nothing is required of the call when it does not.

   `SE_VehicleLightType` enumerates the fourteen OpenSCENARIO vehicle lights, in
   this order and with these values: `DAYTIME_RUNNING_LIGHTS` 0, `LOW_BEAM` 1,
   `HIGH_BEAM` 2, `FOG_LIGHTS` 3, `FOG_LIGHTS_FRONT` 4, `FOG_LIGHTS_REAR` 5,
   `BRAKE_LIGHTS` 6, `WARNING_LIGHTS` 7, `INDICATOR_LEFT` 8, `INDICATOR_RIGHT` 9,
   `REVERSING_LIGHTS` 10, `TAIL_LIGHTS` 11, `LICENSE_PLATE_ILLUMINATION` 12,
   `SPECIAL_PURPOSE_LIGHTS` 13. A scenario names the same fourteen through the
   standard's own spellings of them, which is what the `<VehicleLight>` attribute
   carries and is the only spelling any scenario file uses:
   `daytimeRunningLights`, `lowBeam`, `highBeam`, `fogLights`, `fogLightsFront`,
   `fogLightsRear`, `brakeLights`, `warningLights`, `indicatorLeft`,
   `indicatorRight`, `reversingLights`, `tailLights`,
   `licensePlateIllumination`, `specialPurposeLights` — the same order and the
   same lamps, in the standard's camel-case spelling rather than the enum's
   upper-case one. A reader that only recognises its own enum spelling for the
   attribute does not name the lamp the scenario named.

   Before a scenario says anything about a lamp, that lamp reads back as mode 3
   (unknown) and not emitting — that is the state of every lamp of an entity no
   `LightStateAction` ever names, for the whole run.

   The action reaches the engine wrapped the way the standard wraps it, and all
   three levels of that wrapping are part of the input format: the private
   action is an **`AppearanceAction`** holding the **`LightStateAction`**, which
   holds a **`<LightType>`** element carrying the
   `<VehicleLight vehicleLightType="…"/>`, with the `<LightState mode="…"/>`
   element as its sibling, directly inside the `LightStateAction`:

   ```xml
   <PrivateAction>
     <AppearanceAction>
       <LightStateAction>
         <LightType><VehicleLight vehicleLightType="lowBeam"/></LightType>
         <LightState mode="on" luminousIntensity="1000"/>
       </LightStateAction>
     </AppearanceAction>
   </PrivateAction>
   ```

   A tree that recognises `LightStateAction` only where it stands directly under
   `PrivateAction`, or expects `<VehicleLight>` in place of the `<LightType>`
   element that carries it, refuses a scenario the checker drives.
   `luminousIntensity` is optional content and is out of scope.

   An action of that shape puts its lamp in mode 1 and emitting; it
   affects only the entity the manoeuvre's actor names, and only the lamp named,
   leaving the opposite indicator alone. `mode="off"` puts the lamp in mode 0 and
   not emitting. Setting `fogLights` sets the front and rear fog lamps with it,
   since `fogLights` denotes the group.

   `mode="flashing"` puts the lamp in mode 2, and mode 2 is where it stays for
   as long as the action holds it — `light_mode` does not blink. `emitting` is
   the other half: for a flashing lamp it follows the flash phase and
   alternates between emitting and not, so it describes the instant queried
   and is not a fixed property of the lamp. A caller that wants to know a lamp
   is flashing reads `light_mode`, not `emitting`.

   **The flash phase is the scenario's, not a rate of your choosing.** A
   `LightState` element that asks for `flashing` may declare
   `flashingOnDuration` and `flashingOffDuration`, in seconds: the lamp emits
   for the first and does not for the second, repeating. One period of the
   cycle is `flashingOnDuration + flashingOffDuration`, and each attribute
   defaults to `0.5` s when the element does not declare it — so a lamp whose
   element declares `0.5` and `0.5` is on for half of every second and off for
   the other half, and a query one full second apart sees the same phase. The
   phase runs from the instant the action takes effect. Read the two durations
   off the element rather than hard-coding a rate: that is what makes
   `emitting` an alternation the checker can sample.

4. **A scenario position can be asked for its geodetic position.**

   ```c
   int SE_GetGeoPos(int object_id, double *lat, double *lon);
   ```

   Returns 0 on success, with latitude and longitude in degrees written through
   the out-parameters, and -1 on failure. A failure must be a clean one: an
   object id that does not exist, a null `lat` or a null `lon` returns -1 and
   **writes nothing at all through the caller's pointers**, which keep whatever
   the caller put there.

   The transform needs the projection library. The build is configured
   without it (see the workspace handoff), and a network whose OpenDRIVE header
   carries no usable `geoReference` has nothing to project through either. In
   both of those situations the call reports -1 and leaves the caller's storage
   alone; it does not guess, and it does not fall back on the raw metres. What
   is required of this build, therefore, is the entry point itself: exported
   from the scenario-engine shared library with **C linkage** under exactly the
   name above — dlsym-able, not a mangled C++ symbol — behaving as described on
   the failure paths.

5. **`dat2csv` takes a `--precision` option.**

   `dat2csv --file <recording> --precision <n>` writes every floating-point
   column of the decoded CSV with `n` decimals. Without the option the output is
   unchanged from what it is now: three decimals.

   - `n` of 0 writes integers, with no decimal point and no trailing separator
     change; 1, 6 and 10 write that many decimals, so a value the engine holds
     as a repeating fraction shows more of itself at 10 than at 6.
   - The supported range is `0` to `20` decimals inclusive. An `n` outside it
     — greater than 20, or negative — is a warning, not a failure. The tool
     writes, verbatim,
     `Warning: Invalid precision. Using default value of 3.`
     falls back to three decimals, and still exits successfully.
   - `--precision` given with no value at all falls back to three decimals and
     must **not** be reported as an unrecognized argument.
   - `dat2csv --help` documents it. The help still opens with
     `Usage: dat2csv [options]`, and the option appears in the list as
     `  --precision [number of decimals]` with a description containing
     `Output floating point number precision`.

   Everything else about the decoded CSV is unchanged: same preamble, same
   column header, same rows, same sample rate.

6. **The recording carries a new format version.**

   The `.dat` recording stores object state and the layout changes with this
   work, so the version stamped into a recording moves too: the engine writes
   **recording format version 5.3**, where it writes 5.1 now.

   `dat2csv --file <recording> --file_refs` reports it as the **first line of
   the decoded CSV**, ahead of the column header, in the full form

   ```
   Version: <version>, OpenDRIVE: <path>, 3DModel: <path> GIT REV: <rev>
   ```

   naming the OpenDRIVE file the recording was made against. Read that line as
   three comma-separated fields followed by a fourth the decoder already
   carries. The first three are what this requirement is about, and only the
   version in the first of them moves with this work. The `GIT REV:` field is
   existing text, separated from the 3D model path by a space rather than a
   comma, and it stays; the line does not end at the 3D model path, and a
   reader who only needs the version, the network and the model should treat
   everything from ` GIT REV:` onwards as a trailing field it does not own.

   Two of the four values are properties of the build rather than of this work,
   and neither is in scope. No scene graph is generated in this build, so the
   recorder stores an empty 3D model path and `3DModel:` is followed straight
   away by the `GIT REV:` field; and `<rev>` reads `N/A` when the tree carries
   no git metadata, which is the normal case here. What is required is the
   version value and the three labels in the order above.

   That line is an addition to the CSV, not a replacement: the usual
   `time, id, name, x, y, z, h, p, r, speed, wheel_angle, wheel_rot` header and
   the usual sample rows follow it.

   A recording the engine has just written must be accepted by its own decoder,
   and decoded whole: the CSV runs to the end of the recording and carries the
   last samples the engine wrote for every entity, rather than stopping short of
   the scenario's final instant.

   Moving the version must not narrow what the decoder accepts. Compatibility
   is decided by the **major** version, 5, and this work does not move it: a
   recording written by the previous release — stamped 5.1, not 5.3 — is still
   read by the decoder shipped here, decoded whole in the same way, with the
   same column header and the same sample rows, and reported with its own
   version in its own first line. A decoder that refused such a recording, or
   that stopped part-way through one, because its minor version is behind would
   be wrong.

7. **An entity with no route goes straight on through a junction.**

   An entity that is not following a `Route` and drives into a junction leaves
   it on the connecting road that continues closest to straight ahead — the
   outgoing direction that differs least from the direction it arrived in —
   instead of on an arbitrarily chosen one. On a four-way crossing that is the
   road opposite the one it came in on, reached across exactly one connecting
   road.

   The choice is a property of the road network and the entity's heading, not
   of the run. The same scenario, driven twice with the same time step, must
   send every routeless entity through the junction the same way both times.
   An entity that *does* have a route still follows its route; this changes
   only what the engine does when there is nothing to follow.

   What changes is the road the entity actually drives onto. A lookahead query
   is a separate mechanism with its own junction handling and its own
   caller-supplied selector, and nothing here alters what one of those reports.

## Notes

- `esminiLib.hpp` is `extern "C"`. Every entry point above is C-callable under
  the name given, so an embedder can bind it by name from the shared library.

- The two collision surfaces are separate and both must work.
  `SE_GetObjectNumberOfCollisions` and `SE_GetObjectCollision` report overlaps
  that are happening **now**; requirement 1 predicts one that has not happened
  yet. Adding the second does not change, replace or re-tier the first.

- Apart from the recording format version in requirement 6, nothing the engine
  already does may change. That includes, and is checked: the road manager's
  network, road, lane and position queries and their error paths; the collision
  bookkeeping; parameter-value distributions and `SE_InitWithString`;
  `VariableDeclarations` readback including its type and unknown-name refusals;
  the `--csv_logger` preamble, column header and row format; `odrplot`'s
  `track.csv`; and the published set of artifacts, the usage text they print,
  the exported entry points they already carry, and the road networks and
  scenarios shipped under `resources/`.

  Four of those — the published artifacts and their exported entry points, the
  usage text the tools print, `odrplot`'s `track.csv`, and the `--csv_logger`
  output — are observed by reading text the artifacts themselves produce rather
  than by calling an entry point, so the closing section spells them out.
  Nothing there is new work: every line of it is already true of the tree you
  receive, and the only requirement is that it is still true afterwards.

  Two of the inherited surfaces named above are read back through files the
  checker supplies rather than through text the tools print, so their input
  format is stated here as well. The distribution files it hands
  `SE_SetParameterDistribution` are OpenSCENARIO documents in one dialect: a
  `ParameterValueDistribution` naming the scenario it distributes over with a
  `<ScenarioFile filepath="…"/>` element and holding its distributions under a
  `Deterministic` block, each single-parameter distribution inside it naming
  the parameter it varies with the **`parameterName` attribute** of
  `DeterministicSingleParameterDistribution` and listing its values as
  `Element value="…"` children of a `DistributionSet`.
  And `VariableDeclarations` are read through `<VariableDeclaration>` elements
  whose type attribute is spelled **`variableType`** (`variableType="integer"`,
  `"double"`, `"string"`, `"boolean"`), with the value in `value`.

- The workspace is configured offline and headless, and the behaviour described
  above is the behaviour of *that* build: none of these requirements needs a
  disabled CMake option turned back on or a dependency fetched, and requirement
  4 is written for a build without the projection library. This is a statement
  about the environment you are working in rather than a separate thing to
  implement — nothing reads your build system, and what is observed is how the
  artifacts that build produces behave.

## What "unchanged" means, spelled out

This section covers the four items from the Notes above that are checked by
reading output the artifacts produce themselves. They are properties of the
tree you start from, written down here so that "unchanged" is checkable against
a stated form rather than against the reader's memory of the tool. None of it
asks for work: it is the shape the observations are made against.

**The six artifacts and the entry points they carry.** The build produces
`esmini`, `esmini-dyn`, `odrplot` and `dat2csv` as ELF executables and
`libesminiLib.so` and `libesminiRMLib.so` as ELF shared objects. Both public
headers are `extern "C"`, so each name below resolves from its shared library
by that plain spelling, with no C++ mangling: `SE_Init`, `SE_Close`,
`SE_StepDT`, `SE_GetObjectState`, `SE_GetSimulationTime`,
`SE_CollisionDetection` and `SE_GetObjectNumberOfCollisions` from
`libesminiLib.so`; `RM_Init`, `RM_Close`, `RM_CreatePosition`,
`RM_GetNumberOfRoads`, `RM_SetLanePosition` and `RM_GetPositionData` from
`libesminiRMLib.so`. That is a sample of the existing surface, not the whole of
it — the rule is that nothing already exported stops being exported. The road
networks and scenarios shipped under `resources/xodr` and `resources/xosc` stay
where they are.

**The usage text each tool prints.** Given `--help`, or nothing to run, each
tool identifies itself with the lines below. Three of the four are inherited
and only need to stay as they are; the `esmini-dyn` line is the one to read
carefully, because the release this ladder moves through at step 07 adds a
two-line variant banner ahead of the plain `Usage:` line and no earlier step of
the ladder asks for it, so a tree that prints only the plain line has to take
the variant form on here. What is required in every case is the text, not a
value computed from the tree:

- `esmini --help` prints `Usage: esmini [options]` and then its option list,
  which gives the scenario argument as `  [--osc] <filename>` — bracketed,
  because naming the scenario as a bare positional argument also works — and
  which lists `--headless`, `--fixed_timestep`, `--record` and `--csv_logger`
  among the rest. Those four options are what the checked runs drive the player
  with, alongside `--osc`, and they keep their present spelling and meaning.
- `esmini-dyn` with no arguments prints a line beginning
  `esmini BUILD VERSION:` and then
  `Usage variant 1: esmini-dyn <osc filename>`. The version *value* depends on
  whether the build tree carries git metadata and is out of scope; the line
  itself is required.
- `odrplot` with no arguments prints
  `Usage: ordplot openDriveFile.xodr [Output file, default=output.csv] [Sampling_step, default=1.0]`.
  `ordplot` is a transposition in the shipped string. It is part of the text
  and is not yours to correct.
- `dat2csv` with no arguments prints `Usage: dat2csv [options]` and then its
  option list. Requirement 5 adds one line to that list and leaves the others
  alone; `--file`, `--file_refs`, `--csv`, `--extended`, `--path`,
  `--print_csv`, `--help` and `--version` are the options already there.

Exit statuses for these self-describing runs are not part of the contract and
are not observed: they differ between the four tools and say nothing about the
work.

**`odrplot`'s `track.csv`.** Given an OpenDRIVE network, `odrplot` writes
`track.csv` into the current working directory — that name, not the
`output.csv` its usage line names as a default — and reports
`Created track.csv using stepsize 1.00`. The file is a sequence of per-lane
blocks. Each block opens with a header row of the form

```
lane, <road id>, <section index>, <lane id>, <driving|no-driving>
```

and is followed by one point row per sample, each row four comma-separated
floating-point values written with `%f` — x, y, z and heading. Every lane in
the network gets a block, drivable or not, in descending lane-id order. The
sampling step is one metre along the lane and both endpoints are included, so a
network of `L` lanes over a road `R` metres long produces `L` header rows and
`L x (R + 1)` point rows: sampling follows the geometry, so a longer road
yields proportionally more rows. A file that is not an OpenDRIVE network is
refused — `odrplot` exits non-zero and writes no `track.csv` at all.

What `odrplot` prints about the build it came from, and about projection
attributes it does not support, is neither fixed by this step nor observed.

**`--csv_logger`.** `esmini --csv_logger <file>` writes that file with a
six-line preamble, one column-header line, and then one row per simulation
step. The preamble is, in this order:

```
esmini GIT REV: <rev>
esmini GIT TAG: <tag>
esmini GIT BRANCH: <branch>
esmini BUILD VERSION: <version>
Scenario File Name: <the .xosc given to --osc>
Number of Vehicles: <count>
```

The first four values describe the build environment rather than this work and
are out of scope — without git metadata they read `N/A` — but the six labels,
in that order, are required.

The column header opens `Index [-], TimeStamp [s], ` and is then one block per
entity, in entity-id order. Every column of a block is prefixed `#<n> ` with
`<n>` the entity's 1-based ordinal, and each block carries these thirty-one
columns in this order:

```
Entity_Name [-], Entity_ID [-], Current_Speed [m/s], Wheel_Angle [deg],
Wheel_Rotation [-], bb_x [m], bb_y [m], bb_z [m], bb_length [m], bb_width [m],
bb_height [m], World_Position_X [m], World_Position_Y [m], World_Position_Z [m],
Vel_X [m/s], Vel_Y [m/s], Vel_Z [m/s], Acc_X [m/s2], Acc_Y [m/s2], Acc_Z [m/s2],
Distance_Travelled_Along_Road_Segment [m], Lateral_Distance_Lanem [m], lane_id,
lane_offset [m], World_Heading_Angle [rad], Heading_Angle_Rate [rad/s],
Relative_Heading_Angle [rad], Relative_Heading_Angle_Drive_Direction [rad],
World_Pitch_Angle [rad], Road_Curvature [1/m], collision_ids
```

so a scenario with `E` entities gives a header of `2 + 31 x E` fields, ending
with `#<E> collision_ids` and a trailing `, `. The shipped header is not
perfectly regular and the irregularities are part of the text, not defects to
tidy: the first entity's block writes `lane_offset[m]` with no space before the
unit where every later block writes `lane_offset [m]`, and in every block after
the first the comma before `Current_Speed [m/s]` and the one before `bb_x [m]`
are not followed by a space.

A sample row follows the same field order: an integer index counting from 0,
the timestamp, then per entity the name as text, the id as an integer,
`lane_id` as an integer, `collision_ids` empty when nothing is colliding, and
every other numeric field with **six** decimals. Requirement 5's `--precision`
is a `dat2csv` option and does not reach the CSV logger: six decimals here is
not negotiable by it.

## Scope

This environment has no network access, and everything the build needs is
already installed. The upstream project's source, its releases, mirrors and
package registries are unreachable, so a copy cannot be fetched by any
route.
