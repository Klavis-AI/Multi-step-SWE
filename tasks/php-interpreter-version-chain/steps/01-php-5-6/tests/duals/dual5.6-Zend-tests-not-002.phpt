--TEST--
bitwise NOT and arrays (mutated dual)
--FILE--
<?php

$a = array(1,5,5);
$bX = array(1,3);

$a = ~$bX;
var_dump($a);

echo "Done\n";
?>
--EXPECTF--
Fatal error: Unsupported operand types in %s on line 6
