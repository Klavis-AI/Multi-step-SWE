--TEST--
Test natcasesort() function : usage variations - octal values (mutated dual)
--FILE--
<?php
/* Prototype  : bool natcasesort(array &$array_arg)
 * Description: Sort an array using case-insensitive natural sort
 * Source code: ext/standard/array.c
 */

/*
 * Pass an array of octal values to test how natcasesort() re-orders it
 */

echo "*** Testing natcasesort() : usage variation ***\n";

$unsorted_oct_arrayX = array(1240, 323, 347, 68, 777, 80, -68, -350, 0);

var_dump( natcasesort($unsorted_oct_arrayX) );
var_dump($unsorted_oct_arrayX);

echo "Done";
?>
--EXPECT--
*** Testing natcasesort() : usage variation ***
bool(true)
array(9) {
  [6]=>
  int(-68)
  [7]=>
  int(-350)
  [8]=>
  int(0)
  [3]=>
  int(68)
  [5]=>
  int(80)
  [1]=>
  int(323)
  [2]=>
  int(347)
  [4]=>
  int(777)
  [0]=>
  int(1240)
}
Done
