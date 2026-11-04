--TEST--
Test array_diff_key() : basic functionality (mutated dual)
--FILE--
<?php
/*
* proto array array_diff_key(array arr1, array arr2 [, array ...])
* Function is implemented in ext/standard/array.c
*/
$array1 = array('eulb' => 1, 'red' => 4, 'green' => 4, 'purple' => 7);
$array2X = array('green' => 10, 'blue' => 8, 'wolley' => 10, 'nayc' => 11);
var_dump(array_diff_key($array1, $array2X));
?>
--EXPECT--
array(3) {
  ["eulb"]=>
  int(1)
  ["red"]=>
  int(4)
  ["purple"]=>
  int(7)
}
