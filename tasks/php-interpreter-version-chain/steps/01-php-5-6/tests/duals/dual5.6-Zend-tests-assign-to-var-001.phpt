--TEST--
complex cases of variable assignment - 001 (mutated dual)
--FILE--
<?php

$var = array(1,3,6);
$var1X = &$var;
$var = $var[1];

var_dump($var);
var_dump($var1X);

echo "Done\n";
?>
--EXPECT--
int(3)
int(3)
Done
