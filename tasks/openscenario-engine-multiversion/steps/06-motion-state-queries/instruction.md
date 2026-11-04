# Step 06 - Motion State Queries

Upgrade the esmini scenario engine from v2.44.2 to v2.46.4 (release 2.46.4).

This step introduces motion state query functions, cached distance queries with tracking limits, multi-value option counting, heading setters for road-manager positions, and YAML configuration file support. It also retires one command-line option and completes a set of index/id type migrations in the published C ABI.

## Where the rest of the contract is

- `TEST_INTERFACE.md` in your workspace states how the checker builds your tree, which six artifacts it looks for, which header file names it includes, and that every entry point it calls must be exported with **C linkage**. It defers the *signatures* to this file; the signature of every entry point this step **adds or changes** is written out below, along with the two inherited road-info queries requirement 18's grown structs are read back through. Entry points this step neither adds nor changes keep the signatures they already have at v2.44.2 — `WORKSPACE_HANDOFF.md` names them under "What you receive" — and must keep them.
- `WORKSPACE_HANDOFF.md` states what you start from, which fixtures the checker uses, and what must still work unchanged when you are done.
- `PROFILE.md` states the build profile (OSG, OSI, SUMO, PROJ, ImPlot and the unit-test targets are all off).

Signatures below are the ones the checker's own C++ drivers are compiled against. A parameter list that differs — an extra parameter, a missing one, a different order — does not compile, and a group whose driver does not compile is not satisfied.

## Requirements

1. **SE_GetObjectOdometer** - `float SE_GetObjectOdometer(int object_id)`. Query cumulative distance traveled by a scenario object. The odometer must be monotonic and match the physical distance traveled along the object's path. For an object id no scenario defines there is no return code to spend, so the answer must be a NaN rather than a plausible distance.

2. **SE_GetObjectVelocityGlobalXYZ** - `int SE_GetObjectVelocityGlobalXYZ(int object_id, float* x, float* y, float* z)`. Query instantaneous velocity components in global XYZ coordinates. For straight-line motion at constant speed, vx/vy/vz must match the expected direction and magnitude. For curved paths, components must reflect the instantaneous velocity vector. Returns `0` on success.

3. **SE_GetObjectAngularVelocity** - `int SE_GetObjectAngularVelocity(int object_id, float* h_rate, float* p_rate, float* r_rate)`. Query instantaneous angular velocity (heading rate, pitch rate, roll rate). For straight roads, all rates must be zero. For circular arc motion, heading rate must match v/r (speed divided by radius) — where `r` is the radius of the circle **the object itself** describes, not the radius of the road's reference line. The two differ whenever the lane the object drives in is laterally offset from the reference line: a lane on the outside of the bend adds its offset to the reference-line radius and a lane on the inside subtracts it. `WORKSPACE_HANDOFF.md` gives the geometry and the lane offset of the arc fixture the checker drives, and therefore the radius the closed form resolves to on it. Pitch and roll rates stay zero on any planar road without superelevation, whether it is straight or curved. Returns `0` on success.

4. **SE_GetObjectAngularAcceleration** - `int SE_GetObjectAngularAcceleration(int object_id, float* h_acc, float* p_acc, float* r_acc)`. Query instantaneous angular acceleration. Must spike at arc entry/exit transitions and remain near zero during steady circular motion. That applies to the heading acceleration; the pitch and roll accelerations stay zero throughout on a planar road without superelevation, on the arc exactly as on the straight. Returns `0` on success.

5. **SE_SimpleGetDistanceToObject** - cached distance query with tracking limit:

   ```cpp
   typedef enum
   {
       REL_DIST_UNDEFINED    = 0,
       REL_DIST_LATERAL      = 1,
       REL_DIST_LONGITUDINAL = 2,
       REL_DIST_CARTESIAN    = 3,
       REL_DIST_EUCLIDIAN    = 4
   } SE_RelativeDistanceType;

   int SE_SimpleGetDistanceToObject(const int               object_a_id,
                                    const int               object_b_id,
                                    SE_RelativeDistanceType dist_type,
                                    const double            tracking_limit,
                                    double                 *distance,
                                    double                 *timestamp);
   ```

   The `dist_type` parameter selects which distance is measured; the checker constructs it by casting an integer ordinal, so the enumerator **values** above are part of the contract and not only the names. Only `REL_DIST_EUCLIDIAN` (`4`) is in scope.

   When objects are within the tracking limit, distance is recomputed every call (return code `0`, timestamp equal to the simulation time). When beyond the limit, the cache goes stale: the call returns `-2` and hands back the frozen distance and the frozen timestamp of the last sample it did compute, rather than a wrong distance with a fresh-looking timestamp. Distance value must agree with the legacy SE_GetDistanceToObject query.

   Two consequences of "the last sample it did compute" that the checker relies on, and that the two branches above do not by themselves settle:

   - **The first call for a pair always computes a sample**, whether or not the pair is already beyond the tracking limit when it arrives — and, because it computes one, **it returns `0`**, not `-2`. A pair that starts beyond the limit therefore still gets exactly one real measurement, taken at the simulation time of that first call, for every later call to be served from; there is no third "nothing to hand back yet" answer. `-2` is the answer for a call *served from* an existing sample, so only the calls after the first can return it.
   - Having taken a sample with the pair beyond the limit, the query **holds that sample for three seconds of simulation time** before it recomputes, rather than recomputing on every call. Over a run shorter than that, a pair that never comes inside the limit is measured exactly once and every later call returns the frozen distance, the frozen timestamp and `-2`.

6. **SE_GetOptionValuesCount** - `int SE_GetOptionValuesCount(const char* name)`. Count how many values have been set for a multi-value option. After setting the same option name N times, the count must be N. After unsetting, the count must return to 0. A name with no values stored counts `0` — whether it was unset or never set in the first place. That is a count and not a rejection, so it is reported as `0` rather than through the negative-on-failure convention below.

    Storing more than one value under a name forces a decision the single-valued store never had to make, and this release makes it explicitly: the **last** value set for a name has priority. `SE_GetOptionValue`, which still hands back one value rather than a list, therefore returns the **most recently set** one — so a name set to `alpha`, then `beta`, then `gamma` counts 3 and reads back `gamma`. (Requirement 13's "must work as before" constrains the single-valued behaviour, which is unaffected: with one value stored, the most recent value and the only value are the same one.)

7. **RM_SetH** - `int RM_SetH(int handle, float h)`. Set the heading of a road-manager position handle without changing x, y, s, lane_id, or road_id. The plain form treats the input as absolute heading.

8. **RM_SetHMode** - `int RM_SetHMode(int handle, float h, int mode)`. Set heading with explicit mode. Mode 48 (RM_H_ABS) sets absolute heading; mode 112 (RM_H_REL) sets heading relative to the road heading at that position. As with RM_SetH, position coordinates must remain unchanged. Read back through `RM_GetPositionData`, the `hRelative` member is reported in a fixed interval: it is wrapped into `[0, 2π)` before it is stored or returned, whichever mode was used, so a relative heading that resolves to a small negative angle comes back as that angle plus a full turn — a nominal `-0.5` rad relative heading reads back as `5.78319` (`2π − 0.5`). (The absolute `h` member is not wrapped this way; it is reported as set.)

9. **--config_file_path** - Load a YAML configuration file via command-line argument. The file must be loaded — logged as the message `Loaded config file` followed by the path, the same shape requirement 10 uses for the environment variable — and settings like fixed_timestep must take effect. The player also looks for a default configuration file next to the executable; finding none is not an error, and the notice it logs in that case begins "Ignoring missing default config". See "The configuration file format" below.

    The part of this requirement that is in scope is the explicit path: the checker passes `--config_file_path` a file it wrote itself and asserts the file is loaded and its `fixed_timestep` takes effect, and separately that a path naming a missing file stops the run. The default-file lookup is described here for completeness because it is part of the same mechanism, but it is **not** separately required, and no group should be read as testing it. The reason is that which of its two branches runs is not something the checker fixes. The path resolves next to the artifact doing the reading — that artifact's own directory, joined with `..` and `config.yml` — and `TEST_INTERFACE.md` deliberately does not fix where in your tree each artifact ends up. In the reference layout the two artifacts the checker uses land in different places and answer differently: the `esmini` executable resolves the lookup to a path that holds no such file and logs the absence notice, while `libesminiLib.so`, which the checker's C++ drivers link against, resolves it to a file that does exist. A candidate that implemented `--config_file_path` and no default search at all could not be told apart from one that implemented both, so neither branch is in scope.

    That lookup does carry one obligation, which is a constraint rather than a checked behaviour. Every C++ driver the checker runs links your `libesminiLib.so`, so each of those runs reads whatever default configuration file sits next to it, and settings from that file are appended to the run's arguments exactly as if they had been typed. A default configuration carrying run-affecting settings — `fixed_timestep` and `log_level` are the two that bite — therefore changes what the checker measures in groups that have nothing to do with configuration files. Keep any default `config.yml` your tree carries free of settings that change how a run executes or what it logs.

10. **ESMINI_CONFIG_FILE** - Load a YAML configuration file via environment variable. When set, the engine must log "Found environment variable ESMINI_CONFIG_FILE:" followed by the path, and apply the configuration. When the variable is not set, that line must not be printed.

11. **-h short help** - The short help flag must work and display the same usage information `--help` displays, byte for byte. That block opens with a line matching `Usage: … [options]` and lists `--osc <filename>` among its options — the spellings the earlier releases print and this one keeps. Like the long form it is help rather than a run: it prints the usage block and stops, without loading a scenario or a road network, so nothing of a run's own loading output appears alongside it. (That includes the configuration-file notices of requirement 9: `-h` prints the usage block and exits before any configuration file is looked for, so neither the `Loaded config file` notice nor the `Ignoring missing default config` one appears alongside it.)

12. **Legacy distance queries** - SE_GetDistanceToObject and SE_GetObjectState must continue to work unchanged. Two-car braking scenario must produce expected separation distances and object speeds.

13. **Legacy option store** - SE_SetOptionValue, SE_GetOptionValue, SE_GetOptionSet, and SE_SetOption must work as before. Set/get/isset operations must produce expected results.

14. **Legacy road-manager** - the road-manager surface that already exists at v2.44.2 must work unchanged. In particular the network queries `RM_GetNumberOfRoads`, `RM_GetIdOfRoadFromIndex`, `RM_GetRoadLength` and `RM_GetRoadNumberOfLanes`, the position calls `RM_CreatePosition`, `RM_SetLanePosition`, `RM_SetWorldXYHPosition` and `RM_GetPositionData`, and the lifecycle calls `RM_Init` and `RM_Close`. Lane position placement, world-coordinate placement and coordinate queries must succeed, and a position set by lane and read back as world coordinates must map back onto the same road and lane.

15. **CLI output formats** - csv_logger header format, dat2csv conversion, odrplot track generation and the logger's own line format must produce unchanged output. Specifically:

    - `dat2csv` given no arguments prints `Usage: <program> <filename>` and exits `255`.
    - The `--csv_logger` header keeps the separator spelling it already has: a space
      **before** each comma as well as after it, and a trailing `, ` closing the line —
      `Index [-] , TimeStamp [s] , #1 Entity_Name [-] , ` and so on. Tidying that to
      `Index [-], TimeStamp [s], ` is a change to the format, not a cosmetic one, and is
      not part of this step. **One column is the exception and upstream writes it without
      the space**: the lane column, which reads `#1 lane_id, #1 lane_offset[m] ,` — no
      space before `lane_id`'s comma. It is the one column a reimplementation normalising
      the separator would "fix", so it is named here.
    - The logger writes one line as a bracketed timestamp, then a bracketed level, then
      the message — `[0.100] [info] message`. The levels in use are `info`, `warn` and
      `error`. A logger that prints almost nothing satisfies "prints lines in that form"
      and satisfies nothing else, so the quantity is part of the requirement too: an
      ordinary headless scenario run at the default (`info`) level emits a **stream** of
      info-tagged lines, not a token one or two, and **at least five** of them — the
      floor this project has published since the logging system landed. Its exact size
      is not pinned and is no part of this contract; the floor is what is checked.
    - Every scenario the checks drive is started `--osc <filename> --headless`, and
      `--fixed_timestep <seconds>` is added wherever a run has to be reproducible.
      `--osc` names the OpenSCENARIO file and is required. None of the three changes in
      this step, but this step adds to the argument surface (requirements 9, 10 and 11 - `--config_file_path`, `ESMINI_CONFIG_FILE` and `-h`),
      so they are named here rather than left to be inherited silently: an argument-parser
      rewrite that drops one of them fails checks that never mention it.
    - The `--csv_logger` file opens with six preamble lines before the header row, in
      this order: `esmini GIT REV:`, `esmini GIT TAG:`, `esmini GIT BRANCH:`,
      `esmini BUILD VERSION:`, `Scenario File Name:` and `Number of Vehicles:` — so
      `esmini BUILD VERSION: ` begins the fourth line of the file. Those first four
      lines are the same four `esmini` itself prints on stdout at start-up. Run with no
      arguments, `esmini-dyn` prints an `esmini BUILD VERSION: ` line followed by a
      `Usage:` line, and that pair is what is read there - no check says anything about
      what `esmini-dyn` prints on any other path. The values differ in origin: the three
      `GIT` lines are git-derived and read `N/A` when the tree is not a checkout, while
      `BUILD VERSION` keeps its build-time default, the literal `N/A - client build`,
      unless a CI build overrides it. The lines and their spelling are what must keep
      working, never a particular value.
    - `--log_level warn` keeps the run's `[info]` lines off stdout while still reporting
      `[error]` lines in that same format. The config-file discovery notice described
      under requirement 9 is emitted before the requested level is in force and is the
      one `[info]` line that still appears; it is not part of the run's own logging.
    - The `[error]` line that demonstrates this is one the road-network parser has emitted
      since long before v2.44.2 and still emits unchanged here: each of the three road
      networks the checker drives — `straight_500m.xodr`, `curve_r100.xodr` and
      `e6mini.xodr` — carries a `geoReference` with a PROJ attribute the parser does not
      consume, so loading any of them logs `Unsupported geo reference attr: <attr>` once at
      `[error]` level. (It is a property of those networks, not of every OpenDRIVE file:
      the corpus also holds a junction network that declares no `geoReference` and
      therefore logs no such line. No group loads it.) It is named here because the
      requirement that a level filter still lets errors through has to point at an
      error the run actually produces, and this is the one. Keeping it is a matter of
      not regressing the parser, not of adding anything.

16. **CLI artifacts** - All six published artifacts must be present, be the right kind of
file, and run: the executables `esmini`, `esmini-dyn`, `odrplot` and `dat2csv`, and the
shared libraries `libesminiLib.so` and `libesminiRMLib.so`. (`odrviewer` and `replayer`
are OpenSceneGraph applications and are not built under the build profile, which sets
`USE_OSG=False`; `csv_logger` is an `esmini` option rather than a binary and is covered
by requirement 15.)

17. **Command-line surface changes** - three changes to the `esmini` player's argument surface:

    - `--disable_pline_interpolation` is **removed**. Passing it must land in the unrecognized-argument path: the player prints `Unrecognized arguments:` followed by the offending argument and the usage block, and exits non-zero without running the scenario. Rejection comes **after** the output options have been opened, though: the recorder is opened while the command line is being read, so an invocation that also carried `--record <path>` still leaves that file behind — a `.dat` with its `Version:` preamble and its column header and then no data rows, which `dat2csv` converts to a header-only CSV and exits `0` on. Not running the scenario means no frames are recorded; it does not mean the outputs the command line asked for are never created.
    - `--save_xosc` gains an optional mode argument, `--save_xosc [mode]`, whose values are `quit` and `continue` and whose default when the value is omitted is `continue`. Both modes write the resolved scenario to a `.xosc` file in the current directory. The name is the scenario's basename with a parameter-distribution counter inserted before the extension: `<basename>_<index>_of_<count>.xosc`. The counter comes from the parameter-distribution machinery, which is still in the path even when the scenario declares no distribution file: `index` is the distribution's zero-based position plus one, and `count` is the number of permutations it enumerated. For a scenario without a distribution file both are `0`, so a scenario named `cruise_straight.xosc` is written out as `cruise_straight_0_of_0.xosc`. `quit` exits after writing it, and exits **successfully** — status `0`, because writing the file is what was asked for and it was done — so a `--record` given on the same command line produces no `.dat` file; `continue` writes the file and then runs the scenario normally, producing the `.dat`, and also exits `0`. (`esmini --help` exits non-zero in this codebase, so "exits" is not on its own "exits 0"; here it is.)
    - `--window <x> <y> <w> <h>` gains an ordering rule against `--headless`. The two are opposites — one asks for a viewer window, the other asks for none — and at v2.44.2 a `--window` anywhere on the command line won whatever the order was. At this release they resolve in command-line order and the last one wins, so `--window` must appear **after** any `--headless` for the window to be requested at all. The build profile cannot honour the request: `TEST_INTERFACE.md` §5 fixes `USE_OSG=False`, so no viewer is compiled in. The option is still parsed and the pair is still resolved, and the outcome is reported — a run that ends up requesting a window logs `window requested, but esmini compiled without OSG capabilities` at `[warn]` level, and a run whose window request was cancelled by a later `--headless` logs no such line. That log line is the only observable the profile leaves, so it is the one required; it must appear with `--headless --window ...` and must not appear with `--window ... --headless`.

18. **Index and id type migration** - the published C ABI finishes moving indices and counts to unsigned types, grows two road-info structs, and drops four OSI entry points:

    - `unsigned int SE_GetNumberOfPermutations()` and `int SE_SelectPermutation(unsigned int index)` (both were `int`-based).
    - `int SE_GetRoadSign(id_t road_id, unsigned int index, SE_RoadSign* road_sign)` and `int SE_GetRoadSignValidityRecord(id_t road_id, unsigned int signIndex, unsigned int validityIndex, SE_RoadObjValidity* validity)` (the index parameters were `int`).
    - `SE_RoadInfo` gains `int road_type` and `int road_rule` as its last two members, and the road manager's `RM_RoadLaneInfo` gains the same two members in the same position. Both carry the OpenDRIVE `type` and `rule` entries. The two queries the checker fills those structs through are inherited unchanged from v2.44.2, and it compiles against them as:

      ```cpp
      int SE_GetRoadInfoAtDistance(int object_id, float lookahead_distance,
                                   SE_RoadInfo* data, int lookAheadMode,
                                   bool inRoadDrivingDirection);
      int RM_GetLaneInfo(int handle, float lookahead_distance,
                         RM_RoadLaneInfo* data, int lookAheadMode,
                         bool inRoadDrivingDirection);
      ```

      A road that declares no `type` and takes the default right-hand rule reports `0` for both new members, which is what the checker reads back on this step's networks.
    - `SE_ClearOSIGroundTruth`, `SE_UpdateOSIGroundTruth`, `SE_UpdateOSIStaticGroundTruth` and `SE_UpdateOSIDynamicGroundTruth` are **removed** and must no longer appear in `libesminiLib.so`'s dynamic symbol table.

## How failures are reported

Every entry point above that returns an `int` status reports failure with a **negative** value and success with `0` (the road-manager position setters may also report the non-negative transition codes described in `WORKSPACE_HANDOFF.md`). Which negative value is yours to choose; the checker asserts the sign, not the magnitude, except where a specific code is named in a requirement above — `SE_SimpleGetDistanceToObject`'s `-2` for a stale sample is the one such code.

Concretely, the checker exercises these rejection paths:

- `SE_GetObjectVelocityGlobalXYZ`, `SE_GetObjectAngularVelocity`, `SE_GetObjectAngularAcceleration` and `SE_SimpleGetDistanceToObject` with an object id no scenario defines must report failure rather than writing a plausible value and returning success.
- `SE_GetObjectOdometer` has no status to return, so the same case must produce a NaN (requirement 1).
- `RM_SetH` and `RM_SetHMode` on a handle that was never created must report failure.
- A `--config_file_path` naming a file that does not exist is an error, not a silent fall-back to defaults: the player must exit non-zero rather than run the scenario with built-in settings.

## The configuration file format

The YAML file read by `--config_file_path` and by `ESMINI_CONFIG_FILE` is keyed by
**the application name of the binary reading it**, so a file read by `esmini` has
`esmini:` as its single top-level key. Entries nested under that key are ordinary
command-line option names written **without their leading dashes**, each with the value
the option would have taken on the command line:

```yaml
esmini:
  fixed_timestep: 0.05
  headless: true
```

is equivalent to passing `--fixed_timestep 0.05 --headless`. A boolean `true` contributes
the bare flag; a `false` contributes nothing. Entries under any other top-level key are
ignored, which is what lets one file configure several esmini binaries. Settings from the
file are appended to the process arguments, so they take effect exactly as if they had
been typed.

## Out of scope for this step

The following belong to this release upstream but are **out of scope here**, and no group
tests them. They are listed so that the absence is a decision rather than an oversight:

- `--pline_interpolation <mode>` and `--ignore_heading_for_traj_motion`. Both are only
  observable on a scenario driving a polyline trajectory, and none of this step's fixtures
  contains a trajectory.
- The "last duplicate parameter wins" rule for a parameter declared more than once in a
  scenario, and the "do not collide with ghosts" change. Neither has a fixture of the
  required shape in this step's corpus.
- The `--csv_logger` rule that column blocks are ordered by entity id rather than assuming
  id 0 comes first. Only one of this step's fixtures has more than one entity, and it
  declares them in ascending id order, so the two orderings cannot be told apart here.
- The four OSI entry points `SE_CropOSIDynamicGroundTruth`, `SE_ExcludeGhostFromGroundTruth`,
  `SE_SetOSIStaticReportMode` and `SE_SetOSIFrequency`, and the `--osi_*` options that go
  with them: OSI is switched off in the build profile. (The *removal* of four older OSI
  entry points is required — see requirement 18 — because a removal is observable whether or
  not OSI is compiled in.)

## Requirement groups

This step's requirements are organised in two kinds of group:

- **Gate groups**, covering requirements 12 through 16: the road-manager,
  option-store and legacy distance surfaces inherited from v2.44.2, and the CLI
  output formats and published artifacts. These are inherited obligations and
  are already satisfied at the start tag; they must all remain satisfied. If
  any one of them fails, this step is not satisfied however complete the new
  work is, while keeping them satisfied is a prerequisite rather than the
  milestone's work.
- **Requirement groups**, covering requirements 1 through 11, 17 and 18: the
  APIs and options introduced at v2.46.4. Each of these is in scope.

A group is satisfied only if every case in it passes.

Every one of the eighteen requirements above is covered by at least one group,
requirements 17 and 18 included. Requirement 18 is checked in three ways at
once, because its three changes are observable in three different places: the
migrated parameter and return types are checked by compiling against them (a
tree that still declares the old signatures fails to build that group's driver,
which leaves the group unsatisfied), the two appended struct members are checked by
reading them back after a real query, and the four withdrawn OSI entry points
are checked as an absence from `libesminiLib.so`'s dynamic symbol table.

At the start tag (v2.44.2) every gate group is already satisfied and no
requirement group is, on missing symbols or unrecognized options. A workspace
left untouched therefore satisfies none of the new work: preserving what you
were handed is necessary, and on its own it completes nothing.

At the target tag (v2.46.4) every group is satisfied.

## Scope

This environment has no network access, and everything the build needs is
already installed. The upstream project's source, its releases, mirrors and
package registries are unreachable, so a copy cannot be fetched by any
route.
