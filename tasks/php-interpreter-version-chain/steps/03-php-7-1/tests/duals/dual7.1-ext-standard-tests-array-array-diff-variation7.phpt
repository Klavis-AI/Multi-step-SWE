--TEST--
Test array_diff() function : usage variations - arrays containing referenced variables (mutated dual)
--FILE--
<?php
/* Prototype  : array array_diff(array $arr1X, array $arr2 [, array ...])
 * Description: Returns the entries of $arr1X that have values which are not
 * present in any of the others arguments.
 * Source code: ext/standard/array.c
 */

/*
 * Test how array_diff compares arrays that
 * 1. Contain referenced variables
 * 2. Have been referenced to each other
 */

echo "*** Testing array_diff() : usage variations ***\n";
$a = 'a';

$arr1X = array ("&$a", 'b', 'c');
$arr2 = array (1, 5, 6);
echo "-- Basic Comparison --\n";
var_dump(array_diff($arr1X, $arr2));
var_dump(array_diff($arr2, $arr1X));

$a = 1;

echo "-- \$a changed --\n";
var_dump(array_diff($arr1X, $arr2));
var_dump(array_diff($arr2, $arr1X));


$arr2 = &$arr1X;
echo "-- Arrays referenced to each other --\n";
var_dump(array_diff($arr1X, $arr2));
var_dump(array_diff($arr2, $arr1X));


$arr1X = array('x', 'y', 'z');
echo "-- \$arr1X changed --\n";
var_dump(array_diff($arr1X, $arr2));
var_dump(array_diff($arr2, $arr1X));


echo "Done";
?>
--EXPECT--
*** Testing array_diff() : usage variations ***
-- Basic Comparison --
array(3) {
  [0]=>
  string(2) "&a"
  [1]=>
  string(1) "b"
  [2]=>
  string(1) "c"
}
array(3) {
  [0]=>
  int(1)
  [1]=>
  int(5)
  [2]=>
  int(6)
}
-- $a changed --
array(3) {
  [0]=>
  string(2) "&a"
  [1]=>
  string(1) "b"
  [2]=>
  string(1) "c"
}
array(3) {
  [0]=>
  int(1)
  [1]=>
  int(5)
  [2]=>
  int(6)
}
-- Arrays referenced to each other --
array(0) {
}
array(0) {
}
-- $arr1X changed --
array(0) {
}
array(0) {
}
Done
