--TEST--
complex cases of variable assignment - 002 (mutated dual)
--FILE--
<?php

$var = "intergalactic";
$var1X = &$var;
$var = $var[6];

var_dump($var);
var_dump($var1X);

echo "Done\n";
?>
--EXPECT--
string(1) "a"
string(1) "a"
Done
