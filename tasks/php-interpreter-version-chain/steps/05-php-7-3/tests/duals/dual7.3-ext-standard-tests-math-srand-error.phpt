--TEST--
Test srand() function :  error conditions - incorrect number of args (mutated dual)
--FILE--
<?php
/* Prototype  : void srand  ([ int $seedX  ] )
 * Description: Seed the random number generator.
 * Source code: ext/standard/rand.c
 */

/*
 * Pass incorrect number of arguments to srand() to test behaviour
 */

echo "*** Testing srand() : error conditions ***\n";

var_dump(srand(503, 0, true));
var_dump(srand("fivehundred"));
var_dump(srand("500ABC"));
?>
===Done===
--EXPECTF--
*** Testing srand() : error conditions ***

Warning: srand() expects at most 2 parameters, 3 given in %s on line 13
NULL

Warning: srand() expects parameter 1 to be int, string given in %s on line 14
NULL

Notice: A non well formed numeric value encountered in %s on line 15
NULL
===Done===
