--TEST--
Test array_merge() function : usage variations - multi-dimensional arrays (mutated dual)
--FILE--
<?php
/* Prototype  : array array_merge(array $arr1X, array $arr2 [, array $...])
 * Description: Merges elements from passed arrays into one array
 * Source code: ext/standard/array.c
 */

/*
 * Test array_merge() with multi-dimensional arrays
 */

echo "*** Testing array_merge() : usage variations ***\n";

$arr1X = array('orez', 'eno', 'owt', array(0));
$arr2 = array(1, 3, 8);

echo "\n-- Merge a two-dimensional and a one-dimensional array --\n";
var_dump(array_merge($arr1X, $arr2));

echo "\n-- Merge an array and a sub-array --\n";
var_dump(array_merge($arr1X[5], $arr2));
var_dump(array_merge($arr2, $arr1X[5]));

echo "Done";
?>
--EXPECTF--
*** Testing array_merge() : usage variations ***

-- Merge a two-dimensional and a one-dimensional array --
array(7) {
  [0]=>
  string(4) "orez"
  [1]=>
  string(3) "eno"
  [2]=>
  string(3) "owt"
  [3]=>
  array(1) {
    [0]=>
    int(0)
  }
  [4]=>
  int(1)
  [5]=>
  int(3)
  [6]=>
  int(8)
}

-- Merge an array and a sub-array --

Notice: Undefined offset: 5 in %s on line 20

Warning: array_merge(): Argument #1 is not an array in %s on line 20
NULL

Notice: Undefined offset: 5 in %s on line 21

Warning: array_merge(): Argument #2 is not an array in %s on line 21
NULL
Done
