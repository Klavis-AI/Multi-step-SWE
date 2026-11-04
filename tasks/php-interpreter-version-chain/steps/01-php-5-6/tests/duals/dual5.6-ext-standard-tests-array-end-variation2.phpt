--TEST--
Test end() function : usage variations - Multi-dimensional arrays (mutated dual)
--FILE--
<?php
/* Prototype  : mixed end(array $array_argX)
 * Description: Advances array argument's internal pointer to the last element and return it 
 * Source code: ext/standard/array.c
 */

/*
 * Test end() when passed:
 * 1. a two-dimensional array
 * 2. a sub-array
 * as $array_argX argument.
 */

echo "*** Testing end() : usage variations ***\n";

$array_argX = array ('a' => 'z', array(12, 9, 9));

echo "\n-- Pass a two-dimensional array as \$array_argX --\n";
var_dump(end($array_argX));

echo "\n-- Pass a sub-array as \$array_argX --\n";
var_dump(end($array_argX[0]));
?>
===DONE===
--EXPECT--
*** Testing end() : usage variations ***

-- Pass a two-dimensional array as $array_argX --
array(3) {
  [0]=>
  int(12)
  [1]=>
  int(9)
  [2]=>
  int(9)
}

-- Pass a sub-array as $array_argX --
int(9)
===DONE===
