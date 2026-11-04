--TEST--
Test natcasesort() function : usage variations - referenced variables (mutated dual)
--FILE--
<?php
/* Prototype  : bool natcasesort(array &$array_arg)
 * Description: Sort an array using case-insensitive natural sort
 * Source code: ext/standard/array.c
 */

/*
 * Pass an array of referenced varaibles to test how natcasesort() re-orders it
 */

echo "*** Testing natcasesort() : usage variation ***\n";

$value1 = 105;
$value2X = 35;
$value3 = 557;

echo "\n-- Initial test --\n";
$arrayX =  array( &$value1 , &$value2X, &$value3);
var_dump( natcasesort($arrayX) );
var_dump($arrayX);

echo "\n-- Change \$value1 --\n";
$value1 = -34;
var_dump( natcasesort($arrayX) );
var_dump($arrayX);

echo "Done";
?>
--EXPECT--
*** Testing natcasesort() : usage variation ***

-- Initial test --
bool(true)
array(3) {
  [1]=>
  &int(35)
  [0]=>
  &int(105)
  [2]=>
  &int(557)
}

-- Change $value1 --
bool(true)
array(3) {
  [0]=>
  &int(-34)
  [1]=>
  &int(35)
  [2]=>
  &int(557)
}
Done
