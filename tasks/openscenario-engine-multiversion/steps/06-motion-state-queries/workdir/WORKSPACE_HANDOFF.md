# Workspace handoff — step 06

## What you receive

You begin with a working esmini tree at v2.44.2 that already builds the published artifacts, runs OpenSCENARIO scenarios headless, records playback to `.dat` files, converts those recordings through `dat2csv`, renders road networks through `odrplot`, and plays a recording back through `esmini-dyn`.

The scenario-engine C ABI at this tag already publishes the scenario lifecycle (`SE_Init`, `SE_Step`, `SE_StepDT`, `SE_Close`, `SE_GetSimulationTime`), object state access (`SE_GetObjectState` with its `SE_ScenarioObjectState` struct, `SE_GetNumberOfObjects`), the legacy distance query `SE_GetDistanceToObject` with its `SE_PositionDiff` struct, and the option store: `SE_SetOptionValue`, `SE_GetOptionValue`, `SE_GetOptionSet`, `SE_SetOption` and `SE_UnsetOption`. The road-manager library publishes the lifecycle calls `RM_Init` and `RM_Close`, the network queries `RM_GetNumberOfRoads`, `RM_GetIdOfRoadFromIndex`, `RM_GetRoadLength` and `RM_GetRoadNumberOfLanes`, the position calls `RM_CreatePosition`, `RM_SetLanePosition`, `RM_SetWorldXYHPosition` and `RM_GetPositionData` (with its `RM_PositionData` struct, whose members include `hRelative`, the heading relative to the road), the lane-width queries and the lane-type queries. Both headers already publish `typedef uint32_t id_t;` — the unsigned id type the earlier steps introduced — and the step instruction's signatures use it by that name; it is inherited, not something this step adds.

Your tree builds offline with CMake options that disable OpenSceneGraph, OSI, SUMO, PROJ, ImPlot and the unit-test targets. Nothing downloads at configure or build time. Because OpenSceneGraph is disabled, `odrviewer` and `replayer` are not built and are not part of this step's published set; the published artifacts are the executables `esmini`, `esmini-dyn`, `odrplot` and `dat2csv` and the shared libraries `libesminiLib.so` and `libesminiRMLib.so`.

Note that the two shared libraries do not end up in the same directory: the install step places `libesminiLib.so` under `bin/` while `libesminiRMLib.so` remains under the build tree's `esminiRMLib` directory. Anything that links or loads both must account for that.

The fixture corpus your build is exercised against lives in `$W/fixtures/`, outside
`/app` — you never see the files, but knowing what is in them tells you what your
implementation has to cope with. Eight fixtures sit there:

- `straight_500m.xodr` — a single 500 m straight road, id 1, drawn along +x so that s maps
  directly to x. It carries seven lane records, ids 3 down to -3: a 3.07 m driving lane
  either side of the width-less centre lane 0, then a 1.68 m and a 6.00 m border lane beyond
  each of those. Only lanes 1 and -1 are drivable, and that is what the road manager reports.
  `odrplot` does not filter on it: its track file carries one `lane` record per lane,
  all seven, each tagged `driving` or `no-driving`. The right-hand driving lane is lane -1 with its
  centre at t = -1.535.
- `e6mini.xodr` — a longer curved multi-lane network, first road id 0, six lanes, used where a straight road would make an answer trivially zero.
- `curve_r100.xodr` — road id 0, 757.08 m long in three pieces: a 500 m straight, then a
  constant-radius arc 157.08 m long that bends **left** on a reference-line radius of 100 m,
  then a 100 m straight. Two driving lanes 3.07 m wide; the right-hand one is lane -1 with
  its centre at t = -1.535, the same offset `straight_500m.xodr` has. Because the arc bends
  left, a lane at negative t lies on the **outside** of it, so an entity in lane -1 travels a
  circle of radius 100 + 1.535 = **101.535 m**, not 100 m. That is the radius the closed form
  `heading rate = v / r` uses: at 20 m/s the settled heading rate is 20 / 101.535 = 0.19698
  rad/s, and the odometer measures the entity's own 20 m/s path, which is longer than the
  reference-line s it covers. The road is planar — one elevation entry with every coefficient
  zero, and a `lateralProfile` that is present but empty, holding no `superelevation`
  record, so no gradient and no bank — so pitch and roll, and their rates and
  accelerations, stay zero on it exactly as they do on the straight.
- `fabriksgatan.xodr` — a small junction network.
- `two_car_brake.xosc` — a two-entity longitudinal scenario on the straight road: ego at 20 m/s starting 100 m behind a target at 10 m/s.
- `cruise_straight.xosc` — a single entity cruising at constant speed along the straight road. The road ends before the scenario does: the entity reaches the end of it at 24.0 s, and an entity that runs off the end of its road with no successor has its speed zeroed there, so every frame from 24.1 s on — the `.dat` recording's and the `--csv_logger` table's last frames included — reports speed `0` at the road's end, while the frames before it report the commanded `20`. A tree that keeps reporting the commanded speed past the end of the road fails the gate that re-reads those frames at this step.
- `cruise_curve.xosc` and `cruise_curve_slow.xosc` — a single entity cruising around `curve_r100.xodr` at two different speeds, so that a rate that scales with speed can be told apart from one that does not.

## What you build

You grow the published C ABI in `esminiLib.hpp` and `esminiRMLib.hpp`, and the implementation behind them, with motion-state queries, a cached distance query, multi-value option counting, and heading setters for road-manager positions. You also extend the `esmini` player's command line.

The scenario engine gains five entry points. `SE_GetObjectOdometer` reports the cumulative distance an object has travelled, monotonically, matching the physical path length. `SE_GetObjectVelocityGlobalXYZ` reports the instantaneous velocity vector in global coordinates. `SE_GetObjectAngularVelocity` reports heading, pitch and roll rates — all zero on a straight road, and heading rate equal to speed divided by radius on an arc. `SE_GetObjectAngularAcceleration` reports the corresponding angular accelerations, which spike at arc entry and exit and settle during steady circular motion. `SE_SimpleGetDistanceToObject` is a distance query with a tracking limit that also takes an `SE_RelativeDistanceType` saying which distance to measure: inside the limit it recomputes every call and reports a fresh timestamp; beyond the limit its cache goes stale, which it reports distinctly rather than by returning a wrong distance. Its value agrees with the legacy distance query wherever both are valid. The step instruction gives the full parameter list and the enumerator values.

All four of the pointer-out queries above report an object id no scenario defines as a failure, through a negative return code, rather than writing a plausible value; `SE_GetObjectOdometer` has no return code to spend and answers with a NaN instead.

The option store gains `SE_GetOptionValuesCount`, which reports how many values an option name currently carries, so an option set N times counts N and an option that has been unset counts zero.

The road manager gains `RM_SetH` and `RM_SetHMode`. Both set a position handle's heading without disturbing its x, y, s, lane id or road id. `RM_SetH` treats its argument as an absolute heading; `RM_SetHMode` takes an explicit mode, where the absolute mode is 48 and the relative mode is 112. A handle that was never created is refused, again by sign.

The `esmini` player learns to read a YAML configuration file, both from a `--config_file_path` command-line argument and from the `ESMINI_CONFIG_FILE` environment variable. In either case the file is loaded, the player says so, and settings it carries — `fixed_timestep` among them — take effect. The file is keyed by the reading binary's own application name, with option names under it spelled without their leading dashes; the step instruction shows the shape. A config file that is named but cannot be read is an error and stops the run rather than falling back to defaults. The short help flag `-h` works and prints the same usage information the long form prints.

The player's argument surface also loses `--disable_pline_interpolation`, which from here on is an unrecognized argument, and `--save_xosc` grows an optional `quit`/`continue` mode. Separately, the published C ABI finishes migrating index and count parameters from signed to unsigned, `SE_RoadInfo` and the road manager's `RM_RoadLaneInfo` each grow a road-type and a road-rule member, and four older OSI ground-truth entry points leave the exported surface altogether. The step instruction lists all of these exactly.

## What carries forward

The published artifacts are still produced, at every path the earlier steps established, and every capability listed under "What you receive" still works unchanged — in particular the legacy distance queries, the legacy option store's set/get/isset/unset surface, every road-manager call named under "What you receive", and the output formats of `odrplot`, `dat2csv` and the player's `--csv_logger` option. `dat2csv` invoked with no arguments still prints its `Usage:` line and exits non-zero, and the logger's own line format — a bracketed timestamp, a bracketed level, then the message — is unchanged, with `--log_level warn` still keeping the run's `[info]` lines off stdout, and an ordinary headless run at the default level still emitting a stream of info-tagged lines rather than a token few (the floor this project publishes is at least five; the exact size of the stream is not pinned). The one `[info]` line that survives `--log_level warn` at this release is the config-file discovery notice the new configuration mechanism emits before the requested level is in force; it is a consequence of the capability this step adds, not a regression in the logger.

Your tree is at v2.46.4 after this step. The build profile is unchanged: still offline, still without OpenSceneGraph, OSI, SUMO, PROJ, ImPlot or the unit-test targets, so `odrviewer` and `replayer` are still not built.

All eight fixtures listed above are still present and still run at v2.46.4.

One caution carried forward from the earlier steps: the road manager's position setters return a family of non-negative success codes rather than only zero, and have done since 2.21.2. Callers distinguish success from failure by the sign. Narrowing success back to exactly zero is a regression.
