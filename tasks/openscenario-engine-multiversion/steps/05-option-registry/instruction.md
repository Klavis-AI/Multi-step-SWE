# Step 05 instruction: Option registry and scenario extensions

You are extending the esmini scenario engine with a runtime option registry, string-based road identifiers, injected actions, extended object queries, an optional-argument CLI syntax, and an spdlog-backed logging system.

## R1. String road and junction identifiers

The road manager must accept OpenDRIVE files whose `<road>` and `<junction>` elements use string `id` attributes instead of numeric values, and must expose the mapping between an internal numeric id and the original string in both directions.

Add these entry points to **esminiRMLib.hpp**:

```cpp
extern "C" const char* RM_GetRoadIdString(id_t road_id);
extern "C" const char* RM_GetJunctionIdString(id_t junction_id);
extern "C" id_t RM_GetRoadIdFromString(const char* road_id_str);
extern "C" id_t RM_GetJunctionIdFromString(const char* junction_id_str);
```

- `RM_GetRoadIdString` and `RM_GetJunctionIdString` return the original string identifier for the given internal id, or an empty string if the id is unmapped.
- `RM_GetRoadIdFromString` and `RM_GetJunctionIdFromString` map a string identifier to the internal id. An `id` whose text is a number keeps that number as its internal id (road `id="1"` is internal id `1`); an `id` that is **not** a number takes the next free internal id, assigned sequentially in the order the elements of its own family are read from the file, starting at `0` and skipping any value already held by a numeric id. **Roads and junctions have separate id spaces**: road ids are allocated among roads in road file order and junction ids among junctions in junction file order, so the first non-numeric junction of a file is internal id `0` however many roads precede it, and neither family consumes the other's numbers. Return the undefined-id sentinel `0xffffffff` if the string is not found. That value has a name in each public header and the checks use it by name: `RM_ID_UNDEFINED` in **esminiRMLib.hpp** and `SE_ID_UNDEFINED` in **esminiLib.hpp** (R7 defines both, together with the `id_t` type these four entry points return).

The binary **odrplot** must successfully sample and output geometry from OpenDRIVE files containing string road identifiers.

## R2. Runtime option store with persistence

The scenario engine and road manager must expose a shared, process-global runtime option registry. An option is either a **presence flag** (set or not, no value) or **string-valued** (set with an associated string). Both kinds may additionally be **persistent**: a persistent option survives an `SE_Close` followed by a new `SE_Init` in the same process, while a plain (non-persistent) option is cleared by that cycle.

Add these entry points to **esminiLib.hpp**:

```cpp
extern "C" int SE_SetOption(const char* optionName);
extern "C" int SE_SetOptionPersistent(const char* optionName);
extern "C" int SE_SetOptionValue(const char* optionName, const char* value);
extern "C" int SE_SetOptionValuePersistent(const char* optionName, const char* value);
extern "C" bool SE_GetOptionSet(const char* optionName);
extern "C" const char* SE_GetOptionValue(const char* optionName);
extern "C" int SE_UnsetOption(const char* optionName);
```

Add the mirrored entry points to **esminiRMLib.hpp**: `RM_SetOption`, `RM_SetOptionPersistent`, `RM_SetOptionValue`, `RM_SetOptionValuePersistent`, `RM_GetOptionSet`, `RM_GetOptionValue`, `RM_UnsetOption`, with the same signatures and semantics.

Semantics:
- `SetOption`/`SetOptionPersistent` mark an option as set, with no value.
- `SetOptionValue`/`SetOptionValuePersistent` store a string value under the option name (also marking it set).
- `GetOptionSet` reports whether the option is currently set. `GetOptionValue` returns the stored string. For a name that is not set at all it returns `nullptr`; what it returns for a name that *is* set but carries no value — one registered by `SetOption`/`SetOptionPersistent` — is not part of this contract and is out of scope (this release answers an empty string there, and either answer passes).
- `UnsetOption` clears a presence flag so a subsequent `GetOptionSet` reports false. What it does to a string-valued option is not part of this contract and is out of scope: the check that reaches this entry point uses a flag.
- The SE and RM option stores are the same underlying store: setting an option through an `SE_*` entry point makes it visible through the corresponding `RM_*` entry point, and vice versa.
- A plain (non-persistent) option is cleared when the store is reset; a persistent option is not. The store is reset as part of an `SE_Close`/`SE_Init` cycle.
- **How the `int` returns of this family are read.** The four `Set…` entry points and `UnsetOption` return an `int` status: exactly `0` on success, and a negative value on failure (this release answers `-1`). Do not return a non-zero "success" code from them — a checker asserts `0` for a successful store and the *sign* for a failing one, so the magnitude of a failure is not pinned. The one failure this release produces is an attempt to store a value under a name that was already registered as a pure presence flag with no argument slot; a name the call registers itself always succeeds. **How a name comes to be a pure presence flag, since a checker has to be able to reach that failure:** `SetOption` / `SetOptionPersistent` register a name they have not seen before *as a presence flag, with no argument slot*, so the sequence `SE_SetOption("x")` and then `SE_SetOptionValue("x", "v")` is exactly that failure and must answer negative — the second call must not silently grow an argument slot onto a name registered without one. `SetOptionValue` / `SetOptionValuePersistent` on a name nobody has registered yet register it *with* an argument slot, which is why storing a value under a fresh name succeeds. The same convention, and the same failure, applies to the mirrored `RM_` entry points. This is deliberately narrower than the position setters of "What must keep working", which may return any non-negative value on success: the option store returns `0`.

## R3. Injected actions

The scenario engine must support injecting actions into a running scenario from the C ABI, taking effect immediately and able to be superseded by a later injection of the same kind. Three kinds are injectable: a longitudinal speed action, a lane change and a lane offset.

Add these entry points to **esminiLib.hpp**:

```cpp
struct SE_SpeedActionStruct
{
    int   id;                // id of the object to perform the action
    float speed;             // target speed, m/s (absolute)
    int   transition_shape;  // 0 = cubic, 1 = linear, 2 = sinusoidal, 3 = step
    int   transition_dim;    // 0 = distance, 1 = rate, 2 = time
    float transition_value;  // metres, m/s^2 or seconds according to transition_dim
};

struct SE_LaneChangeActionStruct
{
    int   id;                // id of the object to perform the action
    int   mode;              // 0 = absolute target lane id, 1 = relative to the object's own lane
    int   target;            // target lane id, read according to `mode`
    int   transition_shape;  // 0 = cubic, 1 = linear, 2 = sinusoidal, 3 = step
    int   transition_dim;    // 0 = distance, 1 = rate, 2 = time
    float transition_value;  // metres, m/s^2 or seconds according to transition_dim
};

struct SE_LaneOffsetActionStruct
{
    int   id;                // id of the object to perform the action
    float offset;            // target lateral offset from lane centre, metres (signed)
    float maxLateralAcc;     // upper bound on lateral acceleration during the move, m/s^2
    int   transition_shape;  // 0 = cubic, 1 = linear, 2 = sinusoidal, 3 = step
};

extern "C" void SE_InjectSpeedAction(SE_SpeedActionStruct* action);
extern "C" void SE_InjectLaneChangeAction(SE_LaneChangeActionStruct* action);
extern "C" void SE_InjectLaneOffsetAction(SE_LaneOffsetActionStruct* action);
extern "C" bool SE_InjectedActionOngoing(int action_type);
```

- The integer codes for `transition_shape` and `transition_dim` are those of the ABI comments above and are **fixed by this contract**; they are deliberately not the values of the engine's internal `DynamicsShape` / `DynamicsDimension` enums (which put `LINEAR` and `RATE` at 0). Map them explicitly at the ABI boundary.
- `SE_InjectSpeedAction` injects a speed change for `action->id`, transitioning to `action->speed` according to the given transition shape/dimension/value. The action reports as ongoing immediately after the call; the speed change itself is applied by the engine's subsequent `SE_StepDT` calls. Injecting a new speed action while one is already in progress for the same object supersedes it: the object must settle on the newest injected target, never an earlier one.
- `SE_InjectLaneChangeAction` injects a lane change for `action->id`. `mode` selects how `target` is read: `0` means `target` is an absolute OpenDRIVE lane id; `1` means it is a lane count relative to the lane the object is currently in, counted positive towards the left of the object's direction of travel. Lane id `0` is the reference lane and is never a destination — a relative move that would land on it carries on to the next lane beyond, so an object travelling forward in lane `-1` given `mode = 1, target = 1` ends in lane `1`. The transition fields have the same meaning and the same code domains as the speed action's.
- `SE_InjectLaneOffsetAction` injects a lateral offset for `action->id`: while the action runs the object moves to `action->offset` metres from the centre of its current lane, signed the same way as `laneOffset` is reported, under a lateral acceleration no greater than `action->maxLateralAcc` (m/s^2). This struct carries only a `transition_shape` — it has no `transition_dim` and no `transition_value`.
- **The lane offset is a transient manoeuvre, not a lasting state.** While the action runs, the object's reported `laneOffset` is off the lane centre; once the action has completed, the object is back on its lane's reference line and `laneOffset` reads `0` again — it is not left parked at `action->offset`. A lane change injected after it therefore carries the object from the lane centre, not from the old offset.
- All three injectors return `void`. The caller observes the effect through the object's reported state (`speed`, `laneId`, `laneOffset`) and through `SE_InjectedActionOngoing`.
- `SE_InjectedActionOngoing` reports whether an injected action is still in progress. `action_type` selects which kind of injected action to check, and it answers **per type**: a query for a type that is not currently running must report false even while another type is. The codes are the engine's own private-action indices and are fixed by this contract: `-1` = any type, `2` = speed action (a `LongSpeedAction`), `5` = lane change (a `LatLaneChangeAction`), `6` = lane offset (a `LatLaneOffsetAction`).

## R4. Extended object queries

The scenario engine must expose wheel count, longitudinal acceleration, and enumeration of scenario-declared variables, each returned directly rather than through an out-parameter.

Add these entry points to **esminiLib.hpp**:

```cpp
extern "C" int SE_GetObjectNumberOfWheels(int object_id);
extern "C" float SE_GetObjectAcceleration(int object_id);
extern "C" int SE_GetNumberOfVariables();
extern "C" const char* SE_GetVariableName(int index, int* type);
```

- `SE_GetObjectNumberOfWheels` returns the number of wheels for the object (4 for a car), or a negative value if the object id does not exist.
- `SE_GetObjectAcceleration` returns the object's current longitudinal acceleration (m/s², negative while decelerating), or NaN if it cannot be determined.
- `SE_GetNumberOfVariables` returns the count of variables declared in the currently loaded scenario, and a negative value when no scenario is loaded — before the first `SE_Init`, or after `SE_Close`. What counts as a declared variable is the scenario's own `<VariableDeclarations>` block, one entry per `<VariableDeclaration>` element, whose type attribute is spelled **`variableType`** — `variableType="integer"`, `"double"`, `"string"` or `"boolean"` — with the declared value in `value`. Scenario files whose `VariableDeclarations` use that spelling must load: a reader that only recognises some other name for the attribute has not read the block at all.
- `SE_GetVariableName` returns the name of the variable at the given zero-based index and writes its type code to `*type`. Its index domain is `[0, SE_GetNumberOfVariables())`, and it returns `nullptr` when no scenario is loaded. Behaviour for an index outside that domain is **not specified by this contract and is not exercised by any check**: the release this step targets treats an out-of-range index as a fatal argument error rather than returning a value, so a checker must stay inside the domain and an implementation is free to do either. The type code uses the same values the existing `SE_GetParameterName` already writes, i.e. the workspace's `OSCParameterDeclarations::ParameterType` enum: `1` = integer, `2` = double, `3` = string, `4` = boolean. The enumeration order is the engine's own internal order and is not guaranteed to match declaration order in the source file; a checker may compare the set of names, never their positions.

## R5. Optional-argument CLI syntax and CSV recording spelling

The **esmini** binary must accept `--csv_logger` and `--record` with an OPTIONAL filename argument, where previously the argument was mandatory:

- `--csv_logger [<filename>]` — enable CSV recording. If `<filename>` is omitted, write to the default `log.csv`.
- `--record [<filename>]` — enable `.dat` recording. If `<filename>` is omitted, write to the default `sim.dat`.

Both the bare form (`--csv_logger`, `--record`) and the explicit form (`--csv_logger mylog.csv`, `--record mydata.dat`) must parse and run successfully; a build that still requires the argument fails to parse the bare form. When a filename is given explicitly, only that file is written: the default `log.csv` / `sim.dat` is not created.

The CSV recording header must spell the per-entity column prefix "Entity" correctly (it was misspelled "Entitity" at earlier releases).

## R6. Logging system

The scenario engine must use the `spdlog` library as its logging backend, printing lines in the form `[<sim time>] [<level>] <message>` (with a `[<file>::<function>::<line>]` tag inserted before the message when metadata is enabled). `<sim time>` is the simulation time in seconds with three decimals (empty brackets `[]` before the scenario has started), `<level>` is one of `debug`, `info`, `warn`, `error`, and `<file>` is the source file's basename including extension (for example `[playerbase.cpp::Init::1427]`). The logger must respect these runtime options (see R2):

- `log_level` (string-valued option; one of `debug`, `info`, `warn`, `error`) — minimum level printed; a value outside that set is treated as `info`. Default `info`. Two consequences a checker is entitled to measure, because "prints lines in the documented form" alone is satisfied by a logger that prints almost nothing. First, an ordinary headless scenario run at the default level emits a **stream** of info-tagged lines, not a token one or two: at least five. Second, *treated as `info`* means indistinguishable from `info` rather than merely "some info lines survive" — a run given an unrecognised value must emit **no fewer** info-tagged lines than the same run at the default level, so a build that quietly degraded an unknown value to `error`, or to `warn`, is not conforming even though a handful of info lines happened to remain.
- `log_meta_data` (presence-flag option) — if set, insert the `[<file>::<function>::<line>]` tag into every line. Default: not set (metadata off).
- `log_append` (presence-flag option, used together with `--logfile_path`) — if set, append to the existing log file across runs instead of truncating it.
- `log_only_modules` / `log_skip_modules` (string-valued options) — comma-separated lists of module names to exclusively include or to suppress from log output. A **module name** is the stem of the source file that emits the line, i.e. its basename without extension: the line tagged `[playerbase.cpp::Init::1427]` belongs to module `playerbase`, a line from `ScenarioEngine.cpp` to module `ScenarioEngine`. Matching is exact and case-sensitive. When `log_only_modules` is set, only lines from the listed modules are printed and `log_skip_modules` is ignored; otherwise lines from the modules in `log_skip_modules` are suppressed and everything else is printed. A checker may rely on the modules `playerbase` (player option handling and scenario start-up) and `ScenarioEngine` (scenario loading) both emitting info-level lines during an ordinary headless run; keep those two source files under their existing names. Those two are not the only modules that log — an ordinary headless run emits info lines from other source files as well, so `log_only_modules playerbase,ScenarioEngine` prints strictly fewer lines than the unfiltered run. Keeping the inherited call sites is what preserves that; do not funnel every entry through one file.

Add this entry point to **esminiLib.hpp**:

```cpp
extern "C" void SE_CloseLogFile();
```

`SE_CloseLogFile` releases the log file the logger currently holds open: it flushes and drops the file sink and forgets the current log file name, leaving console logging untouched. It takes no arguments, returns nothing, is safe to call when no log file is open, and must be exported from `libesminiLib.so` under exactly that name. It is what makes the truncation rule above observable across runs — with the handle released at the end of a run, the next run that opens the same `--logfile_path` without `--log_append` starts the file from empty instead of growing a handle that was never closed.

Each of these options is settable both through the runtime option store of R2 and as a command-line flag on `esmini` spelled `--<option_name>`; the string-valued ones take their value as the following argument. The six flags are `--log_level <level>`, `--log_meta_data`, `--log_append`, `--log_only_modules <names>`, `--log_skip_modules <names>` and `--logfile_path <path>` (`--logfile_path` already exists and keeps its meaning — a run that names no log file still writes `log.txt` in the current working directory — and `--disable_stdout`, which suppresses the stdout copy of the log, exists and keeps its meaning too; the other five are new).

## R7. Removed entry points and their position-mode replacement

The road manager entry point `RM_SetAlignModeZ` (and its siblings `RM_SetAlignMode`, `RM_SetAlignModeH`, `RM_SetAlignModeP`, `RM_SetAlignModeR`) has been removed from **esminiRMLib.hpp** and from the exported symbols of `libesminiRMLib.so`. The mirrored scenario-engine family `SE_SetAlignMode`, `SE_SetAlignModeH`, `SE_SetAlignModeP`, `SE_SetAlignModeR`, `SE_SetAlignModeZ` is likewise removed from **esminiLib.hpp** and from the exported symbols of `libesminiLib.so`. Code that calls any of them must fail to link.

Their replacement is a position-mode bitmask family that does **not** exist in the workspace you receive; you must add it. Add to **esminiRMLib.hpp**:

```cpp
typedef uint32_t id_t;   // new: unsigned road/junction id type, also used by R1
                         // and by RM_PositionData.roadId and .junctionId

typedef enum
{
    RM_Z_SET = 1,  RM_Z_DEF = 1,  RM_Z_ABS = 3,  RM_Z_REL = 7,
    RM_H_SET = RM_Z_SET << 4,  RM_H_DEF = RM_Z_DEF << 4,  RM_H_ABS = RM_Z_ABS << 4,  RM_H_REL = RM_Z_REL << 4,
    RM_P_SET = RM_Z_SET << 8,  RM_P_DEF = RM_Z_DEF << 8,  RM_P_ABS = RM_Z_ABS << 8,  RM_P_REL = RM_Z_REL << 8,
    RM_R_SET = RM_Z_SET << 12, RM_R_DEF = RM_Z_DEF << 12, RM_R_ABS = RM_Z_ABS << 12, RM_R_REL = RM_Z_REL << 12,
} RM_PositionMode;

extern "C" void RM_SetObjectPositionMode(int handle, int type, int mode);
extern "C" void RM_SetObjectPositionModeDefault(int handle, int type);
extern "C" int  RM_SetWorldPositionMode(int handle, float x, float y, float z, float h, float p, float r, int mode);
```

- `mode` is a bitmask over `RM_PositionMode`: one 4-bit group each for z, heading, pitch and roll, where `ABS` means the given component is an absolute value and `REL` means it is relative to the road surface (for z) or road direction (for h/p/r). For example `RM_Z_ABS` sets z = the value passed; `RM_Z_REL` sets z = road-surface elevation at (x, y) + the value passed.
- `type` selects which operations a stored mode applies to: `1` = SET (explicit set-functions such as `RM_SetWorldPositionMode`), `2` = UPDATE (positions updated implicitly by controllers).
- `RM_SetObjectPositionMode` stores the mode for the position object; `RM_SetObjectPositionModeDefault` restores the engine's own default mode for the given `type`. Both return nothing and must leave the position object usable afterwards.
- `RM_SetWorldPositionMode` sets x/y/z/h/p/r with the interpretation given by `mode`, and follows the return convention of the other position setters (non-negative on success, negative on failure). The plain `RM_SetWorldPosition` keeps its existing absolute-only behaviour.
- `SE_SetObjectPositionMode` and `SE_SetObjectPositionModeDefault` are the `esminiLib` mirror of the previous two bullets, and the mode surface they use is declared in **esminiLib.hpp** in its own right rather than by reference to the RM one. The numbering matches `RM_PositionMode` nibble for nibble, but the member *names* do not: this family spells its default `DEFAULT` where the RM family spells it `DEF`, and the operation selector is a named enum rather than a plain `int`. Add to **esminiLib.hpp**:

```cpp
typedef enum
{
    SE_Z_SET = 1,  SE_Z_DEFAULT = 1,  SE_Z_ABS = 3,  SE_Z_REL = 7,
    SE_H_SET = SE_Z_SET << 4,  SE_H_DEFAULT = SE_Z_DEFAULT << 4,  SE_H_ABS = SE_Z_ABS << 4,  SE_H_REL = SE_Z_REL << 4,
    SE_P_SET = SE_Z_SET << 8,  SE_P_DEFAULT = SE_Z_DEFAULT << 8,  SE_P_ABS = SE_Z_ABS << 8,  SE_P_REL = SE_Z_REL << 8,
    SE_R_SET = SE_Z_SET << 12, SE_R_DEFAULT = SE_Z_DEFAULT << 12, SE_R_ABS = SE_Z_ABS << 12, SE_R_REL = SE_Z_REL << 12,
} SE_PositionMode;

typedef enum
{
    SE_SET    = 1,   // explicit set-functions
    SE_UPDATE = 2    // positions updated implicitly by controllers
} SE_PositionModeType;

extern "C" void SE_SetObjectPositionMode(int object_id, SE_PositionModeType type, int mode);
extern "C" void SE_SetObjectPositionModeDefault(int object_id, SE_PositionModeType type);
```

  `type` selects the same two operation classes the RM `type` argument selects, and `mode` carries the same meaning per nibble. An object id that does not exist is ignored: the call returns nothing and leaves the engine running. There is no `SE_SetWorldPositionMode` in the release you are moving to — the world-position-with-mode setter exists only on the RM side.
- Three further `esminiLib` entry points are part of this release's route and pose surface and must be added and exported alongside the position-mode family. This step states what each one is for, but requires only that it is present on the exported surface — the reachable behaviour of each needs a route-assigned or externally-reported scenario this step's fixtures do not build.
  - `float SE_GetRouteTotalLength(int object_id)` — the total length in metres of the route currently assigned to the object, i.e. the summed distance between consecutive waypoints of that route; `0.0` when no route is assigned.
  - `int SE_GetObjectRouteStatus(int object_id)` — the object's progress along its assigned route: `0` when no route is assigned, `1` when the object is off the assigned route, `2` when it is on it, and a negative value on error.
  - `int SE_ReportObjectPosMode(int object_id, float timestamp, float x, float y, float z, float h, float p, float r, int mode)` — report an object's pose externally, with `mode` the same bitmask the RM bullets above define, so a `z` reported with the relative-Z bit set is measured from the road surface and one reported without it is the literal value given. It returns `0` on success and a negative value if the object id is unknown.
- `id_t` is a new **unsigned** 32-bit typedef — `typedef uint32_t id_t;` — and it is declared in **both** public headers, `esminiLib.hpp` as well as `esminiRMLib.hpp`. Each header also defines the all-ones "undefined id" sentinel as a preprocessor macro, under the name its own prefix dictates:

```cpp
typedef uint32_t id_t;                 // in esminiLib.hpp and in esminiRMLib.hpp
#define SE_ID_UNDEFINED 0xffffffff     // in esminiLib.hpp
#define RM_ID_UNDEFINED 0xffffffff     // in esminiRMLib.hpp
```

  A checker compiles against these names, so both spellings must exist and both must expand to the all-ones 32-bit value. Because `id_t` is unsigned, an undefined id is `0xffffffff` and never `-1`; code that still tests road or junction ids for `< 0` is wrong at this release.
- `id_t` replaces the `int` road and junction id fields wherever an id is returned or reported. `RM_GetRoadIdFromString` / `RM_GetJunctionIdFromString` (R1) return it; **both** the `roadId` and the `junctionId` fields of `RM_PositionData` become `id_t`, with `RM_ID_UNDEFINED` as the "no road" / "not in a junction" sentinel for each; and on the `esminiLib` side the `roadId` and `junctionId` members of both **`SE_ScenarioObjectState`** (filled by `SE_GetObjectState`) and **`SE_RoadInfo`** (filled by `SE_GetRoadInfoAtDistance`) become `id_t` too. The `laneId` member of each stays a signed `int` — lane ids are negative on one side of the reference line. A road or junction that is not defined for a reported position is `SE_ID_UNDEFINED`: in particular `SE_GetRoadInfoAtDistance` must fill `SE_RoadInfo.junctionId` with `SE_ID_UNDEFINED` whenever the lookahead target is not inside a junction, rather than with `-1` or with `0` (`0` is a legitimate junction id).

## What must keep working

- **Catalog-free scenario execution**: the engine must load and run OpenSCENARIO 1.0 `.xosc` files that define vehicles inline rather than referencing external catalogs, stepping deterministically via `SE_StepDT`. `esmini-dyn` announces both files it opened on start-up, and R6's replacement of the logging backend does not change what those two announcements say: one of them reads `Loading <scenario>.xosc (vX.Y)`, naming the scenario file together with its declared OpenSCENARIO version in parentheses, and another ends with the name of the OpenDRIVE file it loaded — `Loaded OpenDRIVE: <road>.xodr`, the file name last on the line. Only that text is required. R6 is free to prefix either line with a timestamp, a level tag or a module name, and does; what it must not do is drop the file name, drop the parenthesised version, or move the road file name away from the end of its line.
- **Position queries**: the entry points `RM_GetPositionData`, `RM_SetWorldPosition`, `RM_SetLanePosition`, and their data structures must remain stable. Position setters may return any non-negative value on success (not only 0); a negative return means failure.
- **CSV recording**: `esmini --csv_logger <filename> ...` must produce a CSV file with one row per simulation step, reporting entity state.
- **The options every check runs with.** Each scenario the checks drive is started `--osc <filename> --headless --fixed_timestep <seconds>`, usually with `--disable_log` as well. `--osc` names the OpenSCENARIO file to run and is required; `--headless` runs with no viewer; `--fixed_timestep` decouples the run from wall-clock time and advances the simulation by exactly the given step, which is what makes a recorded run reproducible; `--disable_log` suppresses the text log file. All three already exist in the tree you receive and this step does not change any of them - but this step rewrites the option-handling layer (R2, R5), so they are named here rather than left to be inherited silently. An option-registry rewrite that drops `--fixed_timestep`, or that stops it taking its value as a separate argument, fails checks that never mention it.
- **The `--csv_logger` file's preamble.** The CSV opens with six lines before the header row, in this order: `esmini GIT REV:`, `esmini GIT TAG:`, `esmini GIT BRANCH:`, `esmini BUILD VERSION:`, `Scenario File Name:` and `Number of Vehicles:`. The first four are the same four lines the tools print on stdout at start-up, in the same order and from the same accessors. They do not all come from the same place: `GIT REV`, `GIT TAG` and `GIT BRANCH` carry git-derived values and read `N/A` when the tree is not a checkout, while `BUILD VERSION` carries a build-time string that a CI build sets and that otherwise keeps its default, the literal `N/A - client build`. The checks require the lines and their spelling, never a particular value, so every one of those answers is accepted.
- **The inherited entry points the checks call.** Everything in this step is exercised through the C ABI your workspace already publishes, and those entry points keep their existing names, signatures and meanings. On the scenario-engine side: the lifecycle calls `SE_Init`, `SE_Step`, `SE_StepDT`, `SE_Close` and `SE_GetSimulationTime`; object access `SE_GetNumberOfObjects` and `SE_GetObjectState` with its `SE_ScenarioObjectState` struct; and the lookahead query `SE_GetRoadInfoAtDistance` with its `SE_RoadInfo` struct, which returns a non-negative value when it answered (`0` for an ordinary answer, a small positive code when the lookahead crossed onto a new road or through a junction) and a negative value when it could not answer. On the road-manager side: `RM_Init`, `RM_Close`, `RM_CreatePosition` and `RM_DeletePosition` alongside the position calls named above. The change this release makes to them is the `id_t` retyping of the road and junction id fields described in R7; apart from that no member of `SE_ScenarioObjectState`, `SE_RoadInfo` or `RM_PositionData` is renamed, reordered, dropped or given a new meaning, so an existing caller keeps compiling and keeps reading the same values out of them.

## Scope

All work is offline and headless. The `USE_OSG`, `USE_OSI`, `USE_SUMO`, `USE_GTEST`, `USE_IMPLOT`, and `USE_PROJ` flags are all `False`. Code behind those switches is not compiled and not checked.

R1-R7 above, together with "What must keep working" and the entry points, CLI changes and file-format changes listed in `WORKSPACE_HANDOFF.md`, are the complete set of changes this step asks for. Other differences between the release you start from and the release you are moving to are not part of this step's contract and are not checked.

**One behaviour in this range is not optional**, because a later step re-reads
it and pins the answer this range produces: what an entity does at the
end of its road. From this release on, an entity whose longitudinal motion runs
off the end of the road it is on, with no successor to continue onto, has its
**speed zeroed** — the position stays clamped at the end of the road and every
reader reports `0` for it, `SE_GetObjectState`, the `--record` `.dat` and the
`--csv_logger` table alike. The release you start from keeps reporting the
entity's commanded speed there instead. On the checker's `cruise_straight.xosc`
— one entity cruising at 20 m/s along the 500 m straight road — the entity
reaches the end of the road at 24.0 s, and from 24.1 s on every frame, the
recording's last one included, reports speed `0`, where the older answer was
`20` throughout. Adopt this; a tree that keeps the old answer fails the later
step that re-reads that recording. Everything the workspace already does must keep working unchanged, including the six delivered artifacts (`esmini`, `esmini-dyn`, `odrplot`, `dat2csv`, `libesminiLib.so`, `libesminiRMLib.so`) and the output formats of `odrplot` and `dat2csv` — `dat2csv` run with no arguments still prints its `Usage:` line and still exits **non-zero**, and its CSV keeps the `time, id, name, ...` header and is written to the current working directory under the `.dat` file's basename.

This environment has no network access, and everything the build needs is
already installed. The upstream project's source, its releases, mirrors and
package registries are unreachable, so a copy cannot be fetched by any
route.
