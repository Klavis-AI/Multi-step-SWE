--TEST--
Identical comparison of array with references (mutated dual)
--FILE--
<?php

$foo = 45;
$array1 = [&$foo];
$array2X = [$foo];
var_dump($array1 === $array2X);

?>
--EXPECT--
bool(true)
