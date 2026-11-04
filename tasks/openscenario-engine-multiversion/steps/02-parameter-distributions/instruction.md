# Parameter distributions

The engine in `workdir/` is at v2.23.1. Advance it to the v2.27.0 feature set.
The headline capability is **parameter distributions**: a scenario's parameters
can be varied over a declared set of values, the resulting permutations can be
counted and selected by index, and one permutation can be resolved and run.
Several unrelated library additions land in the same range, and one callback
entry point is retired.

Build configuration does not change. The tree is configured offline and
headless: `USE_OSG`, `USE_OSI`, `USE_SUMO`, `USE_GTEST`, `USE_IMPLOT` and
`USE_PROJ` are all `False`, nothing is downloaded, and the published artifacts
are `esmini`, `esmini-dyn`, `odrplot`, `dat2csv`, `libesminiLib.so` and
`libesminiRMLib.so`. `replayer` and `odrviewer` are not built and must not be:
the checker builds those six targets by name and nothing else, so what this
rules out is making either viewer a *dependency* of one of the six. Their
absence is not itself asserted by any check — with `USE_OSG` off they cannot
build — so nothing is gained by removing their sources.

Read `TEST_INTERFACE.md` in your workspace first. It states how the checker
builds your tree, which six artifacts it looks for under which fixed names,
which header file names it includes, that every entry point it calls must be
exported with **C linkage**, and how the small programs it links against your
shared libraries report back what they observed. It defers the *signatures* to
this file, and every signature this step is about is written out below.
`WORKSPACE_HANDOFF.md` lists what the starting tree already does and the
inherited figures that must not move; `PROFILE.md` describes the container.

## 1. Load and reset a parameter distribution

```c
SE_DLL_API int  SE_SetParameterDistribution(const char* filename);
SE_DLL_API void SE_ResetParameterDistribution();
```

`SE_SetParameterDistribution` reads an OpenSCENARIO parameter-value-distribution
file and makes it the distribution in force for the next `SE_Init`. It returns 0
on success and a **negative** value on refusal. The documents it is handed are
written in the dialect §2 writes out; the three outcomes below are outcomes for
files in it. Three outcomes are distinguished, and all three are in scope:

* A document that declares a usable distribution **loads**: return 0.
* A document that parses but declares **no** parameter distribution at all — a
  `ParameterValueDistribution` carrying only a `ScenarioFile`, say — also
  **loads**: return 0, and it enumerates **zero** permutations (§2). It is not
  an error; there is simply nothing to vary.
* A document that is present but cannot be parsed as XML, or whose distribution
  is impossible — a `Range` whose `lowerLimit` exceeds its `upperLimit`, for
  instance — is **refused**: return a negative value, and leave the permutation
  count at 0. The library must return from this, not abort.

The three outcomes above are about files that are *there*. A path that names no
file at all is a fourth case, and it is outside this contract: upstream at this
release aborts on a missing path, the checker does not pass one, and nothing
about what a missing path should do is in scope.

Which negative value is returned is not specified and is out of scope; only the
sign is required. `SE_ResetParameterDistribution` discards whatever is in force and
returns the library to the state it had before any distribution was set; after
it, the permutation count is zero again.

## 2. Count, select and report permutations

```c
SE_DLL_API int SE_GetNumberOfPermutations();
SE_DLL_API int SE_SelectPermutation(int index);
SE_DLL_API int SE_GetPermutationIndex();
```

With a distribution in force, `SE_GetNumberOfPermutations` returns the number of
distinct parameter combinations it generates: the product of the sizes of the
declared distributions. Those sizes come from three shapes, and all three are
pinned here:

* a `DistributionSet` holding `n` `Element`s has size `n`;
* a `DistributionRange` has the size of the sequence it enumerates — the values
  `lowerLimit + k * stepWidth` for every `k` that keeps the value **at or below**
  `upperLimit`. A `10 .. 30` range with `stepWidth` `5` is therefore **five**
  values (`10 15 20 25 30`), not four: the upper limit is enumerated when the
  sequence lands on it.
* a `DeterministicMultiParameterDistribution` that holds a single
  `ParameterValueSet` is one factor: the assignments in that set vary together,
  so it contributes one combination, not one per assignment.

**The dialect those shapes are written in.** Every distribution file the
checker hands `SE_SetParameterDistribution`, and every one §3's `--param_dist`
is handed, is an OpenSCENARIO document in the form below. The element names,
the nesting and the attribute spellings here are the contract, not an
illustration: a reader that invents its own spelling for any of them refuses a
file the checker expects it to load.

```xml
<OpenSCENARIO>
  <FileHeader revMajor="1" revMinor="2" date="…" description="…" author="…"/>
  <ParameterValueDistribution>
    <ScenarioFile filepath="param_cars.xosc"/>
    <Deterministic>
      <DeterministicSingleParameterDistribution parameterName="EgoSpeed">
        <DistributionSet>
          <Element value="12"/>
          <Element value="24"/>
        </DistributionSet>
      </DeterministicSingleParameterDistribution>
      <DeterministicSingleParameterDistribution parameterName="TargetStartS">
        <DistributionRange stepWidth="5">
          <Range lowerLimit="10" upperLimit="30"/>
        </DistributionRange>
      </DeterministicSingleParameterDistribution>
      <DeterministicMultiParameterDistribution>
        <ValueSetDistribution>
          <ParameterValueSet>
            <ParameterAssignment parameterRef="EgoSpeed" value="18"/>
            <ParameterAssignment parameterRef="TargetSpeed" value="6"/>
          </ParameterValueSet>
        </ValueSetDistribution>
      </DeterministicMultiParameterDistribution>
    </Deterministic>
  </ParameterValueDistribution>
</OpenSCENARIO>
```

Four details of it are the ones a reader is most likely to get differently:

* The parameter a single-parameter distribution varies is named by the
  **`parameterName` attribute of `DeterministicSingleParameterDistribution`**.
  It is not a child element, it is not an attribute called `name`, and it is
  not spelled `parameterRef` — that spelling belongs to the
  `ParameterAssignment` elements inside a `ParameterValueSet`, whose value
  attribute is `value` in both places.
* The distributions sit under one `<Deterministic>` block inside
  `<ParameterValueDistribution>`, beside `<ScenarioFile>`; a document that
  declares no distribution is one whose `ParameterValueDistribution` carries
  no `Deterministic` block at all.
* A range's bounds and step are the `lowerLimit` and `upperLimit` attributes of
  a `<Range>` child and the `stepWidth` attribute of the enclosing
  `<DistributionRange>` — the range is not written with elements of its own.
* The file is read as XML, so indentation, newlines and the whitespace between
  elements carry no meaning, and any element below may be written
  self-closing or with an explicit end tag. The `FileHeader` is present and
  carries the document version; nothing about the distribution is read out of
  it.

How a document that combines those shapes *differently* — several
`ParameterValueSet` elements under one multi-parameter distribution, say — is
counted is **not part of this contract and is out of scope**; the counts that
are required are the ones the three shapes above produce.

With no distribution in force, and for a document that declares none, the count
is 0.

**Enumeration order.** Permutation indices are **0-based**, values within a
parameter are taken in **declaration order**, and the **last declared parameter
varies fastest**. So for `EgoSpeed ∈ {12, 24}` declared before
`TargetSpeed ∈ {5, 10, 15}`, index 0 is `(12, 5)`, index 1 is `(12, 10)`,
index 2 is `(12, 15)`, index 3 is `(24, 5)`, and index 5 is `(24, 15)`. This
order is part of the contract: an implementation that varies the first declared
parameter fastest produces the same *set* of combinations under the wrong
*indices*, and is wrong.

`SE_SelectPermutation` pins the one that the next `SE_Init` will resolve. **It
returns 0 on success**, exactly as `SE_SetParameterDistribution` does in §1 —
that value is fixed and is required, so an implementation that answers 1, or that
echoes the index it just selected, is wrong. An index outside `[0, count)` —
including any index at all when the count is 0 — must instead be **rejected**
with a **negative** return rather than clamped or wrapped, and the rejection
must leave the loaded distribution and the already-selected index undisturbed.
Which negative value is returned is not specified and is out of scope; only the
sign is required. The *success* value is not in that latitude: it is `0`.

**A loaded distribution and the selected index both survive `SE_Init`.**
`SE_Init` *resolves* the selected permutation — it substitutes that
permutation's values into the scenario it initialises — but it does not consume
the distribution and does not undo the selection. Once `SE_Init` has returned,
`SE_GetNumberOfPermutations` still reports the same count and
`SE_GetPermutationIndex` still reports the index `SE_SelectPermutation` pinned:
the count does not fall back to 0 and the index is not reset to permutation 0.
Both readings are taken on the far side of `SE_Init` and both are required.
`SE_ResetParameterDistribution` (§1) is what discards a loaded distribution;
`SE_Init` is not.

What a *second* `SE_Init` resolves while the same distribution is still loaded
is deliberately left open: §3's `--param_dist` has to play every permutation
from a single invocation of the player, and how the engine walks the enumeration
between those runs is your choice and is out of scope here. What is required is
the pair of readings above, taken after the first `SE_Init` under an explicit
selection.

`SE_GetPermutationIndex` reports the permutation currently selected. A
successful load leaves permutation **0** selected, so the index reads 0
immediately after `SE_SetParameterDistribution` returns and before any explicit
`SE_SelectPermutation` — including after a document that enumerates zero
permutations.

## 3. Drive distributions from the command line

`esmini` gains two options:

* `--param_dist <filename>` — apply a parameter-value-distribution file to the
  scenario named by `--osc`. **On its own it runs every permutation**, in
  enumeration order, one execution of the scenario after another from a single
  invocation of the player, and exits when the last one finishes.
* `--param_permutation <index>` — run that one permutation and stop, instead of
  sweeping. The index is 0-based and uses the order of §2. An index outside
  `[0, count)` is refused: the player exits non-zero and runs no scenario. With
  no distribution loaded at all, every **non-zero** index is refused the same
  way. Index `0` with no distribution loaded is the one corner of this rule the
  step leaves alone: it is neither required to be refused nor required to run,
  and no check uses it.

**Per-permutation output filenames.** Because one invocation can produce many
runs, every output file a run writes is decorated with the permutation it
belongs to. Two of those outputs are in scope here — the `--record` recording and
the `--save_xosc` scenario of §4 — and the rule below is checked on both. (The
engine's own console log and a `--csv_logger` file follow the same rule, but no
check at this step reads either of them under a distribution.) The decoration
is:

    <stem>_<index + 1>_of_<count><ext>

so `--param_dist` over a six-permutation distribution with `--record rec.dat`
writes `rec_1_of_6.dat` through `rec_6_of_6.dat`. The decoration is **1-based**
while the index is 0-based, so `--param_permutation 3` writes `rec_4_of_6.dat`.
The **undecorated** name is not written at all: no `rec.dat` appears beside
them. This holds for the single-permutation form as much as for the sweep.

## 4. Write the resolved scenario back out

`--save_xosc` writes the scenario as resolved — with the selected permutation's
parameter values substituted in — to a new OpenSCENARIO file. It takes **no
argument**: it is a bare flag, and the following token on the command line is
the next option, not a filename.

The name is derived instead. The resolved file is written beside the scenario
under the same per-permutation decoration as §3, built from the scenario's own
stem: running `--osc param_cars.xosc --param_dist <six permutations>
--param_permutation 3 --save_xosc` produces `param_cars_4_of_6.xosc`.

Two properties of the file itself:

* Parameters the distribution assigns carry the **selected permutation's**
  values; parameters it says nothing about keep the values the scenario
  declared. The substitution is targeted, not a rewrite of the declaration
  block, and the scenario given to `--osc` is input only — it must not be
  rewritten in place.

  **Where the values land.** The resolved value is written into the
  scenario's own top-level `<ParameterDeclarations>` block, in the `value`
  attribute of the `<ParameterDeclaration>` that declares the parameter — not
  into a new element, a comment or a sidecar. The block is emitted the way the
  source scenario declares it, one declaration per line with `name` before
  `value`; the check reads it as text, matching
  `name="EgoSpeed"` and then `value="24"` on the same line, and requiring that
  the declared `value="20"` is no longer on it. Whitespace between attributes
  and the spelling of the self-closing tag are not significant.
* **The file it produces must itself be loadable by the engine**: feeding it
  back through `--osc` loads and runs it. This is checked, not assumed.

## 5. Three additions to the scenario-engine library

```c
SE_DLL_API void   SE_SetDatFilePath(const char* datFilePath);
SE_DLL_API int    SE_GetIdByName(const char* name);
SE_DLL_API double SE_GetSimulationTimeDouble();
```

`SE_SetDatFilePath` chooses where a recording is written, and must be honoured
by an `SE_Init` that requests recording: the file appears at exactly the path
given, and the name derived from the scenario is not written alongside it.

`SE_GetIdByName` maps an entity's scenario name to its numeric id, agreeing with
the existing index-based `SE_GetId` about the same entity, and returns a
**negative** value both for a name that is not in the scenario and when no
scenario is initialised. Which negative value is not specified and is out of
scope; only the sign is required.

`SE_GetSimulationTimeDouble` returns the simulation clock in double precision.
The existing single-precision `SE_GetSimulationTime` stays, and the two must
agree to within float resolution. They are the **same clock at two precisions**,
not two clocks: the double form must return the engine's own double-precision
time, and the float form that time narrowed to `float` — not the reverse.
`SE_GetSimulationTimeDouble` implemented as the single-precision value widened
back to `double` does not satisfy this, and at an instant that `float` cannot
represent exactly the two readings therefore differ.

The clock **starts at zero**, and both accessors report that: they read `0`
before any `SE_Init`, when there is no scenario to measure, and they read `0`
again on a scenario that has been initialised but not yet stepped. `SE_Init`
therefore resets the clock rather than continuing a previous run's.

## 6. Storyboard element state-change callback

```c
SE_DLL_API void SE_RegisterStoryBoardElementStateChangeCallback(
    void (*fnPtr)(const char* name, int type, int state));
```

Registers a callback invoked whenever a storyboard element changes state. The
callback receives the element's name, its type, and the state it moved to. It
must fire for the storyboard elements — act, manoeuvre group, manoeuvre, event
and action — as the scenario runs.

**It reports state changes, not transition requests, and it hangs on the
storyboard machinery that already exists.** The engine's storyboard elements
already move between `STANDBY`, `RUNNING` and `COMPLETE` at v2.23.1 and already
print each such move to the console; what this step adds is the delivery of
those same moves to a registered callback. The callback fires exactly once each
time an element's state is genuinely different from the state it held before,
and never when an element is re-asserted into the state it already holds. A
count of the calls a scenario produces is therefore a count of its state
changes, not two calls per element, and three consequences follow. All three
are inherited behaviour, identical at v2.23.1 and v2.27.0, and all three are
checked:

* **An element that never occupies `RUNNING` is reported only as `COMPLETE`.**
  An action whose dynamics finish in the same step that starts it is atomic: it
  goes from `STANDBY` directly to `COMPLETE`, as the inherited console trace
  `<name> standbyState -> endTransition -> completeState` shows, and so does the
  event that holds it. Each is reported once, with state `3`, and neither is
  ever reported with state `2`.
* **An element still running when the scenario ends is not reported complete.**
  When the storyboard's stop trigger ends the run, the run is over before any
  further state change is applied, so an act that was `RUNNING` reports nothing
  more.
* **The story element is not delivered through this entry point.** `STORY = 1`
  appears in the table below because it is part of the public code numbering,
  not because a story-level state change is observable here.

**The manoeuvre is the one element whose participation this step changes, and
it is required.** At the starting tag a manoeuvre is not a participating
element of the running storyboard at all: its manoeuvre group is started,
updated and ended without ever starting, updating or ending it, so it never
changes state and never appears in the console trace, while its group, its act
and its events all do. That is the gap to close. A manoeuvre must be driven
like the other elements: it starts when its group starts, and it reaches
`COMPLETE` once its events are done. A scenario whose single named manoeuvre
starts and finishes therefore reports that manoeuvre exactly twice — running
(`2`), then complete (`3`), in that order — with the same type code both times.

`type` and `state` are **numeric codes on the public API**, so their values are
part of this interface and not an internal detail:

    type:   UNDEFINED = 0, STORY = 1, ACT = 2, MANEUVER_GROUP = 3,
            MANEUVER = 4, EVENT = 5, ACTION = 6
    state:  UNDEFINED = 0, STANDBY = 1, RUNNING = 2, COMPLETE = 3

Use exactly these. Renumbering the engine's internal storyboard-element
enumerations is part of this release, and the numbering above is the one the
callback must report — an internal enum inherited from the starting tree
numbers them differently, and passing it through unchanged is wrong.

**Registrations are cleared by `SE_Init`.** A callback registered before
`SE_Init` does not survive it, so a host application registers *after* the
scenario is initialised. Transitions that happen during `SE_Init` itself are
consequently not observable through this entry point and are not required.

## 7. Retire the event callback

`SE_RegisterEventCallback` is removed. It must no longer appear in the
library's exported, C-linkage symbol table, and code written against it must
fail to compile and link. The storyboard-element callback in §6 replaces it.

## 8. Export the additions with C linkage

Every entry point named above — `SE_SetParameterDistribution`,
`SE_ResetParameterDistribution`, `SE_GetNumberOfPermutations`,
`SE_SelectPermutation`, `SE_GetPermutationIndex`, `SE_SetDatFilePath`,
`SE_GetIdByName`, `SE_GetSimulationTimeDouble`,
`SE_RegisterStoryBoardElementStateChangeCallback` and `SE_SetOffScreenRendering`
— must be exported from `libesminiLib.so` under its own unmangled name. A
C++-mangled spelling does not satisfy this.

`SE_SetOffScreenRendering` is the one name in that list not introduced by an
earlier section, so its declaration is given here:

    SE_DLL_API int SE_SetOffScreenRendering(bool state);

It enables or disables the off-screen framebuffer-capture path, which only
exists in a build with OSG on. This profile builds with OSG off, so the function
has no observable effect here and no behaviour is required: what is checked is
that the symbol is present, with C linkage, in `libesminiLib.so`'s exported
table. It is not called by any check.

## Pre-existing entry points the checks also use

Getting at any of the above means driving a scenario, and the checker's own
programs do that through the init/step/close cycle that already exists at
v2.23.1. Those declarations are therefore part of what this step must keep
working. They are in `esminiLib.hpp` in your workspace; they are reproduced
here, together with the inherited road-manager and simple-vehicle entry points
the checker's programs also link against, so that nothing the checker calls has
to be inferred from a header.

**The scenario-engine cycle**, from `esminiLib.hpp`:

```c
SE_DLL_API int  SE_Init(const char* oscFilename, int disable_ctrls,
                        int use_viewer, int threads, int record);
SE_DLL_API int  SE_Step();
SE_DLL_API int  SE_StepDT(float dt);
SE_DLL_API void SE_Close();
SE_DLL_API int  SE_GetId(int index);
SE_DLL_API int  SE_GetObjectState(int object_id, SE_ScenarioObjectState* state);
```

`SE_Init`'s **last** argument is the record flag: a non-zero value makes the run
write a `.dat` recording, and that is the recording §5's `SE_SetDatFilePath`
redirects. `SE_Init` returns 0 on success. `SE_GetId` maps a 0-based
declaration index to an entity id, and it is the lookup §5's `SE_GetIdByName`
has to agree with for the same entity. `SE_GetObjectState` fills an
`SE_ScenarioObjectState`, whose pre-existing `float speed` member is how one
permutation's run is told from another's — that member is not among the struct
changes the out-of-scope list below sets aside.

**The road-manager query surface**, from `esminiRMLib.hpp`. The checker links a
second program against `libesminiRMLib.so` to prove the road manager did not
regress while the scenario engine grew. These declarations are byte-identical at
v2.23.1 and v2.27.0 and must stay that way:

```c
RM_DLL_API int   RM_Init(const char* odrFilename);
RM_DLL_API int   RM_Close();
RM_DLL_API int   RM_GetNumberOfRoads();
RM_DLL_API int   RM_GetIdOfRoadFromIndex(int index);
RM_DLL_API float RM_GetRoadLength(int id);
RM_DLL_API int   RM_GetRoadNumberOfLanes(int roadId, float s);
RM_DLL_API int   RM_CreatePosition();
RM_DLL_API int   RM_SetLanePosition(int handle, int roadId, int laneId,
                                    float laneOffset, float s, bool align);
RM_DLL_API int   RM_SetWorldXYHPosition(int handle, float x, float y, float h);
RM_DLL_API int   RM_GetPositionData(int handle, RM_PositionData* data);
RM_DLL_API float RM_GetLaneWidth(int handle, int lane_id);
RM_DLL_API float RM_GetLaneWidthByRoadId(int road_id, int lane_id, float s);
RM_DLL_API int   RM_GetLaneType(int handle, int lane_id);
RM_DLL_API int   RM_GetLaneTypeByRoadId(int road_id, int lane_id, float s);
```

`RM_CreatePosition` returns a **handle**, numbered from `0` in creation order,
and the handle-addressed queries take it as their first argument. Of the
`RM_PositionData` struct the checker reads only `roadId`, `laneId`, `s`, `x`,
`y` and `h`; the other members are not inspected. Per the Notes below, the
position setters report success with any non-negative code, not only `0`.

**The simple-vehicle model**, from `esminiLib.hpp`:

```c
SE_DLL_API void* SE_SimpleVehicleCreate(float x, float y, float h,
                                        float length, float speed);
SE_DLL_API void  SE_SimpleVehicleSetMaxDeceleration(void* handleSimpleVehicle,
                                                    float maxDeceleration);
SE_DLL_API void  SE_SimpleVehicleControlBinary(void* handleSimpleVehicle,
                                               double dt, int throttle,
                                               int steering);
SE_DLL_API void  SE_SimpleVehicleGetState(void* handleSimpleVehicle,
                                          SE_SimpleVehicleState* state);
SE_DLL_API void  SE_SimpleVehicleDelete(void* handleSimpleVehicle);
```

Of `SE_SimpleVehicleState` the checker reads only the `float speed` member.
The struct's wheel-angle and wheel-rotation members are **deliberately not**
read and are not part of this contract — upstream corrects their spelling
inside this release range and the checker must not care either way.

## Out of scope at this step

The v2.23.1 -> v2.27.0 range carries other upstream changes that this step does
**not** require and does not check. No check here looks for any of them, so
implementing them is neither required nor harmful and leaving them alone is
the expected answer:

* the `float speed` parameter dropped from the `SE_ReportObjectPos`,
  `SE_ReportObjectPosXYH` and `SE_ReportObjectRoadPos` declarations, and the
  `wheel_angle` / `wheel_rot` members added to `SE_ScenarioObjectState`;
* the realignment of `SE_GetRoadInfoAtDistance`'s return codes with the internal
  road-manager codes;
* the `%` and `round` expression operators;
* generated road ids for OpenDRIVE roads that declare none;
* `miscObjectCategory` as an alternative `MiscObject` category spelling;
* `odrplot`'s end-of-lane-section sampling clamp;
* logging scenario parameter names and values to the log file;
* the viewer-only options `--custom_light`, `--lod` and `--lodScale`, which an
  OSG-disabled profile cannot exercise in any case.

**One behaviour in this range is not optional**, because a later step's gate
re-runs the surface it touches and pins the answer this range produces: the road
manager's overlap enumeration. `RM_GetNumberOfRoadsOverlapping` and
`RM_GetOverlappingRoadId` keep their signatures and their subject — the roads
whose paved surface lies under the handle's world position — but the position
search this range brings in registers a road only where that road's own centre
line brackets the position, that is, where the position falls between the
extended normals of two consecutive OSI points of the road. Consecutive normals
of a perfectly straight centre line are parallel and never bracket anything, so
**on a straight road the count is `0` and index `0` answers `-1`, however
squarely the position sits on the road's lanes**, where v2.23.1 reported the
road itself with a count of `1`. A curved road brackets its own projection and
still reports itself, a point that several roads' surfaces cover still reports
each of them, and a point off every road's lanes is still `0`. On the checker's
fixtures: `straight_500m.xodr` answers `0` at every point, and `e6mini.xodr` — a
lone curved road — answers `1` with that road's id at index `0`. Adopt this
answer; a tree that keeps the v2.23.1 one fails the later step that re-runs
these queries.

That list says what this step does not **add**. It is not permission to
regress: by the Notes below, everything the engine already does must keep
working unchanged, and that includes the `.dat` recording format and its
version number, the `--csv_logger` header text, the `dat2csv` output lines and
the `--record` path forms already accepted at v2.23.1.

## Exact output that must not change

"Everything the engine already does must keep working" includes the bytes the
tools print. None of the text below is something this step asks you to add or
alter: it is what the starting tree already prints, it is unchanged at the
release you are reproducing, and it is written down here so that "do not change
it" is an obligation you were given rather than one you have to infer. The
checker reads these literally.

**The `esmini` usage banner.** Run with no arguments — or with `--help` —
`esmini` prints the line `Usage: esmini [options]`, then the line `Options: `,
then one line per option indented by exactly two spaces. `  --osc <filename>`,
`  --headless` and `  --fixed_timestep <timestep>` are among those lines at
v2.23.1 and must still be. The two options of §3 and the flag of §4 **join**
that list; they do not replace it, and reorganising or rewording the banner
while adding them is a regression. That clause is checked in both directions:
the banner is read back for `--param_dist`, `--param_permutation` and
`--save_xosc` by name, under the same two-space indent, and for the inherited
lines still standing beside them — so parsing an option without advertising it
is a miss. Only the option *names* and the indent are read; the descriptive
text that follows a name on its line is not pinned, so you may word it however
you like. The banner's *exit status* is deliberately not part of this contract
— upstream returns a non-zero status here and the checker does not pin the
number.

**A headless run's console trace.** Loading a scenario prints
`Loading <the file as it was named on the command line> (v<OpenSCENARIO
version>)`, so `--osc two_car_brake.xosc` on a v1.0 scenario prints
`Loading two_car_brake.xosc (v1.0)`. Each storyboard transition of the init
actions prints one line of the form `<time to three decimals>: Init <entity>
<action type> <state> -> <transition> -> <state>`, for example

    0.000: Init Ego TeleportAction standbyState -> startTransition -> runningState
    0.000: Init Ego TeleportAction runningState -> endTransition -> completeState

A `--headless` run opens no window and prints nothing about one.

**`odrplot` with no arguments** prints

    Usage: ordplot openDriveFile.xodr [Output file, default=output.csv] [Sampling_step, default=1.0]

`ordplot` is upstream's own misspelling of the program's name. It is part of the
string, not a typo to correct.

**`--csv_logger <path>`** writes six banner lines — the three `esmini GIT …`
lines, `esmini BUILD VERSION: …`, `Scenario File Name: …` and
`Number of Vehicles: …` — and then one column-header line that begins, byte for
byte,

    Index [-] , TimeStamp [s] , #1 Entitity_Name [-] , #1 Entitity_ID [-] , #1 Current_Speed [m/s] , 

and continues with the rest of the `#1 ` block and then the same block once per
further entity. Every separator is a space, a comma and a space, and
**`Entitity` is misspelled** in the two columns that carry it. The misspelling
is part of the format: correcting it changes the format and is a regression.

**`dat2csv <recording>.dat`** derives its output name from its input: it writes
`<recording>.csv` into the **current working directory**, named from the
**basename** of the input, and exits `0` — the file lands where the tool was
run, not beside the recording it read. That file's first line is
`Version: %d, OpenDRIVE: %s, 3DModel: %s` — format version `2`, and the road
network the run used; the `3DModel` field is empty in this profile because no
scene graph is loaded. The `OpenDRIVE` field carries that network's **file name
exactly as the scenario named it** in its `<LogicFile filepath="…"/>`, not the
absolute path the loader resolved it to: both fixtures here declare
`straight_500m.xodr`, so both decode to `OpenDRIVE: straight_500m.xodr` whatever
directory the run happened in. The checker matches that field literally.

That decoded file's second line is the column header
`time, id, name, x, y, z, h, p, r, speed, wheel_angle, wheel_rot`, so the header
is not the file's first line. Then one `%.3f, %d, %s, …` row per sample, the
`id` and `name` columns carrying the scenario's own entity ids and names —
which are assigned in declaration order from `0`, so a recording of either
two-entity fixture decodes to a first row beginning `0.000, 0, Ego, `.
`WORKSPACE_HANDOFF.md` records the same lines in full.

**A successful `esmini` run exits `0`.** A headless run that loads its scenario,
plays it to its stop trigger and closes returns zero status. The usage banner
above is the deliberate exception, and a *refused* `--param_permutation` is
required by §3 to exit non-zero.

**`esmini-dyn` runs on the same terms.** It is the example player that drives
the engine through `libesminiLib.so` rather than linking the engine statically,
and it accepts the same `--osc <filename>`, `--headless` and
`--fixed_timestep <timestep>` options with the same meanings. Given a scenario
it can play, it plays it to its stop trigger and exits `0`, exactly as `esmini`
does; the checker runs it that way. Keeping it building, keeping it linked
against the shared library, and keeping that exit status are inherited
obligations — none of this step's new options has to be wired into it.

## Notes

* Road-manager position setters — `RM_SetLanePosition` and
  `RM_SetWorldXYHPosition`, and only those — return 1 and 2 as well as 0 to
  mean success, so the checker treats any non-negative return from **those two**
  as success and never tests them for `== 0`. That latitude is theirs alone. It
  does not extend to the `SE_*` surface: where a section above states that an
  `SE_*` entry point returns 0 on success — `SE_SetParameterDistribution` (§1),
  `SE_SelectPermutation` (§2), `SE_Init` — the success value is exactly `0` and
  is checked as such.
* Everything the engine already does must keep working unchanged: the
  road-manager core queries, lane width and lane type, the simple-vehicle
  deceleration model, and running a scenario headless on a fixed timestep.
* That includes the console and file text listed under "Exact output that must
  not change" above — the `esmini` usage banner, a headless run's loading and
  init-transition lines, `odrplot`'s usage line, the `--csv_logger` column
  header and the `dat2csv` lines. Those strings are inherited, they are checked
  literally, and the options this step adds join the banner rather than
  replacing it.

## Scope

This environment has no network access, and everything the build needs is
already installed. The upstream project's source, its releases, mirrors and
package registries are unreachable, so a copy cannot be fetched by any
route.
