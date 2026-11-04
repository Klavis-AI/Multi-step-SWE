# Step 3 — OpenSCENARIO variables namespace

The workspace holds a working ASAM OpenSCENARIO / OpenDRIVE scenario engine that builds and runs. Your job in this step is to grow its published scenario-engine C ABI so that OpenSCENARIO *variables* become a readable and writable namespace of their own, distinct from the parameter namespace the earlier step already exposed, and to make the size of a parameter value distribution readable from the player's exit status without executing any permutation.

Read `TEST_INTERFACE.md` first: it states how the checker builds your tree, which six artifacts it looks for, and how it compiles small programs against your headers. Nothing in this instruction overrides it.

## 1. Typed variable accessors — read and write

`esminiLib` must expose four pairs of entry points for reading and writing OpenSCENARIO variables:

```c
int SE_GetVariableInt(const char *variableName, int *value);
int SE_GetVariableDouble(const char *variableName, double *value);
int SE_GetVariableString(const char *variableName, const char **value);
int SE_GetVariableBool(const char *variableName, bool *value);

int SE_SetVariableInt(const char *variableName, int value);
int SE_SetVariableDouble(const char *variableName, double value);
int SE_SetVariableString(const char *variableName, const char *value);
int SE_SetVariableBool(const char *variableName, bool value);
```

- All eight return 0 on success and -1 on failure.
- A scenario that declares `<VariableDeclaration name="CruiseCount" variableType="integer" value="7"/>` must be readable as `SE_GetVariableInt("CruiseCount", &v)` returning 0 and writing 7 into `v`.
- The same entry point called before `SE_Init` returns -1 — the setters as much as the getters. Nothing is declared before a scenario has been loaded, so a write issued before `SE_Init` names an undeclared variable and fails with the rest of them.
- A declared variable asked through the getter for the wrong type returns -1: `SE_GetVariableDouble("CruiseCount", &d)` fails when `CruiseCount` is an integer variable.
- A name no `<VariableDeclarations>` block mentions returns -1 from both the getters and the setters.
- The setters replace the value. After `SE_SetVariableInt("CruiseCount", 42)` returns 0, `SE_GetVariableInt("CruiseCount", &v)` must report 42, not 7.
- A write touches only the variable it names. Every other declared variable still reports the value its declaration — or its own last write — gave it, unchanged.
- Writing a variable does not change its declared type. After the write above, `SE_GetVariableDouble("CruiseCount", &d)` still returns -1.
- Writing an undeclared name fails and creates no variable.
- What a *setter* does when the value it is handed is not of the variable's declared type — `SE_SetVariableDouble("CruiseCount", 1.5)` on an integer variable — is not specified here and is out of scope: refusing with -1 and coercing are both acceptable, and no check calls a mismatched setter.

Initialisation answers on the same convention. `SE_Init` returns 0 only when it has opened the scenario file it was handed; a path it cannot open is reported as **-1** rather than accepted, and the variable namespace of the scenario that did not load is not readable.

## 2. Struct-based variable accessors

```c
typedef struct {
    const char *name;
    void *value;
} SE_Variable;

int SE_GetVariable(SE_Variable *variable);
int SE_SetVariable(SE_Variable variable);
```

Both return 0 on success and -1 on failure. The struct forms read and write the same variable store the typed calls do: a value written with `SE_SetVariableInt("CruiseCount", 101)` must be readable through `SE_GetVariable` by filling `variable->name = "CruiseCount"; variable->value = &iv;` and inspecting `iv` after the call returns. The inverse must also hold.

`value` points at storage of exactly the type the matching typed accessor takes — `int *` for an integer variable, `double *` for a double, `bool *` for a boolean — and the call reads or writes through it at exactly that width. Storing a boolean as an `int` and writing four bytes through a caller's `bool *` corrupts the caller's stack and is a defect, not a representation choice.

The checker exercises only the integer, double and boolean variants through the struct forms. For a string variable upstream reinterprets `value` as a pointer to a C++ `std::string` rather than to a C string; that is not a stable C ABI and this step does not require it.

## 3. Variables are not parameters

The variable namespace and the parameter namespace are distinct. A scenario that declares two parameters and seven variables reports 2 from `SE_GetNumberOfParameters()`, and `SE_GetParameterName(0, &type)` through `SE_GetParameterName(1, &type)` enumerate only the two parameter names, never the seven variable names.

`SE_GetParameterDouble("HeadwayTarget", &d)` returns -1 when `HeadwayTarget` is a variable, and `SE_GetVariableDouble("EgoStartSpeed", &d)` returns -1 when `EgoStartSpeed` is a parameter. The setters fail the same way and for the same reason: `SE_SetParameterDouble("HeadwayTarget", 11.0)` returns -1 and creates no parameter, and `SE_SetVariableDouble("EgoStartSpeed", 11.0)` returns -1 and creates no variable. Each namespace answers only for its own names.

Writing a variable through `SE_SetVariableDouble` must not graft it onto the parameter namespace: after such a write, `SE_GetParameterDouble` on that name still returns -1, and `SE_GetNumberOfParameters()` still reports the same count it did before the write.

## 4. Storyboard-driven variable changes

A variable the storyboard assigns while the scenario runs is visible through the variable readers. A scenario whose `<VariableDeclarations>` block declares `<VariableDeclaration name="PhaseTag" variableType="string" value="phase_initial"/>` and whose storyboard later fires a global `<VariableAction variableRef="PhaseTag"><SetAction value="phase_running"/></VariableAction>` must be readable as `SE_GetVariableString("PhaseTag", &s)` returning the string `"phase_initial"` before the action's start trigger and `"phase_running"` after the action has run.

## 5. Parameter distribution size as exit status

The player gains one new command-line option:

```
--return_nr_permutations
    Return number of permutations without executing the scenario (-1 = error)
```

That block is the option's own help text and it is printed in that shape: the
option's name **alone on its line**, and its description — beginning
`Return number of permutations without executing the scenario` — on the line
below it. The indentation is not part of the contract; the line break is.

When this option is given alongside `--param_dist <filename>`, the player reports the size of the parameter value distribution through its exit status and skips scenario execution entirely. A distribution file that declares a three-value deterministic set crossed with a two-value set — six permutations — makes the process exit with status 6. No permutation is ever executed, so `--record` and `--csv_logger` must produce no files even when they are also on the command line.

When `--return_nr_permutations` is given with no `--param_dist` at all, the player exits with status 0 because there is nothing to permute. That invocation skips execution too, and for the same reason: the option reports a count instead of running, so with nothing to permute there is nothing to run either, and `--record` and `--csv_logger` produce no files on that path either. Exiting 0 because the scenario played to its stop trigger is the wrong 0 here.

Errors travel on the same channel. The option's own help text says `-1 = error`, and because a process exit status is a single unsigned byte, a returned -1 reaches the shell as **255**. A `--param_dist` file that cannot be found, or that cannot be parsed as a parameter value distribution, therefore makes the player exit 255 instead of reporting a count.

## 6. C linkage for the variable entry points

The ten variable entry points — the four typed getters, the four typed setters, and the two struct forms — must be exported from `libesminiLib.so` under their own unmangled names. The symbol table must show `SE_GetVariableInt`, not `_Z18SE_GetVariableIntPKcPi`. That is what `extern "C"` around the declarations buys you, and it is what makes the entry points callable from C as well as from C++.

## 7. What must keep working

Everything the engine already did. The checker re-runs, at this step, capabilities the two earlier steps established:

- **Parameters**: `SE_SetParameterDistribution`, `SE_GetNumberOfPermutations`, `SE_SelectPermutation`, `SE_GetPermutationIndex`, `SE_ResetParameterDistribution`, `SE_GetNumberOfParameters`, `SE_GetParameterName`, and `SE_GetIdByName`. The parameter *accessors* are ten entry points and all ten stay exported under their own unmangled names, exactly as §6 requires of the ten variable ones: `SE_GetParameterInt`, `SE_GetParameterDouble`, `SE_GetParameterString`, `SE_GetParameterBool`, `SE_SetParameterInt`, `SE_SetParameterDouble`, `SE_SetParameterString`, `SE_SetParameterBool`, and the two struct forms `SE_GetParameter` and `SE_SetParameter`. These were step 02. The distribution **documents** they read are inherited with them, and the checker's files are all in the same dialect: an OpenSCENARIO document whose `ParameterValueDistribution` names the scenario it distributes over with a `<ScenarioFile filepath="…"/>` element and holds its distributions under a `Deterministic` block, each single-parameter one naming the parameter it varies with the **`parameterName` attribute** of `DeterministicSingleParameterDistribution` and listing its values as `Element value="…"` children of a `DistributionSet` — or, for a range, as a `Range lowerLimit=… upperLimit=…` inside a `DistributionRange stepWidth=…`. A document that declares nothing is one whose `ParameterValueDistribution` carries no `Deterministic` block; the `ScenarioFile` element is the one part of the shape every one of these documents carries, and it is what names the scenario the selected permutation is built from. A reader that accepts only an invented spelling for any of those refuses a file the checker expects it to load. The **enumeration order** those permutation calls established is part of what must keep working, and it is read back here rather than merely linked against: permutation indices are **0-based**, the values of one parameter are taken in **declaration order**, and the **last declared parameter varies fastest**. So for a distribution declaring `EgoSpeed ∈ {12, 24}` before `TargetSpeed ∈ {5, 10, 15}`, index 0 is `(12, 5)`, index 2 is `(12, 15)` and index 5 is `(24, 15)`. `SE_SelectPermutation(i)` pins the combination the next `SE_Init` resolves — the scenario really is built with those values, so an entity whose speed comes from a distributed parameter drives at the value the selected index resolves to — and `SE_GetPermutationIndex` reports `i` back. An implementation that varies the *first* declared parameter fastest produces the same set of combinations under the wrong indices and is a regression. `SE_GetIdByName` also keeps its current id assignment: the first entity a scenario declares is id 0, and an unknown name answers negative. The zero-state of those calls is part of what is read back too: with no distribution set `SE_GetNumberOfPermutations` reports 0 — both before any `SE_SetParameterDistribution` and again after `SE_ResetParameterDistribution` — and `SE_SetParameterDistribution` itself returns 0 on success, like every other entry point named here.
- **Road-manager lane queries**: `RM_GetLaneWidth`, `RM_GetLaneWidthByRoadId`, `RM_GetLaneType`, `RM_GetLaneTypeByRoadId`. These were step 01.
- **CLI and recording**: running a scenario headless to its stop trigger with `--osc`, `--headless`, `--fixed_timestep <seconds>`, `--logfile_path`, `--csv_logger` and `--record`; the `.dat` recording format and the `dat2csv` column header. `--fixed_timestep` still decouples the run from realtime and advances the simulation by the number of seconds it names, which is what makes every run here repeatable. Five details of that inherited output are read back verbatim and must not be tidied up on the way through:
    - the `--csv_logger` preamble and column header are emitted byte for byte as the engine already emits them, **including the existing `Entitity_Name` / `Entitity_ID` misspelling and the space before every comma — do not "fix" either**;
    - `--logfile_path <path>` still redirects the run log to the file it names, and an invocation that passes no such option still writes `log.txt` into the working directory, with its entries still in the existing `%.3f: %s` form;
    - `esmini --help` and `esmini-dyn --help` each keep their current usage line — `Usage: esmini [options]` and `Usage: esmini-dyn [options]` — and their current option list, `--osc <filename>` among its entries;
    - the player's own run output on stdout is part of what must not change, and three of its lines are read back verbatim from `esmini` and from `esmini-dyn` alike: the `esmini BUILD VERSION` banner each of them prints at startup, the `Loading <file>` line naming the scenario file it opened, and the `Closing` line — alone on a line of its own — at shutdown.
    - `dat2csv` still decodes a `.dat` recording into the two header lines it already writes: a `Version: 2, OpenDRIVE: <network file>, 3DModel: ` line, which is where the recording's format version `2` is read back, followed by the twelve-column header `time, id, name, x, y, z, h, p, r, speed, wheel_angle, wheel_rot`. Both are compared literally, so a renamed, reordered or added column is a regression. Where the decoded file lands is unchanged too: it is written into the **current working directory**, named from the **basename** of the recording it read — not beside the recording, and not including its directory.
- **The artifacts and side tools**: the six published artifacts (`esmini`, `esmini-dyn`, `odrplot`, `dat2csv`, `libesminiLib.so`, `libesminiRMLib.so`), and the outputs `odrplot` and `dat2csv` produce. `odrplot` keeps writing `track.csv` into the current working directory and keeps printing its current completion line, `Created track.csv using stepsize 1.00`, naming the 1.00 m default sampling step it still uses. Its sampling is unchanged with it: each lane is sampled every step along the road **and** a final sample is always emitted at the very end of the road, so a 500 m road yields 501 sample rows per lane rather than 500; `esmini-dyn` keeps driving the library frame by frame and keeps emitting its per-frame `Detections [sensor ID ...]` lines while it plays a scenario.

Nothing is removed from the published ABI at this step, and that is read back from the libraries' symbol tables rather than inferred from a driver linking successfully: every entry point this section names stays exported under its own unmangled name, and so do the lifecycle and stepping calls the earlier steps' own examples are built on — `SE_Init`, `SE_InitWithArgs`, `SE_Step`, `SE_StepDT`, `SE_GetSimulationTimeDouble`, `SE_GetObjectState` and `SE_Close` in `libesminiLib.so`, and `RM_Init`, `RM_Close`, `RM_CreatePosition`, `RM_SetLanePosition`, `RM_GetPositionData`, `RM_GetNumberOfRoads` and `RM_GetRoadNumberOfLanes` beside the four lane queries in `libesminiRMLib.so`.

A missing or unparseable input file is still an error rather than a silent success: `esmini` given a `--osc` scenario file that does not exist exits non-zero, and `odrplot` given a file that is not an OpenDRIVE network exits non-zero.

One subtlety worth noting: `--return_nr_permutations` makes a *successful* esmini run exit with a **non-zero** status by design — the exit code is the data channel. A regression harness that blanket-asserts `exit == 0` for every esmini invocation will misread this option from this step onward.

## 8. Scenario initialisation from an argument vector

`SE_InitWithArgs(int argc, const char *argv[])` already exists and already starts a scenario from a command-line-shaped argument vector. Command-line-shaped means exactly what it does for `main`: `argv[0]` is the program name and is skipped, and the options begin at `argv[1]`. At this step it must treat each element of `argv` as **one** argument exactly as handed over, instead of re-splitting that element on spaces. So a scenario whose path contains repeated or trailing spaces — `--osc` followed by, as a single `argv` element, a path such as `/tmp/two  spaces/demo.xosc` — must tokenise correctly and initialise, returning 0. Splitting it into fragments and rejecting the result with -1 is the behaviour this step replaces.

## Scope

Do not switch on OpenSceneGraph, OSI, SUMO, PROJ, ImPlot or the unit-test targets, and do not add anything that downloads at configure or build time. The checker builds offline with all of those switched off; see `PROFILE.md`.

Three behaviour changes that ship in the same upstream release are deliberately **out of scope at this step** and no requirement covers them: the speed adjustment along `s` that keeps total speed from rising during a lateral action, the continuous polyline speed profile with its `FollowMode` distinction, and the orientation-alignment correction to the reported heading columns. None of them is specified anywhere in this step's documents, so none of them is in scope here. What §7 does name still holds.

This environment has no network access, and everything the build needs is
already installed. The upstream project's source, its releases, mirrors and
package registries are unreachable, so a copy cannot be fetched by any
route.
