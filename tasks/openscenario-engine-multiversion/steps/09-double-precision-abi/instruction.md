# Widen the public numeric ABI from float to double

The engine's two public C APIs — the scenario-engine library (`esminiLib.hpp`)
and the road-manager library (`esminiRMLib.hpp`) — currently carry 32-bit
`float` across most of their numeric surface: struct members, function
parameters and return values. Single precision is not enough for the
coordinates these APIs carry. A position expressed in a projected coordinate
system is routinely a six- or seven-digit number of metres, and a `float` holds
only about seven significant decimal digits in total, so the centimetre and
millimetre digits of such a coordinate are already gone by the time a caller
reads them back. The same applies to an accumulating simulation clock and to an
odometer.

Widen the public numeric surface of both libraries to 64-bit `double`, and
widen the implementation behind it so the extra precision is real rather than a
wider container for a value that was already rounded.

## Requirements

1. **The simulation clock and its getters are double.**
   `SE_GetSimulationTime()` and `SE_GetSimTimeStep()` return `double`. The
   clock must accumulate in double: stepping a scenario 3600 times by 0.001 s
   must leave the reported time within 1e-12 of 3.6 s, not within the ~4e-7
   that a float accumulator drifts to. Reading the time twice without stepping
   must return the same value both times.

2. **`SE_ScenarioObjectState` and `SE_RoadInfo` carry double members.**
   At minimum `SE_ScenarioObjectState::x`, `::y`, `::h`, `::s`, `::speed`,
   `::timestamp` and the bounding-box dimensions `::length`, `::width` and
   `::height`, and `SE_RoadInfo::global_pos_x`, `::s`, `::curvature` and
   `::speed_limit`, are `double`. `SE_StepDT` takes a
   `double` timestep; `SE_GetRoadInfoAtDistance` takes a `double` lookahead,
   and so does the route-aware `SE_GetRoadInfoAlongRoute` that fills the same
   struct.
   A pose written in and read back out must survive to within 1e-12. `::timestamp`
   is the simulation time at which the state was reported, so after a single
   step of `0.05` s it reads `0.05` to the full precision of the type — it is
   the widened clock of requirement 1, read back through the state struct.

   Those lists are a **floor**, not the extent of the widening. The step's
   headline is that the public numeric surface becomes 64-bit throughout, so
   members of the same structs that are not named above — `RM_PositionData::h`,
   `::x` and `::y`, `RM_RoadProbeInfo::relative_pos` and the rest of those
   structures' scalar members — are `double` as well, and an s, a pose or a
   lookahead read back through any of them is held to the same `1e-12`.

3. **The motion-state surface is double in both directions.**
   `SE_GetObjectOdometer`, `SE_GetObjectAcceleration` and
   `SE_GetRouteTotalLength` return `double`.
   `SE_GetObjectVelocityGlobalXYZ`, `SE_GetObjectAngularVelocity` and
   `SE_GetObjectAccelerationGlobalXYZ` take three `double*` out-parameters;
   `SE_GetObjectAccelerationLocalLatLong` takes two.
   `SE_ReportObjectVel`, `SE_ReportObjectAngularVel` and `SE_ReportObjectAcc`
   take `(int, double, double, double)`;
   `SE_ReportObjectLateralPosition` takes `(int, double)`.
   The reporters keep returning a negative code for an object id that does not
   exist, rather than writing through an unset out-parameter. The negative code
   is `-1`, and `-1` is likewise what any report entry point returns when it is
   called before a scenario has been loaded. Two of the getters above answer
   the same way in that situation: called before a scenario has been loaded,
   `SE_GetObjectVelocityGlobalXYZ` and `SE_GetObjectAngularVelocity` each
   return exactly `-1`. Here the magnitude is part of the contract and not
   only the sign, in the sense `TEST_INTERFACE.md` §3 gives to a named
   literal.

4. **The remaining report entry points take double.**
   `SE_ReportObjectPos` takes `(int, double, double, double, double, double,
   double)`; `SE_ReportObjectPosXYH` takes `(int, double, double, double)`;
   `SE_ReportObjectPosMode` takes six `double` coordinates followed by an `int`
   mode; `SE_ReportObjectSpeed` takes `(int, double)`;
   `SE_ReportObjectWheelStatus` takes `(int, double, double)`;
   `SE_ReportObjectRoadPos` takes `(int, id_t, int, double, double)`.
   These follow the same error convention as requirement 3: `-1` for an object
   id that does not exist, and `-1` when called before a scenario is loaded.

5. **The road manager's scalar getters return double.**
   `RM_GetRoadLength` and `RM_GetSpeedLimit` return `double`.
   `RM_GetLaneWidth` takes an `int, int, double*`; `RM_GetLaneWidthByRoadId`
   takes `(id_t, int, double, double*)`.

6. **The road manager's position, lane and probe structures are double.**
   At minimum `RM_PositionXYZ::x`, `RM_PositionData::s`,
   `RM_PositionData::laneOffset`, `RM_RoadLaneInfo::s`, `RM_RoadLaneInfo::width`,
   `RM_RoadLaneInfo::curvature`, `RM_RoadProbeInfo::relative_h` and
   `RM_PositionDiff::ds` are `double`.
   `RM_GetProbeInfo` reports its point in the position handle's **own local
   frame** — rotated by the heading that handle currently holds — so the
   `x` of its relative position is the lookahead foreshortened by the cosine of
   the angle between that heading and the probe direction, not the lookahead
   distance itself.
   `RM_GetLaneInfo` and `RM_GetProbeInfo` take a `double` lookahead;
   `RM_SetS`, `RM_SetH` and `RM_SetHMode` take a `double`;
   `RM_SetRoadPosition` takes `(int, id_t, double, double, bool)`;
   `RM_SetWorldPositionMode` takes six `double` coordinates and an `int` mode;
   `RM_PositionMoveForward` takes `(int, double, double)`;
   `RM_GetRoadNumberOfLanes` takes `(id_t, double, int)` and
   `RM_GetRoadNumberOfDrivableLanes` takes `(id_t, double)`.
   An s value set through the API and read back must agree to within 1e-12, and
   so must a lane-info or probe-info lookup taken at a fractional lookahead.

7. **The widened record layout carries a new recording format version.**
   The `.dat` recording stores object state, so widening that state changes the
   record layout on disk. Stamp the recording with major version 5 and minor
   version 1, and make `dat2csv` refuse a recording whose major version is
   older rather than decoding it as if it were current. Refusing means all
   three of the following. The message it prints contains the words
   `Incompatible DAT major file version` and names the version it found
   immediately after them. The process exits with a non-zero status. And the
   `<basename>.csv` it derives from the input path is **created and left
   empty**: the file is there, and there is nothing in it — no rows, not even a
   column header. Both halves are required. Writing no file at all does not
   satisfy this, and neither does writing a header-only one.
   The decoded output of a current recording is otherwise
   unchanged — same preamble fields, same sample rate, and the same column
   header, which is exactly
   `time, id, name, x, y, z, h, p, r, speed, wheel_angle, wheel_rot`. The
   widening changes what those columns are computed from, not how many of them
   there are, what they are called or how many decimals each is printed to.

8. **The simple vehicle model is double on both sides.**
   `SE_SimpleVehicleCreate` takes five `double` arguments;
   `SE_SimpleVehicleSetSpeed`, `SE_SimpleVehicleSetMaxSpeed`,
   `SE_SimpleVehicleSetMaxAcceleration` and
   `SE_SimpleVehicleSetMaxDeceleration` each take a `double`; and every member
   of `SE_SimpleVehicleState` — `x`, `y`, `z`, `h`, `p`, `speed`,
   `wheel_rotation` and `wheel_angle` — is `double`. A vehicle created at a
   coordinate needing more than 24 mantissa bits reports that coordinate back
   through `SE_SimpleVehicleGetState` to within 1e-12 before anything has been
   integrated.

9. **The float-era duplicates leave the exported surface.**
   `SE_GetSimulationTimeDouble` exists only because `SE_GetSimulationTime`
   returned `float`. Once requirement 1 is done the two are the same function,
   and this step widens entry points rather than accumulating them: it must no
   longer be exported from `libesminiLib.so`. The deprecated
   `SE_UpdateOSITrafficCommand` goes at the same time — this build has
   `USE_OSI` off and the entry point is a stub with no caller. Every other
   symbol named in this instruction stays exported, under the name it has now.

## Notes

- Both headers are `extern "C"`. Keep them so: every symbol above stays
  C-callable under the name it has now. This is a widening of existing entry
  points, not a parallel set of new ones — do not keep float-typed duplicates
  alongside them.

- The road-manager position setters signal success with a **non-negative**
  return code: 0, 1 and 2 all mean the position was set, with the non-zero
  codes carrying extra information about how it was resolved. Do not narrow
  that to `== 0`.

  *Which* non-negative code carries which meaning is part of what does not
  change here, so the two a checked run reaches are named rather than left to
  the sign alone. **0** means the position was set on the road and lane that
  were asked for — `RM_SetLanePosition` on a road and lane that exist answers
  0. **1** means the position moved into a new road segment — following a lane
  position forward off the end of its own road with
  `RM_PositionMoveForward` answers 1, not 0. (2, a junction choice, and 3, the
  end of a lane, are the other non-negative codes; a checked run does not reach
  them.) Keep both answers as they are: collapsing 1 onto 0, or answering 1
  where the position simply stayed where it was put, is the narrowing this
  bullet warns against.

- The road-manager's *query* entry points that fill an out-parameter use the
  stricter convention they already use, and it does not change either:
  `RM_GetLaneWidth`, `RM_GetLaneWidthByRoadId` and `RM_SubtractAFromB` answer
  exactly `0` on success and a negative code on any error. A zero-width centre
  lane is a success with a width of 0, not an error.

- Apart from the recording format version in requirement 7, everything the
  engine already does must keep working unchanged: the CSV logger, `odrplot`'s
  output, and loading a scenario named as a bare positional argument on the
  command line. The published set of artifacts is unchanged. "Unchanged"
  reaches down to how those text outputs are printed: every real-valued field
  the CSV logger writes is printed to six decimals — `0.000000`,
  `20.000000` — and `odrplot` echoes its sampling step to two, as in
  `Created track.csv using stepsize 1.00`. Those field widths are
  presentation, not precision; widening the numeric ABI does not widen any of
  them.

- Unchanged behaviour includes the tools' existing error handling. In
  particular `odrplot` exits with a non-zero status on an input that is not an
  OpenDRIVE network, rather than writing a plot from it.

- Unchanged behaviour also includes the engine's log output, which this step
  must not reformat even though every number it prints is now carried in a
  wider type. A log line carries a bracketed simulation time, then a bracketed
  level, then the message: `[<seconds to three decimals>] [<level>] <message>`,
  for example `[10.050] [info] Closing`. Lines emitted before the clock starts
  keep the brackets and leave the time empty (`[] [warn] ...`); the three
  decimals are not to be widened along with the numeric ABI. `esmini` also
  writes this log to a file named `log.txt` in the working directory by
  default, in addition to standard output.

## Scope

This environment has no network access, and everything the build needs is
already installed. The upstream project's source, its releases, mirrors and
package registries are unreachable, so a copy cannot be fetched by any
route.
