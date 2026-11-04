# Workspace handoff — step 05

## What you receive

Your `/app` workspace is a built copy of the candidate's step 04 submission that passed all step 04 checks. It contains:

- All six target binaries (`esmini`, `esmini-dyn`, `odrplot`, `dat2csv`, `libesminiLib.so`, `libesminiRMLib.so`)
- The public headers `esminiLib.hpp` and `esminiRMLib.hpp` under `EnvironmentSimulator/Libraries/`
- The `resources/` directory with upstream scenarios, roads, and schemas
- All source files, including those under `externals/`
- The CMake build system configured for offline headless operation

The C ABI you receive already publishes everything the step's checks lean on besides the new work below, and all of it must keep working. On the scenario-engine side that is the lifecycle (`SE_Init`, `SE_Step`, `SE_StepDT`, `SE_Close`, `SE_GetSimulationTime`), object access (`SE_GetNumberOfObjects`, and `SE_GetObjectState` with its `SE_ScenarioObjectState` struct) and the lookahead query `SE_GetRoadInfoAtDistance` with its `SE_RoadInfo` struct. On the road-manager side it is `RM_Init`, `RM_Close`, `RM_CreatePosition`, `RM_DeletePosition`, `RM_SetWorldPosition`, `RM_SetLanePosition` and `RM_GetPositionData` with its `RM_PositionData` struct. None of these are new here and none of them is renamed; the change this step makes to them is the `id_t` retyping of their road and junction id fields (see below), and apart from that no member of the structs they fill is renamed, reordered, dropped or given a new meaning.

Note that the two shared libraries do not end up in the same directory: the install step places `libesminiLib.so` under `bin/` while `libesminiRMLib.so` remains under the build tree's `esminiRMLib` directory. Anything that links or loads both must account for that.

If your target release requires a vendored source that is not already present (specifically `externals/spdlog`), the checker supplies that one at the pinned upstream commit before your build begins. It supplies no other third-party source; anything else the build needs has to be vendored by you, from the read-only copies `/opt/esmini-vendor` holds. A copy you place yourself travels forward and the checker's does not.

## What you deliver

Your modified `/app` must build offline and headless with the same CMake invocation documented in `TEST_INTERFACE.md`, producing the same six artifacts at their expected locations.

**New entry points you must add:**

To `esminiLib.hpp`:
- `SE_SetOption`, `SE_SetOptionPersistent`, `SE_SetOptionValue`, `SE_SetOptionValuePersistent`, `SE_GetOptionSet`, `SE_GetOptionValue`, `SE_UnsetOption` — runtime option store with persistence
- `typedef uint32_t id_t;` and `#define SE_ID_UNDEFINED 0xffffffff` — the unsigned id type and the "undefined id" sentinel, declared here as well as in `esminiRMLib.hpp`
- `SE_InjectSpeedAction`, `SE_InjectLaneChangeAction`, `SE_InjectLaneOffsetAction`, `SE_InjectedActionOngoing` — injected actions, together with the structs `SE_SpeedActionStruct`, `SE_LaneChangeActionStruct` and `SE_LaneOffsetActionStruct` (layouts and code domains in instruction R3)
- `SE_GetObjectNumberOfWheels`, `SE_GetObjectAcceleration` — extended object queries
- `SE_GetNumberOfVariables`, `SE_GetVariableName` — variable enumeration
- `SE_GetRouteTotalLength` and `SE_GetObjectRouteStatus` — route length and route status
- `SE_SetObjectPositionMode`, `SE_SetObjectPositionModeDefault` — the `esminiLib` mirror of the position-mode family, taking an `SE_PositionModeType` where the RM_ entry points take the `RM_PositionMode` bitmask
- `SE_ReportObjectPosMode` — externally reported pose with an explicit position mode, `mode` being the same bitmask instruction R7 defines
- `SE_CloseLogFile` — releases the log file the logger holds open; it is what makes the `--logfile_path` truncation rule of instruction R6 observable across runs

To `esminiRMLib.hpp`:
- `typedef uint32_t id_t;` and `#define RM_ID_UNDEFINED 0xffffffff` — the new unsigned id type used by the string-id entry points and by `RM_PositionData.roadId` / `.junctionId`, and the all-ones "undefined id" sentinel spelled with this library's prefix
- `RM_GetRoadIdString`, `RM_GetJunctionIdString`, `RM_GetRoadIdFromString`, `RM_GetJunctionIdFromString` — string road/junction id mapping
- `RM_SetOption`, `RM_SetOptionPersistent`, `RM_SetOptionValue`, `RM_SetOptionValuePersistent`, `RM_GetOptionSet`, `RM_GetOptionValue`, `RM_UnsetOption` — shared option store
- the `RM_PositionMode` enum (`RM_Z_ABS`, `RM_Z_REL`, `RM_H_ABS`, ...) and `RM_SetObjectPositionMode`, `RM_SetObjectPositionModeDefault`, `RM_SetWorldPositionMode` — position-mode bitmask family replacing the removed align-mode setters (prototypes and value domains in instruction R7). None of these exist in the workspace you receive.

**Removed entry points:**
- `RM_SetAlignMode`, `RM_SetAlignModeH`, `RM_SetAlignModeP`, `RM_SetAlignModeR`, `RM_SetAlignModeZ` must all be removed from `esminiRMLib.hpp` and from the exported symbols of `libesminiRMLib.so`; the mirrored `SE_SetAlignMode`, `SE_SetAlignModeH`, `SE_SetAlignModeP`, `SE_SetAlignModeR`, `SE_SetAlignModeZ` must be removed from `esminiLib.hpp` and from `libesminiLib.so`. Code that calls any of them must fail to link. Their replacement is the position-mode bitmask family you add above.

**CLI changes:**
- `esmini` must accept `--csv_logger [<filename>]` and `--record [<filename>]` in optional-argument form
- `esmini` must accept `--disable_pline_interpolation`, which turns off orientation interpolation along polyline trajectories

**Existing structs whose id fields change type:**
- `SE_ScenarioObjectState.roadId` / `.junctionId`, `SE_RoadInfo.roadId` / `.junctionId` and `RM_PositionData.roadId` / `.junctionId` all move from `int` to `id_t`. Their `laneId` members stay signed `int`. An undefined road or junction is reported as the all-ones sentinel, not as `-1`: `SE_GetRoadInfoAtDistance` must set `SE_RoadInfo.junctionId` to `SE_ID_UNDEFINED` when the lookahead target is not inside a junction.

**File format changes:**
- The CSV recording header must spell "Entity" correctly (no longer "Entitity")
- OpenDRIVE files with string `id` attributes on `<road>` and `<junction>` elements must load successfully

**Logging backend:**
- Replace the custom logger with `spdlog`. Log output must respect the `log_level`, `log_meta_data`, `log_append`, `log_only_modules`, and `log_skip_modules` runtime options.

## What carries forward to step 06

Your entire `/app` workspace, including:
- All modified sources
- The vendored `externals/spdlog` if you placed it (a checker-supplied copy does not carry forward)
- The `resources/` directory

The step 06 author will receive your workspace in the same state you left it, so preserve build stability: the same CMake invocation must continue to work.
