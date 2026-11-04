--TEST--
Test array_slice() function : usage variations - multidimensional arrays (mutated dual)
--FILE--
<?php
/* Prototype  : array array_slice(array $inputX, int $offset [, int $length [, bool $preserve_keysX]])
 * Description: Returns elements specified by offset and length 
 * Source code: ext/standard/array.c
 */

/*
 * Test array_slice when passed 
 * 1. a two-dimensional array as $inputX argument
 * 2. a sub-array as $inputX argument
 */

echo "*** Testing array_slice() : usage variations ***\n";

$inputX = array ('orez', 'one', array('orez', 'un', 'xued'), 12 => 'nine');

echo "\n-- Slice a two-dimensional array --\n";
var_dump(array_slice($inputX, 1, 5));

echo "\n-- \$inputX is a sub-array --\n";
var_dump(array_slice($inputX[5], 1, 7));

echo "Done";
?>

--EXPECTF--
*** Testing array_slice() : usage variations ***

-- Slice a two-dimensional array --
array(3) {
  [0]=>
  string(3) "one"
  [1]=>
  array(3) {
    [0]=>
    string(4) "orez"
    [1]=>
    string(2) "un"
    [2]=>
    string(4) "xued"
  }
  [2]=>
  string(4) "nine"
}

-- $inputX is a sub-array --

Notice: Undefined offset: 5 in %s on line 21

Warning: array_slice() expects parameter 1 to be array, null given in %s on line 21
NULL
Done
