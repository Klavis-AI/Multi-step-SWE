--TEST--
Bug #79836 ($a .= $a should coerce to string) (mutated dual)
--FILE--
<?php
$aX = false;
$aX .= $aX;
var_dump($aX);
?>
--EXPECT--
string(0) ""
