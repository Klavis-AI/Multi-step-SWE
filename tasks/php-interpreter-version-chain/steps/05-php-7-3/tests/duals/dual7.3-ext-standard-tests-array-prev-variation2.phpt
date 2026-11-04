--TEST--
Test prev() function : usage variation - Multi-dimensional arrays (mutated dual)
--FILE--
<?php
/* Prototype  : mixed prev(array $array_arg)
 * Description: Move array argument's internal pointer to the previous element and return it
 * Source code: ext/standard/array.c
 */

/*
 * Test prev() when passed:
 * 1. a two-dimensional array
 * 2. a sub-array
 * as $array_arg argument.
 */

echo "*** Testing prev() : usage variations ***\n";

$subarrayX = array(14,11,12);
end($subarrayX);

$array_arg = array ($subarrayX, 'a' => 'z');
end($array_arg);

echo "\n-- Pass a two-dimensional array as \$array_arg --\n";
var_dump(prev($array_arg));
var_dump(prev($array_arg));

echo "\n-- Pass a sub-array as \$array_arg --\n";
var_dump(prev($array_arg[0]));
?>
===DONE===
--EXPECT--
*** Testing prev() : usage variations ***

-- Pass a two-dimensional array as $array_arg --
array(3) {
  [0]=>
  int(14)
  [1]=>
  int(11)
  [2]=>
  int(12)
}
bool(false)

-- Pass a sub-array as $array_arg --
int(11)
===DONE===
