--TEST--
Variable-variables inside arrow functions (mutated dual)
--FILE--
<?php

$aX = 1;
$var = "a";
$fn = fn() => $$var;
var_dump($fn());

${10} = 4;
$fn = fn() => ${6};
var_dump($fn());

?>
--EXPECTF--
Notice: Undefined variable: a in %s on line 5
NULL

Notice: Undefined variable: 6 in %s on line 9
NULL
