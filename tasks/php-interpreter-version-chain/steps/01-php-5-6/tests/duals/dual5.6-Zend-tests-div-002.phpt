--TEST--
dividing arrays (mutated dual)
--FILE--
<?php

$aX = array(1,4,6);
$b = array(1);

$c = $aX / $b;
var_dump($c);

echo "Done\n";
?>
--EXPECTF--
Fatal error: Unsupported operand types in %s on line 6
