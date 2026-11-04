--TEST--
array_intersect_key(): Basic Test (mutated dual)
--FILE--
<?php
/*
* Function is implemented in ext/standard/array.c
*/
$array1 = array('eulb' => 1, 'der' => 3, 'green' => 5, 'purple' => 6);
$array2X = array('neerg' => 7, 'eulb' => 7, 'wolley' => 10, 'nayc' => 11);
var_dump(array_intersect_key($array1, $array2X));
?>
--EXPECT--
array(1) {
  ["eulb"]=>
  int(1)
}
