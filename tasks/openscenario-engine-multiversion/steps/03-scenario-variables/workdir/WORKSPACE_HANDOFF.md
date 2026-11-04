# Workspace handoff — step 03

## What you receive

You begin with a working esmini tree at v2.27.0 that already builds the six published artifacts, runs OpenSCENARIO player scenarios headless, records playback to `.dat` files, parses those recordings through `dat2csv`, renders road networks through `odrplot`, and plays back a recording through `esmini-dyn`.

The scenario-engine C ABI at this tag already publishes the parameter distribution entry points from the earlier step: `SE_SetParameterDistribution`, `SE_GetNumberOfPermutations`, `SE_SelectPermutation`, `SE_GetPermutationIndex`, `SE_ResetParameterDistribution`, the typed parameter getters and setters (`SE_GetParameterInt` through `SE_SetParameterString`), and `SE_GetIdByName` for translating an entity name into its numeric id. The road-manager library publishes `RM_GetLaneWidth`, `RM_GetLaneWidthByRoadId`, `RM_GetLaneType`, and `RM_GetLaneTypeByRoadId`.

Your tree builds offline with CMake options that disable OpenSceneGraph, OSI, SUMO, PROJ, ImPlot, and the unit-test targets. Nothing downloads at configure or build time. Read `PROFILE.md` for the exact shape.

The checker runs your tree against a fixture corpus it owns. Those fixtures are **not in your workspace** and are described here only so you know what shapes of input your build has to handle. Three of them are already in use at v2.27.0:

- `two_car_brake.xosc` — a two-entity longitudinal scenario that the earlier step's parameter distribution checker uses; it declares no parameters and no variables, brakes the Ego from 20 m/s to 5 m/s, and stops itself at 10 s. Its two entities are `Ego` and `Target`, declared in that order, so they keep the ids `0` and `1` the earlier step recorded and a decoded recording of it still begins its first sample `0.000, 0, Ego, `. Runs to completion with `--headless`.
- `straight_500m.xodr` — the 500 m straight road network that scenario references. The road-manager lane queries are asked against it too: one road, id `1`, with a driving lane 3.0700 m wide, and `RM_GetRoadNumberOfLanes` still answering `2` for it exactly as the earlier step recorded. Its single `<laneSection>` declares seven lane elements in the `.xodr` text — ids `3` down to `-3`, the centre lane `0` and the border lanes among them, which is a count of the file's lane records and not of what any lane-count query returns — and `odrplot` draws one track per lane element rather than per drivable lane, so the sample rows it writes for this network are seven times the per-lane count `instruction.md` §7 describes. The two counts answer different questions and both are read back here.
- `e6mini.xodr` — the multi-lane motorway road network the road-manager lane-width and lane-type queries are asked against, a single road of id `0` whose one `<laneSection>` declares fifteen lane elements in the `.xodr` text, ids `7` down to `-7`; the seven of differing widths on its right-hand side, ids `-1` through `-7`, are the ones the width and type queries name here; `RM_GetRoadNumberOfLanes` still answers `6` for that road, as the earlier step recorded. Both fixtures' lane widths and lane-type codes are read back as literal values, through the road-id form and the position-handle form alike, so the road manager's lane-type numbering is part of what must not move: a driving lane still reports type `2`, and a road's centre lane still reports width `0` and type `1`.

## What you build

You grow the published C ABI in `esminiLib.hpp` and the implementation behind it so that OpenSCENARIO *variables* become a separate namespace from the parameter namespace already exposed. The variable namespace is readable and writable from outside the engine through ten new entry points: four typed getters (`SE_GetVariableInt`, `SE_GetVariableDouble`, `SE_GetVariableString`, `SE_GetVariableBool`), four typed setters (`SE_SetVariableInt` through `SE_SetVariableBool`), and two struct-based forms (`SE_GetVariable`, `SE_SetVariable` operating on an `SE_Variable` struct). All ten are exported from `libesminiLib.so` with C linkage so they are callable from C as well as from C++.

A variable the storyboard assigns while the scenario runs is visible through these readers: a `<VariableAction>` that fires a `<SetAction>` on a declared variable makes the new value immediately readable through `SE_GetVariable*`.

The variable namespace and the parameter namespace remain distinct: a scenario that declares two parameters and seven variables reports 2 from `SE_GetNumberOfParameters()`, and `SE_GetParameterName` enumerates only the two parameter names, never the seven variable names. Writing a variable through `SE_SetVariableDouble` does not graft it onto the parameter namespace.

You add one new command-line option to the `esmini` player:

```
--return_nr_permutations
    Return number of permutations without executing the scenario (-1 = error)
```

When this option is given alongside `--param_dist <filename>`, the player reports the size of the parameter value distribution through its exit status and skips scenario execution entirely. A distribution file that declares six permutations makes the process exit with status 6. No permutation is ever executed, so `--record` and `--csv_logger` produce no files even when they are also on the command line.

You also change how `SE_InitWithArgs(int argc, const char *argv[])` reads the argument vector it is handed. That entry point already exists and already starts a scenario from a command-line-shaped vector: `argv[0]` is the program name and is skipped, and the options begin at `argv[1]`. At this step each element of `argv` must be taken as one argument exactly as handed over, instead of being re-split on spaces — so a scenario path carrying repeated or trailing spaces, passed as the single element after `--osc`, initialises and returns 0 rather than being broken into fragments and rejected. `instruction.md` §8 is the normative statement of this; the checker exercises it.

## What carries forward

The six published artifacts (`esmini`, `esmini-dyn`, `odrplot`, `dat2csv`, `libesminiLib.so`, `libesminiRMLib.so`) are rebuilt at the new tag, so they are not the same bytes you received, but they keep every path the earlier steps established and answer the same way at each of them. The new material is three things: the variable entry points, the `--return_nr_permutations` option, and the `SE_InitWithArgs` argument convention described above and specified in `instruction.md` §8. Everything else is a capability regression that the checker re-runs at this step to confirm nothing broke.

That carry-forward includes the players' own console output and the command-line options they already accept, both of which the checker reads back. `esmini` and `esmini-dyn` each still print the `esmini BUILD VERSION` banner at startup, a `Loading <file>` line naming the scenario they opened, and a `Closing` line on its own at shutdown; `esmini --help` and `esmini-dyn --help` each still print their usage line and their option list, `--osc <filename>` among the entries. `--fixed_timestep <seconds>` is still accepted by both and still decouples the run from realtime, advancing the simulation by the seconds it names. `instruction.md` §7 is the authoritative list.

Your tree ends at v2.27.5. The build profile is unchanged: still offline, still without OpenSceneGraph/OSI/SUMO/PROJ/ImPlot/unit-tests.

Four new fixtures join that verifier-owned corpus. They are not in your workspace either:

- `variables_demo.xosc` — a two-entity scenario that declares seven variables (two integers, one double, two strings, two booleans) and two parameters (one double, one string), and stops itself at 6 s. The storyboard fires a global `<VariableAction>` at 2 s that replaces the string variable `PhaseTag` from `"phase_initial"` to `"phase_running"`. The other six variables are never assigned after their declarations. The scenario references the existing `straight_500m.xodr`.
- `param_sweep.xosc` — a two-entity scenario that sets each entity's absolute speed from a parameter (`EgoSpeed`, `TargetSpeed`), re-applies `EgoSpeed` once at 1 s, and stops itself at 4 s. It declares three named parameters (`EgoSpeed`, `TargetSpeed` and `EgoStart`), no variables. This is the target scenario the next two distribution fixtures point to.
- `dist_six.xosc` — a distribution file for `param_sweep.xosc` that declares a 3×2 deterministic crossing: `EgoSpeed` takes three values, `TargetSpeed` takes two values, six permutations total.
- `dist_three.xosc` — a distribution file for `param_sweep.xosc` that declares a single three-value deterministic set on `EgoStart` and leaves the other two parameters at their declared defaults: three permutations.

All four run at v2.27.5. `variables_demo.xosc` is the primary fixture for the variable namespace; `param_sweep.xosc` and its two distribution companions exercise the `--return_nr_permutations` option and also serve as regression material for the parameter distribution API the earlier step established.

The earlier steps' fixtures `two_car_brake.xosc`, `straight_500m.xodr` and `e6mini.xodr` are still in the checker's corpus and still answer the same way at v2.27.5.
