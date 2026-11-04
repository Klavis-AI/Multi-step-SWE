# Workspace handoff — step 04

## What you receive

You begin with a working esmini tree at v2.27.5 that already builds the six
published artifacts, runs OpenSCENARIO player scenarios headless, records
playback to `.dat` files, parses those recordings through `dat2csv`, renders
road networks through `odrplot`, and runs a scenario through the shared
library rather than the static player with `esmini-dyn`.

The road-manager library already publishes its core network queries — number
of roads, road ids, road length, number of lanes at a station, lane width and
lane type by road id and by position handle — and the scenario-engine library
already publishes object enumeration and state readback: the number of
objects a running scenario holds, their names, the id an entity's name maps
to, and per-object state including road id, lane id, longitudinal position,
speed, and the object's bounding-box centre offset and dimensions, all through
`SE_ScenarioObjectState`.

`STEP_04_INTERFACE.md`, alongside this file, names every one of those existing
entry points and their arguments, so that nothing `instruction.md` refers to
has to be guessed at. Nothing in that document is a task.

Your tree builds offline with CMake options that disable OpenSceneGraph, OSI,
SUMO, PROJ, ImPlot, and the unit-test targets. Nothing downloads at configure
or build time. Read `PROFILE.md` for the exact shape.

The fixture corpus your build is exercised against is verifier-owned and lives
outside `/app`. It is not in your workspace and you never see the files, but
knowing what is in them tells you what your implementation has to cope with.
All of them already run at v2.27.5:

- A two-entity longitudinal brake scenario and the straight road network it
  references, the same pairing earlier steps use, self-contained and
  catalog-free.
- A pair of single-entity scenarios, each paired with its own road network:
  one where the road climbs and crests along the entity's path, and one with
  a longer, more varied elevation profile. Neither scenario moves its own
  entity; both exist so a driver can add and reposition its own externally
  reported object against real, non-flat road geometry.
- A two-entity scenario with the vehicles placed in a pair of lanes that
  carry traffic in opposite directions, each entity given its own distinct
  bounding box, both stationary — built for measuring the distance between
  two objects rather than for playing out a manoeuvre.
- A two-entity scenario on a road network made of two **disconnected**
  components — two roads with no link between them, far apart in the plane,
  one entity parked on each. Nothing routes from one to the other, so it is
  the fixture that exercises what the distance query does when there is no
  path to measure along.
- A larger road network with a junction, and a longer multi-lane road, both
  carried over from earlier steps for lane-geometry regression coverage.

## What you build

You grow the published C ABI so that entities carry a real bounding box that
is visible outside the engine in three ways: the `--csv_logger` recording
gains columns for each entity's box and its lane identity, alongside the box
already carried by state readback; the scenario-engine library gains an entry
point to add an object at runtime with an explicit, caller-supplied box
(distinct from the existing model-implied box) and an entry point to remove
an added object again; and the library gains an entry point that measures the
longitudinal and lateral distance, and the lane-id delta, between any two
objects, either reference-point to reference-point or between the nearer
edges of their bounding boxes.

You also grow road-placement control: per-component alignment to the road
surface — heading, pitch, roll and elevation can each be set independently to
ignore the road, to blend with it, or to follow it exactly — and a mask that
restricts which lane types a position, whether a scenario object or a bare
road-manager handle, is allowed to resolve a world coordinate onto.

Every new entry point is exported from its library under its own unmangled
name, so it is callable from C as well as from C++.

## What carries forward

The six published artifacts are byte-identical in behaviour to what you
received at every path the earlier steps established. The road-manager core
network queries, lane width and lane type lookups, scenario object
enumeration and state readback, and running a scenario headless to its own
stop trigger all keep working unchanged — the checker re-runs these at this
step to confirm nothing regressed while the bounding-box and alignment work
landed.

Your tree is at v2.31.10 once this step is done. The build profile is
unchanged: still offline, still without OpenSceneGraph/OSI/SUMO/PROJ/ImPlot/
unit-tests.
