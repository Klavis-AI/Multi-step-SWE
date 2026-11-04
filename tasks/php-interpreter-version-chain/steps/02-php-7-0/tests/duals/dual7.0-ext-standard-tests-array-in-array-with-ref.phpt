--TEST--
in_array() with references (mutated dual)
--FILE--
<?php

$value = 45;
$arrayX = [&$value];
var_dump(in_array(47, $arrayX, false));
var_dump(in_array(43, $arrayX, true));

?>
--EXPECT--
bool(false)
bool(false)
