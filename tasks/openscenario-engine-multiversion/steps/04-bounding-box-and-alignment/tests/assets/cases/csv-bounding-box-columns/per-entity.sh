#!/usr/bin/env bash
# The box columns are the entity's own box, so a scenario whose two vehicles
# have different shapes produces different numbers in the two entity blocks.
#
# A logger that emits one constant box for everything cannot satisfy this.
source /tests/caselib.sh

cat > "$WORK/shapes.xosc" << 'XOSC'
<?xml version="1.0" encoding="UTF-8"?>
<OpenSCENARIO>
    <FileHeader revMajor="1" revMinor="0" date="2026-01-01T00:00:00" description="box shapes" author="tests"/>
    <ParameterDeclarations/>
    <CatalogLocations/>
    <RoadNetwork><LogicFile filepath="straight_500m.xodr"/></RoadNetwork>
    <Entities>
        <ScenarioObject name="Short">
            <Vehicle name="s" vehicleCategory="car">
                <ParameterDeclarations/>
                <Performance maxSpeed="60" maxAcceleration="5" maxDeceleration="5"/>
                <BoundingBox><Center x="1.0" y="0.0" z="0.5"/><Dimensions width="1.6" length="3.6" height="1.2"/></BoundingBox>
                <Axles>
                    <FrontAxle maxSteering="0.5" wheelDiameter="0.6" trackWidth="1.6" positionX="1.4" positionZ="0.3"/>
                    <RearAxle maxSteering="0.0" wheelDiameter="0.6" trackWidth="1.6" positionX="0.0" positionZ="0.3"/>
                </Axles>
                <Properties/>
            </Vehicle>
        </ScenarioObject>
        <ScenarioObject name="Long">
            <Vehicle name="l" vehicleCategory="truck">
                <ParameterDeclarations/>
                <Performance maxSpeed="40" maxAcceleration="3" maxDeceleration="3"/>
                <BoundingBox><Center x="3.5" y="0.0" z="1.9"/><Dimensions width="2.5" length="9.0" height="3.8"/></BoundingBox>
                <Axles>
                    <FrontAxle maxSteering="0.5" wheelDiameter="1.0" trackWidth="2.0" positionX="3.0" positionZ="0.5"/>
                    <RearAxle maxSteering="0.0" wheelDiameter="1.0" trackWidth="2.0" positionX="0.0" positionZ="0.5"/>
                </Axles>
                <Properties/>
            </Vehicle>
        </ScenarioObject>
    </Entities>
    <Storyboard>
        <Init>
            <Actions>
                <Private entityRef="Short">
                    <PrivateAction>
                        <TeleportAction>
                            <Position><LanePosition roadId="1" laneId="-1" offset="0" s="40"/></Position>
                        </TeleportAction>
                    </PrivateAction>
                </Private>
                <Private entityRef="Long">
                    <PrivateAction>
                        <TeleportAction>
                            <Position><LanePosition roadId="1" laneId="-1" offset="0" s="120"/></Position>
                        </TeleportAction>
                    </PrivateAction>
                </Private>
            </Actions>
        </Init>
        <Story name="s">
            <Act name="a">
                <ManeuverGroup maximumExecutionCount="1" name="g">
                    <Actors selectTriggeringEntities="false">
                        <EntityRef entityRef="Short"/>
                    </Actors>
                    <Maneuver name="m">
                        <Event name="e" priority="overwrite">
                            <Action>
                                <PrivateAction>
                                    <LongitudinalAction>
                                        <SpeedAction>
                                            <SpeedActionDynamics dynamicsShape="step" value="0" dynamicsDimension="time"/>
                                            <SpeedActionTarget><AbsoluteTargetSpeed value="10"/></SpeedActionTarget>
                                        </SpeedAction>
                                    </LongitudinalAction>
                                </PrivateAction>
                            </Action>
                        </Event>
                    </Maneuver>
                </ManeuverGroup>
            </Act>
        </Story>
        <StopTrigger>
            <ConditionGroup>
                <Condition name="StoryboardStop" delay="0" conditionEdge="none">
                    <ByValueCondition><SimulationTimeCondition value="1" rule="greaterThan"/></ByValueCondition>
                </Condition>
            </ConditionGroup>
        </StopTrigger>
    </Storyboard>
</OpenSCENARIO>
XOSC

cp "$FIXTURES/straight_500m.xodr" "$WORK/"

run_esmini --osc "$WORK/shapes.xosc" --headless --fixed_timestep 0.05 \
           --csv_logger "$WORK/shapes.csv"
need_status 0 $? "esmini on the two-shape scenario"
need_file "$WORK/shapes.csv"

hdr=$(grep -m1 '^Index \[-\] , TimeStamp' "$WORK/shapes.csv")
[ -n "$hdr" ] || fail "no CSV header line"

col() { printf '%s' "$hdr" | tr ',' '\n' | grep -n "^ #$1 $2 \$" | cut -d: -f1; }

c1_len=$(col 1 'bb_length \[m\]'); c1_wid=$(col 1 'bb_width \[m\]'); c1_hei=$(col 1 'bb_height \[m\]')
c2_len=$(col 2 'bb_length \[m\]'); c2_wid=$(col 2 'bb_width \[m\]'); c2_hei=$(col 2 'bb_height \[m\]')
for c in "$c1_len" "$c1_wid" "$c1_hei" "$c2_len" "$c2_wid" "$c2_hei"; do
    [ -n "$c" ] || fail "a bb_length/bb_width/bb_height column is missing from the header"
done

# Both entities live in the SAME row, one column block per entity -- this is
# a wide CSV, not one row per entity. Which block a given entity landed in is
# not something this case should assume, so find each entity by its name field
# and let the ORDER of those two name fields say which block is #1 and which
# is #2. The box values then come from the column indices the header itself
# gave us above, never from a hardcoded offset into the block layout.
row=$(grep -m1 '^0, 0\.000000,' "$WORK/shapes.csv")
[ -n "$row" ] || fail "no first data row found"

name_idx() { printf '%s' "$row" | tr ',' '\n' | grep -n "^ $1\$" | cut -d: -f1; }
val_at()   { printf '%s' "$row" | tr ',' '\n' | sed -n "${1}p" | tr -d ' '; }

i_short=$(name_idx Short)
i_long=$(name_idx Long)
[ -n "$i_short" ] || fail "no field named Short in the data row"
[ -n "$i_long" ]  || fail "no field named Long in the data row"
[ "$i_short" != "$i_long" ] || fail "both entity names resolved to the same field"

if [ "$i_short" -lt "$i_long" ]; then
    s_len=$c1_len; s_wid=$c1_wid; s_hei=$c1_hei
    l_len=$c2_len; l_wid=$c2_wid; l_hei=$c2_hei
else
    s_len=$c2_len; s_wid=$c2_wid; s_hei=$c2_hei
    l_len=$c1_len; l_wid=$c1_wid; l_hei=$c1_hei
fi

v1_len=$(val_at "$s_len");  v1_wid=$(val_at "$s_wid");  v1_hei=$(val_at "$s_hei")
v2_len=$(val_at "$l_len");  v2_wid=$(val_at "$l_wid");  v2_hei=$(val_at "$l_hei")
for v in "$v1_len" "$v1_wid" "$v1_hei" "$v2_len" "$v2_wid" "$v2_hei"; do
    [ -n "$v" ] || fail "a box column named in the header carries no value in the data row"
done

[ "$v1_len" != "$v2_len" ] || fail "both entities report the same length ($v1_len)"
[ "$v1_wid" != "$v2_wid" ] || fail "both entities report the same width ($v1_wid)"
[ "$v1_hei" != "$v2_hei" ] || fail "both entities report the same height ($v1_hei)"

awk -v a="$v1_len" -v b="3.6" 'BEGIN{d=a-b; if(d<0)d=-d; exit d<=0.001?0:1}' || fail "Short length is $v1_len, not 3.6"
awk -v a="$v2_len" -v b="9.0" 'BEGIN{d=a-b; if(d<0)d=-d; exit d<=0.001?0:1}' || fail "Long length is $v2_len, not 9.0"
awk -v a="$v1_wid" -v b="1.6" 'BEGIN{d=a-b; if(d<0)d=-d; exit d<=0.001?0:1}' || fail "Short width is $v1_wid, not 1.6"
awk -v a="$v2_wid" -v b="2.5" 'BEGIN{d=a-b; if(d<0)d=-d; exit d<=0.001?0:1}' || fail "Long width is $v2_wid, not 2.5"
awk -v a="$v1_hei" -v b="1.2" 'BEGIN{d=a-b; if(d<0)d=-d; exit d<=0.001?0:1}' || fail "Short height is $v1_hei, not 1.2"
awk -v a="$v2_hei" -v b="3.8" 'BEGIN{d=a-b; if(d<0)d=-d; exit d<=0.001?0:1}' || fail "Long height is $v2_hei, not 3.8"
