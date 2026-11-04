# Bounding boxes, dynamic objects and road alignment

The engine in `workdir/` is at v2.27.5. Advance it to the v2.31.10 feature set.
The headline capability is **bounding boxes**: entities carry a real box that
flows into the recording format, and the scenario-engine library gains entry
points to add objects at runtime with an explicit box, to remove them again,
to measure the distance between two objects, to control per-component road
alignment, and to restrict which lane types a position may snap onto.

Build configuration does not change. The tree is configured offline and
headless: `USE_OSG`, `USE_OSI`, `USE_SUMO`, `USE_GTEST`, `USE_IMPLOT` and
`USE_PROJ` are all `False`, nothing is downloaded, and the published artifacts
are `esmini`, `esmini-dyn`, `odrplot`, `dat2csv`, `libesminiLib.so` and
`libesminiRMLib.so`. `replayer` and `odrviewer` are not built and must not be.

Everything this document builds on but does not define — the names, argument
order, struct fields and success conventions of the entry points that already
exist in the tree you were given — is written down in
`workdir/STEP_04_INTERFACE.md`. Nothing in that file is a task; it exists so
that no existing interface has to be guessed at. This document is the only one
that describes new work.

## 1. Bounding box and lane identity in the CSV recording

`esmini --csv_logger <file>` gains new columns in each entity's block. For
entity index `N` the header gains, in this order, immediately after that
entity's `Wheel_Rotation` column and before its `World_Position_X` column:

```
#N bb_x [m] , #N bb_y [m] , #N bb_z [m] , #N bb_length [m] , #N bb_width [m] , #N bb_height [m] ,
```

`bb_x`/`bb_y`/`bb_z` are the offset of the box centre from the entity's
reference point, and `bb_length`/`bb_width`/`bb_height` are its dimensions —
the same six numbers an OpenSCENARIO `<BoundingBox>` declares. They are the
entity's own box: two entities with different `<BoundingBox>` declarations
report different numbers in their respective blocks, not a shared constant.

Separately, immediately after that entity's `Lateral_Distance_Lanem` column
and before its `World_Heading_Angle` column, the header gains:

```
#1 lane_id, #1 lane_offset[m] ,     <- the first entity block
#N lane_id, #N lane_offset [m] ,    <- every block from #2 on
```

reporting the entity's current lane id and its offset within that lane.

These two columns are the one place in the header where the whitespace is
**not** the uniform `<name> , ` every other column uses, and the checker
compares header fields byte for byte, so reproduce them exactly:

* `lane_id` is followed by a comma with **no space before it**.
* the unit suffix of `lane_offset` is written `[m]`, closed up against the
  name, in the `#1` block and ` [m]`, with a space, in every block from `#2`
  on.

The **values** in the new columns follow the file's existing conventions, and
the checker reads them as text, so their formatting is part of the contract
too. Every value in a data row is written with a leading space, as the rest of
the file already does. The six `bb_*` values and `lane_offset` are floating
point printed to six decimal places — an entity whose `<BoundingBox>` declares
`length="5.0"` writes ` 5.000000`, and an entity on the lane centreline writes
` 0.000000` for its offset. `lane_id` is the exception: it is a **plain signed
integer with no decimal point at all**, so the right-hand lane of a two-way
road reads ` -1` and not ` -1.000000`. That asymmetry is not an oversight —
a lane id is an identifier, not a measurement — but it is the kind of detail
that is easy to get wrong by formatting the whole new block uniformly, so it
is written down here.

The preamble is six lines and none of them change: `esmini GIT REV:`,
`esmini GIT TAG:`, `esmini GIT BRANCH:`, `esmini BUILD VERSION:`,
`Scenario File Name:` and `Number of Vehicles:`. The value on the three `GIT`
lines is filled in from `git` when the source tree is configured and reads
`N/A` when it is not a git checkout; the checker requires the lines and their
shape, never a particular revision, so either answer is accepted. The rest of
the column layout is unchanged too, including the `Entitity_Name` /
`Entitity_ID` misspelling of "Entity" and the space before every other comma.

## 2. Add and remove objects at runtime

```c
SE_DLL_API int SE_AddObject(const char *object_name, int object_type, int object_category, int object_role, int model_id);
SE_DLL_API int SE_DeleteObject(int object_id);
```

`SE_AddObject` creates a new scenario object after `SE_Init` and returns its
id — the next id not already in use, never a fixed value, and never colliding
with an id a scenario's own `<Entities>` block already occupies. The returned
id is immediately usable: `SE_GetObjectState` and `SE_GetObjectName` accept it
and report the name it was given. Calling `SE_AddObject` before `SE_Init`
fails and returns a negative value.

"Next id not already in use" is a rule you can predict from the scenario you
were handed, and the checker relies on it. The scenario's own entities occupy
the ids `0` through `n-1` in the order their `<Entities>` block declares them,
and the engine's allocator is a counter that starts at `0` and is consumed as
those entities are created, so it stands at `n` by the time you start adding.
The first object you add therefore takes the id the scenario's last entity
would have had — `n` — and each addition after that takes the next one up:
`n+1`, `n+2`, and so on. A scenario declaring two vehicles leaves the first
added object the id `2` and the second the id `3`. The rule is stated here
rather than left to a fixed number because it is the `n` that depends on the
scenario: the check below adds two objects to a two-entity scenario and
requires `2` and `3`, which a build that hands back `0` and `1` — or one
constant for both — does not satisfy.

The `object_role` argument of both add forms is the one part of the signature
with no observable consequence at this release, and it is out of scope. It is
accepted at the ABI and stored on the object, but the published
state readback does not carry it: `SE_ScenarioObjectState` has `objectType`
and `objectCategory` fields and no role field, so no exported call hands the
role back to you. Pass whatever the caller needs there; nothing reads it out.

`object_type` and `object_category`, by contrast, are observable and are
required. They are stored on the created object and handed back unchanged
through `SE_GetObjectState`, in the `objectType` and `objectCategory` fields
of `SE_ScenarioObjectState` — the same two fields the readback already carries
for a scenario's own entities. An object added with `object_type` 1 (vehicle)
and `object_category` 1 (car) reads back `objectType == 1` and
`objectCategory == 1`; an implementation that accepts the two arguments and
then registers every added object under some default does not satisfy this.

`SE_DeleteObject` removes a previously added object and returns 0. After a
successful delete, the object count reported by the library falls by exactly
one, an object that was not deleted remains fully readable, and a second
`SE_DeleteObject` on the same, now-gone id fails and returns a negative value.

## 3. Add an object with an explicit bounding box

```c
typedef struct {
    float x_;
    float y_;
    float z_;
} SE_Center;

typedef struct {
    float width_;
    float length_;
    float height_;
} SE_Dimensions;

typedef struct {
    SE_Center     center_;
    SE_Dimensions dimensions_;
} SE_OSCBoundingBox;

SE_DLL_API int SE_AddObjectWithBoundingBox(const char *object_name, int object_type, int object_category,
                                            int object_role, int model_id, SE_OSCBoundingBox bounding_box,
                                            int scale_mode);
```

This adds an object exactly as `SE_AddObject` does, but the object's bounding
box is the one the caller supplies rather than whatever a 3D model would
imply. `scale_mode` follows `CommonMini::EntityScaleMode`, whose three values
are `0` (`NONE`, keep the box exactly as given), `1` (`BB_TO_MODEL`, scale the
box to the 3D model) and `2` (`MODEL_TO_BB`, scale the model to the box); only
`0` is in scope here. The box that comes back out of `SE_GetObjectState` for the
returned id must be the box that went in — its centre and its three
dimensions — for any box the caller supplies, not a fixed shape.

The existing `SE_AddObject` declared in section 2 — five arguments, ending
in `model_id` — is the same call with no box: it
supplies an all-zero box and scale mode `1` (`BB_TO_MODEL`), so the dimensions
such an object ends up with are the ones its 3D model implies. **This build
has no 3D models** — `USE_OSG` is off and `resources/models` is empty — so
under the profile the checker uses there is nothing to scale to and an object
added through `SE_AddObject` reports a box of all zeros: `length`, `width` and
`height` each read back as `0` through `SE_GetObjectState`. That zero box is
the observable difference between the two forms at this step. The object is
otherwise complete: it gets a usable id, a readable state and a name, exactly
as the box form does.

## 4. Distance between two objects

```c
typedef struct {
    float ds;
    float dt;
    int   dLaneId;
    float dx;
    float dy;
    bool  oppositeLanes;
} SE_PositionDiff;

SE_DLL_API int SE_GetDistanceToObject(int object_a_id, int object_b_id, bool free_space, SE_PositionDiff *pos_diff);
```

Fills `pos_diff` with the road-relative offset from object A to object B: `ds`
is the longitudinal distance measured along object A's own direction of
travel, `dt` the lateral separation defined just below, `dLaneId` the lane-id
delta described below, and `oppositeLanes` is true when the two objects'
reference lanes run in opposite directions. `free_space` selects between
measuring from reference point to reference point (`false`) and from the
nearer edges of the two objects' bounding boxes (`true`); the free-space
distance is smaller than the reference-point distance whenever the boxes are
non-degenerate.

`dt` is measured on the **road's** lateral axis, not on either object's own
axis: it is B's `t` — its signed lateral position, the same coordinate a
`LanePosition`'s `offset` displaces — less A's own `t` taken as a magnitude and
given the sign of **A's lane id**, so with both objects on their lane centres it
is the plain lateral gap between the two lanes. It is positive when B lies at a
greater `t` than A's own lateral position and negative when it lies at a smaller
one, whatever heading either object holds, and it therefore changes sign when
the query is reversed (the last case of this step measures that on the
opposite-lane pair, where `ds` does not).

`dLaneId` is the lane-id delta **counted without the centre lane**. Lane `0`
is the road's reference lane: it carries no traffic and is not counted as one
of the lanes separating the two objects. Equivalently, it is the plain
difference of the two lane ids, reduced by one whenever the two lanes lie on
opposite sides of the centre line. So lanes `-2` and `+3` differ by `4`, not
`5`; lanes `-1` and `+1` differ by `1`, not `2`; lanes `-1` and `-3` differ by
`2`, since no centre lane lies between them; and two objects in the same lane
differ by `0`. The plain arithmetic difference of the two lane ids is *not*
what this field reports.

`dx` and `dy` are the **same A-to-B offset that `ds`/`dt` describes, resolved
in global axes instead of the road's** — the `x` and `y` components of "where
B is, minus where A is". They are the fields to read when you want the offset
as a plain vector rather than as a longitudinal/lateral pair, and they are the
pair to cross-check `ds`/`dt` against: on a straight road with both objects at
heading `0` the road frame and the global frame coincide, and `dx`/`dy`
reproduce the `ds`/`dt` of the same query exactly, as the two cases below
measure. To be exact about the frame: the difference vector is resolved about the
**asking object's own heading**, not the road's. The engine takes the
world-frame difference `B - A` and rotates it by `-h` of the asking object, so
the resulting `x` axis points wherever that object is facing. The frame
therefore follows the *object*: an object sitting at an angle to its own lane
resolves `dx`/`dy` about its own heading, and the road's direction does not
enter the calculation at all. Every distance fixture at this step places the
asking object at heading `0` on a road whose single geometry is also heading
`0`, so object frame, road frame and world frame all coincide here — which is
why every `dx`/`dy` value quoted below is equally readable as the plain
world-frame difference.

**With `free_space` `true`, `dx` and `dy` mean something narrower than
`ds`/`dt` do.** With `free_space` `false` the four values are one displacement
in two frames, and `dx`/`dy` are always the global form of the `ds`/`dt`
returned alongside them. With `free_space` `true` they are not, and the
mechanism is worth writing down, because the two cases below assert the values
it produces.

The bounding-box path replaces `ds` and `dt`, but not `dx` and `dy`. It walks
all sixteen pairs formed by the four corners of A's box against the four
corners of B's box, and **each pair assigns its own `ds`, `dt`, `dx` and `dy`**
into the caller's struct. When the scan finishes, the routine overwrites `ds`
and `dt` with the smallest magnitudes the scan saw — the near-edge measurement
the field names promise — and leaves `dx` and `dy` holding the values written
by the **last pair scanned**. That last pair is corner 3 of each box, where
corner 3 is the corner at (`center.x + length/2`, `center.y - width/2`) in the
object's own frame, rotated by that object's heading and translated to its
position. (Two boxes that overlap short-circuit before the scan and report
`ds`, `dt`, `dx` and `dy` all `0`; neither pair below overlaps.)

Both free-space `dx`/`dy` values the cases assert therefore follow from the
fixture by arithmetic, and are meant to be reproduced rather than taken on
trust:

* **Same lane.** Both vehicles carry the identical box — centre `1.4, 0`,
  length `5.0`, width `2.0` — at heading `0` in lane `-1`, so corner 3 sits at
  the same offset from each reference point and that offset cancels in the
  difference. `free_dx` is exactly the difference of the two reference points'
  world `x`, which is the same `99.5` the reference-point query reports, and
  `free_dy` is `0.0`. Only `free_ds` moves to the near-edge value `94.5`,
  shorter by both half-lengths. The driver prints each object's lane, its `s`
  and its box length beside the result — `a_lane`, `a_s`, `a_length` and the
  same three `b_*` keys — so the cross-check is in the case's own output.
* **Opposite lanes.** A is at `x 50` in lane `-1` (`y -1.535`) at heading `0`
  with box centre `1.4, 0` and dimensions `5.0` by `2.0`; B is at `x 200` in
  lane `1` (`y 1.535`) at heading `pi` with box centre `3.5, 0` and dimensions
  `9.0` by `2.5`. Corner 3 of A is therefore at `(53.9, -2.535)` and corner 3
  of B, rotated by `pi`, at `(192.0, 2.785)`. Their difference is
  `free_dx 138.1`, `free_dy 5.32`. That is why `free_dy` comes back *wider*
  than the reference-point lateral gap of `3.07`, while `free_dt` — which is
  the real box measurement — correctly comes back *narrower*, at `0.82`.

On the free-space path, then, `ds`/`dt` are the near-edge measurement and
`dx`/`dy` are one corner pair's displacement — not the near-edge offset
resolved in global axes, which is what the field names alone would suggest. It
is a quirk of the engine rather than a design, but it is the engine's
behaviour at this release, it is observable, and it is reproducible from the
numbers above, so the cases assert all four values and a solution has to
reproduce all four. In particular, do **not** compute free-space `dx` as the
offset between the nearest box edges: on the same-lane pair that would give
`94.5`, and this release returns — and the case asserts — `99.5`.

The sign of every delta follows the direction of the query, A -> B, and `ds`,
`dx` and `dy` are each resolved in the asking object's own frame — its
direction of travel and its heading. `dt` is the exception, for the reason
given where it is defined above: it keeps the road's lateral axis rather than
either object's. Asking B about A therefore
flips the sign of `dt` and of `dLaneId`; it flips the sign of `ds` only when
the two objects face the same way, because `ds` is always measured along the
asking object's own direction of travel — for a pair in opposite lanes the
other object is "ahead" from both ends, and `oppositeLanes` is the flag that
says so. `dx` and `dy` obey the same rule as `ds`, and for the same reason:
the axes they are resolved in belong to whichever object is asking. Reversing
the query negates the displacement *and* re-resolves it into the other
object's axes. Where the two objects face the same way those axes coincide,
so only the negation survives and the reversal is a plain sign flip — the
same-lane pair turns `dx 99.5` into `dx -99.5`. Where they face opposite ways
the axes are flipped as well, the two negations cancel, and `dx`/`dy` come
back **unchanged**: on the opposite-lane pair the query reports `dx 150.0`,
`dy 3.07` in *both* directions. Both behaviours are measured on this release
and both are asserted by the cases.

Returns 0 on success. It returns a negative value if no path between the two
positions can be found, and likewise if either argument is an object id no
scenario defines: an unknown id is rejected, never answered with a distance.

The no-path refusal applies to the **reference-point** query, the one with
`free_space` false, because that is the one that routes: it walks the road
network from one position to the other, and if the two positions sit on
disconnected components of the network there is nothing for it to walk. The
free-space query does not route. As described above it scans the sixteen
bounding-box corner pairs and reports straight-line geometry between them,
which is defined whether or not a road connects the boxes, so on a
disconnected pair it returns 0 and hands back a real `dx`/`dy` across the gap.
This is measured on this release and the cases require it as stated: the
refusal is required of the reference-point query only. The unknown-id
rejection, by contrast, is required of both — an id no scenario defines has no
box to scan either.

Which negative value comes back is **not** part of the contract and the cases
do not pin it. They require the sign. Two different negative codes are in fact
returned for the two refusals at this release; reproducing either one is not
asked of you, only that a refusal is negative and a success is 0.

## 5. Per-component road alignment

```c
SE_DLL_API void SE_SetAlignMode(int object_id, int mode);
SE_DLL_API void SE_SetAlignModeH(int object_id, int mode);
SE_DLL_API void SE_SetAlignModeP(int object_id, int mode);
SE_DLL_API void SE_SetAlignModeR(int object_id, int mode);
SE_DLL_API void SE_SetAlignModeZ(int object_id, int mode);
```

`mode` is one of `0` (`ALIGN_NONE`), `1` (`ALIGN_SOFT`) or `2` (`ALIGN_HARD`).
`SE_SetAlignMode` sets heading, pitch, roll and Z alignment together; the
`H`/`P`/`R`/`Z` variants set only their own component, leaving the others as
they were. **The default, for a position no align entry point has been called
on, is `ALIGN_SOFT` for all four components**, and this step does not change
that.

The three modes differ in what each does with the value the caller last
supplied for the component:

* `ALIGN_NONE` (`0`) ignores the road and keeps the supplied value as it is.
* `ALIGN_SOFT` (`1`) follows the road while preserving the object's offset
  from it. For Z that means the object keeps the height above the road surface
  it last held, so an object re-reported at the same horizontal point comes
  back at the same elevation it already had; an object moved somewhere else
  rides up and down with the road, holding that same offset. **An object that
  has never held an elevation of its own — one freshly added and then reported
  without any align call — has a zero offset to preserve, so it reports the
  road surface elevation at its location.** That is the default behaviour a
  newly added object shows, and it is observably the road surface rather than
  the `ALIGN_NONE` result.
* `ALIGN_HARD` (`2`) follows the road exactly and discards the offset: the
  component becomes the road's own value at the object's location.

This requirement covers the **Z** and the **pitch** components; heading and
roll are exported and must work, but are reached only by the export check.

For Z, through `SE_SetAlignModeZ` and through the Z effect of
`SE_SetAlignMode`: an object positioned by a call that supplies an explicit,
deliberately-wrong elevation, and subsequently repositioned in the horizontal
plane only (no elevation supplied), must end up with `ALIGN_HARD` (`2`)
replacing its elevation with the road surface elevation at its new location.
`ALIGN_NONE` (`0`) must not introduce a snap to the road surface that
`ALIGN_HARD` does — the two must be observably different for the same object
and the same horizontal move. `ALIGN_SOFT` (`1`), by the rule above, leaves
such an object's elevation where it was, because the horizontal point and
therefore the road surface under it did not change.

For pitch, through `SE_SetAlignModeP`, which is where the road's shape becomes
visible: an object reported with an explicit pitch of `0` must keep that `0`
under `ALIGN_NONE` and must take the road's own tilt at its location — a value
clearly different from `0` wherever the road's elevation profile slopes — under
`ALIGN_HARD`.

What is read back is the **composed** pitch the engine reports for that object,
not the road frame's slope on its own. Alignment supplies the road's heading and
tilt at the object's position, and the engine then composes them with the
heading the object itself reports, exactly as it composes any other orientation
— so an object whose reported heading differs from the road's reads back the
composition of the two, not the raw slope. Nothing about that machinery is
changed here: implement the alignment so that it feeds the existing orientation
composition, and the value follows. Orientation is read back through
`SE_ScenarioObjectState.p`, and the checker compares angles modulo a full turn,
so normalising to `[0, 2*pi)` and normalising to `(-pi, pi]` are equally
acceptable.

## 6. Restrict which lane types a position may snap onto

```c
SE_DLL_API int SE_SetSnapLaneTypes(int object_id, int laneTypes);
RM_DLL_API int RM_SetSnapLaneTypes(int handle, int laneTypes);
```

`laneTypes` is a bitmask over `roadmanager::Lane::LaneType`, one bit per
OpenDRIVE lane type, in the order the standard lists them:

```
LANE_TYPE_NONE          = 1 << 0     LANE_TYPE_MEDIAN          = 1 << 10
LANE_TYPE_DRIVING       = 1 << 1     LANE_TYPE_SPECIAL1        = 1 << 11
LANE_TYPE_STOP          = 1 << 2     LANE_TYPE_SPECIAL2        = 1 << 12
LANE_TYPE_SHOULDER      = 1 << 3     LANE_TYPE_SPECIAL3        = 1 << 13
LANE_TYPE_BIKING        = 1 << 4     LANE_TYPE_ROADMARKS       = 1 << 14
LANE_TYPE_SIDEWALK      = 1 << 5     LANE_TYPE_TRAM            = 1 << 15
LANE_TYPE_BORDER        = 1 << 6     LANE_TYPE_RAIL            = 1 << 16
LANE_TYPE_RESTRICTED    = 1 << 7     LANE_TYPE_ENTRY           = 1 << 17
LANE_TYPE_PARKING       = 1 << 8     LANE_TYPE_EXIT            = 1 << 18
LANE_TYPE_BIDIRECTIONAL = 1 << 9     LANE_TYPE_OFF_RAMP        = 1 << 19
                                     LANE_TYPE_ON_RAMP         = 1 << 20
                                     LANE_TYPE_CURB            = 1 << 21
                                     LANE_TYPE_CONNECTING_RAMP = 1 << 22
```

with the composites `LANE_TYPE_ANY_DRIVING` (`DRIVING | ENTRY | EXIT |
OFF_RAMP | ON_RAMP`), `LANE_TYPE_ANY_ROAD` (`ANY_DRIVING | RESTRICTED |
STOP`) and `LANE_TYPE_ANY = -1`, meaning every bit. **A position nobody has
called these entry points on starts at `LANE_TYPE_ANY_DRIVING`**, which is the
default this step inherits and does not change.

The mask governs which lane types a subsequent world-coordinate placement is
allowed to resolve onto. Restricting the mask to exclude the lane type a world
point would otherwise land on forces resolution onto the nearest lane the mask
still permits, at a correspondingly non-zero lateral offset from the original
point — the same world coordinate must resolve differently depending only on
the mask in force.

`SE_SetSnapLaneTypes` applies to a scenario object id and returns a negative
value both before `SE_Init` and for an id no scenario defines.
`RM_SetSnapLaneTypes` applies to a road-manager position handle and returns a
negative value for a handle that was never created. On success both return a
non-negative value, and the checker accepts any of them: as with the other
position setters (see the Notes below), it tests the sign, never a particular
code.

## 7. Export the additions with C linkage

Every entry point named above — `SE_AddObjectWithBoundingBox`,
`SE_SetAlignMode`, `SE_SetAlignModeH`, `SE_SetAlignModeP`, `SE_SetAlignModeR`,
`SE_SetAlignModeZ`, `SE_SetSnapLaneTypes`, `SE_GetDistanceToObject` and
`RM_SetSnapLaneTypes` — must be exported under its own unmangled name, from
`libesminiLib.so` or `libesminiRMLib.so` as appropriate. A C++-mangled
spelling does not satisfy this.

## 8. Withdraw the semi-fixed custom camera entry point

The published surface shrinks as well as grows at this release.
`SE_AddCustomSemiFixedCamera`, which `libesminiLib.so` exports in the tree you
were given, is withdrawn: it must no longer appear in the library's dynamic
symbol table, and it must no longer be declared in `esminiLib.hpp`.

Only that one name. `SE_AddCustomCamera` stays exactly as it is, and so do
`SE_WritePPMImage` and `SE_WriteTGAImage` — all three are still part of the
published surface at this release, and removing any of them is wrong.

## Notes

* Road-manager position setters return 1 and 2 as well as 0 to mean success.
  Treat any non-negative return as success; never test for `== 0`.
* Everything the engine already does must keep working unchanged. That is not
  a slogan; these are the specific surfaces the checker re-runs at this step,
  and each of them is required:
  * the road-manager core network queries, lane width and lane type, and the
    position handle — including the fact that `RM_SetWorldXYHPosition` snaps to
    the nearest acceptable lane rather than inverting `RM_SetLanePosition`;
  * object enumeration and `SE_ScenarioObjectState` readback: road id, lane id,
    `s`, speed and the bounding box, before and after the object moves;
  * `SE_RoadInfo`, whose `roadId` and `junctionId` are plain `int`s and whose
    `junctionId` reads `-1` for any point that is not inside a junction;
  * `--record` writing a `.dat` recording at format version `2`, and the C++
    `dat2csv` converting it — keeping its
    `Version: %d, OpenDRIVE: %s, 3DModel: %s` line and its plain
    `time, id, name, ...` header. (The upstream release note about dropping the
    OpenDRIVE and 3D-model references concerns the *Python* script, not this
    binary. Do not apply it here.)
  * `odrplot` rendering a network into `track.csv` in the working directory,
    byte for byte as it does today;
  * `esmini-dyn` still starting and running a scenario through
    `libesminiLib.so`.
* **Process exit status.** A scenario run that completes exits `0`; a run that
  cannot start — a nonexistent `--osc` file, for instance — exits non-zero.
  Restructuring `main` around a `static int execute_scenario(int argc, char*
  argv[])` is how upstream gets there, and any structure that produces the same
  two outcomes is acceptable. Note that an option the binary does not
  recognise is ignored rather than diagnosed, so an unknown option is not a
  failure.
* **Logging does not change at this release.** The text log keeps the
  pre-spdlog `%.3f: %s` format — the simulation time in seconds to three
  decimal places, a colon, a space, then the message — and keeps `log.txt` in
  the working directory as its default destination, selectable with
  `--logfile_path`. Do not introduce `--log_level` or `--log_meta_data`; they
  belong to a later release.

## Scope

Do not switch on OpenSceneGraph, OSI, SUMO, PROJ, ImPlot or the unit-test
targets, and do not add anything that downloads at configure or build time.
The checker builds offline with all of those switched off.

The following changes are part of the upstream v2.31.10 release but are **out
of scope for this step**, and no requirement here covers them. They are listed so that
their absence is not mistaken for an omission:

* **Removing `SE_WritePPMImage` or `SE_WriteTGAImage`.** These are sometimes
  described as withdrawn at this release. They are not: both are still
  exported from `libesminiLib.so` and still declared in `esminiLib.hpp` at
  v2.31.10. Leave them alone. Only `SE_AddCustomSemiFixedCamera` goes (§8).
* **`AccelerationCondition` evaluated against signed acceleration** rather than
  its absolute value, which shifts trigger times in braking scenarios. This
  needs viewer-driven and controller-driven scenarios to observe, and the
  headless profile this step builds under cannot distinguish the two
  behaviours reliably. Out of scope.
* **Parameter distributions applied one value set per run** instead of all
  value sets at once (`--param_dist`, `--param_permutation`). Out of scope for
  the same reason: nothing in this step's fixture set exercises parameter
  distributions at all.

This environment has no network access, and everything the build needs is
already installed. The upstream project's source, its releases, mirrors and
package registries are unreachable, so a copy cannot be fetched by any
route.
