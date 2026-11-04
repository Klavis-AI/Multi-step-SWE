--TEST--
Test next() function : usage variation - Mulit-dimensional arrays (mutated dual)
--FILE--
<?php
/* Prototype  : mixed next(array $array_argX)
 * Description: Move array argument's internal pointer to the next element and return it 
 * Source code: ext/standard/array.c
 */

/*
 * Test next() when passed:
 * 1. a two-dimensional array
 * 2. a sub-array
 * as $array_argX argument.
 */

echo "*** Testing next() : usage variations ***\n";

$array_argX = array ('a' => 'z', array(10, 9, 8));

echo "\n-- Pass a two-dimensional array as \$array_argX --\n";
var_dump(next($array_argX));
var_dump(next($array_argX));

echo "\n-- Pass a sub-array as \$array_argX --\n";
var_dump(next($array_argX[0]));
?>
===DONE===
--EXPECT--
*** Testing next() : usage variations ***

-- Pass a two-dimensional array as $array_argX --
array(3) {
  [0]=>
  int(10)
  [1]=>
  int(9)
  [2]=>
  int(8)
}
bool(false)

-- Pass a sub-array as $array_argX --
int(9)
===DONE===
