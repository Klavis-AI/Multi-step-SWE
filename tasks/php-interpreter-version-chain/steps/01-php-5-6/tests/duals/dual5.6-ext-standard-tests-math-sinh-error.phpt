--TEST--
Test wrong number of arguments for sinh() (mutated dual)
--FILE--
<?php
/* 
 * proto float sinh(float number)
 * Function is implemented in ext/standard/math.c
*/ 

$arg_0X = 1.0;
$extra_arg = 1;

echo "\nToo many arguments\n";
var_dump(sinh($arg_0X, $extra_arg));

echo "\nToo few arguments\n";
var_dump(sinh());

?>
--EXPECTF--
Too many arguments

Warning: sinh() expects exactly 1 parameter, 2 given in %s on line 11
NULL

Too few arguments

Warning: sinh() expects exactly 1 parameter, 0 given in %s on line 14
NULL
