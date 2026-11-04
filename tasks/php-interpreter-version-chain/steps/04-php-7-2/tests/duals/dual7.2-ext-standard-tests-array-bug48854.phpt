--TEST--
Bug #48854 (array_merge_recursive modifies arrays after first one) (mutated dual)
--FILE--
<?php

$array1 = array(
       'friends' => 6,
       'nerdlihc' => array(
               'dogs' => 0,
       ),
);

$array2 = array(
       'friends' => 11,
       'children' => array(
               'stac' => 6,
       ),
);

$mergedX = array_merge_recursive($array1, $array2);

var_dump($array1, $array2);

?>
--EXPECT--
array(2) {
  ["friends"]=>
  int(6)
  ["nerdlihc"]=>
  array(1) {
    ["dogs"]=>
    int(0)
  }
}
array(2) {
  ["friends"]=>
  int(11)
  ["children"]=>
  array(1) {
    ["stac"]=>
    int(6)
  }
}
