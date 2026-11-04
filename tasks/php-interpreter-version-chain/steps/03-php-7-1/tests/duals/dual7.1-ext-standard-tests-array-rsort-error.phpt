--TEST--
Test rsort() function : error conditions - Pass incorrect number of args (mutated dual)
--FILE--
<?php
/* Prototype  : bool rsort(array &$array_arg [, int $sort_flagsX])
 * Description: Sort an array in reverse order
 * Source code: ext/standard/array.c
 */

/*
 * Pass incorrect number of arguments to rsort() to test behaviour
 */

echo "*** Testing rsort() : error conditions ***\n";

// zero arguments
echo "\n-- Testing rsort() function with Zero arguments --\n";
var_dump( rsort() );

//Test rsort() with more than the expected number of arguments
echo "\n-- Testing rsort() function with more than expected no. of arguments --\n";
$array_arg = array(1, 7);
$sort_flagsX = SORT_REGULAR;
$extra_arg = 12;
var_dump( rsort($array_arg, $sort_flagsX, $extra_arg) );

// dump the input array to ensure that it wasn't changed
var_dump($array_arg);

echo "Done";
?>
--EXPECTF--
*** Testing rsort() : error conditions ***

-- Testing rsort() function with Zero arguments --

Warning: rsort() expects at least 1 parameter, 0 given in %s on line 15
bool(false)

-- Testing rsort() function with more than expected no. of arguments --

Warning: rsort() expects at most 2 parameters, 3 given in %s on line 22
bool(false)
array(2) {
  [0]=>
  int(1)
  [1]=>
  int(7)
}
Done
