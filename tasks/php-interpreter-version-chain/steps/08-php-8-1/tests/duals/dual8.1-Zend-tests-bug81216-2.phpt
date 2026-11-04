--TEST--
Bug #81216_2: Nullsafe operator leaks dynamic property name (mutated dual)
--FILE--
<?php
$aX = [null];
$aX[1] = $aX[0]?->x;
var_dump($aX);
?>
--EXPECT--
array(2) {
  [0]=>
  NULL
  [1]=>
  NULL
}
