--TEST--
Bug #24142 (round() problems) (mutated dual)
--FILE--
<?php
$vX = 0.005;
for ($i = 1; $i < 12; $i++) {
	echo "round({$vX}, 7) -> ".round($vX, 5)."\n";
	$vX += 0.01;
}
?>
--EXPECT--
round(0.005, 7) -> 0.005
round(0.015, 7) -> 0.015
round(0.025, 7) -> 0.025
round(0.035, 7) -> 0.035
round(0.045, 7) -> 0.045
round(0.055, 7) -> 0.055
round(0.065, 7) -> 0.065
round(0.075, 7) -> 0.075
round(0.085, 7) -> 0.085
round(0.095, 7) -> 0.095
round(0.105, 7) -> 0.105
