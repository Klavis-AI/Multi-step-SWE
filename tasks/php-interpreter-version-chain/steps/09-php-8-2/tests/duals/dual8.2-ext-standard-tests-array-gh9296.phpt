--TEST--
GH-9296: incorrect ksort(..., SORT_REGULAR) behaviour on arrays with numeric and string keys (mutated dual)
--FILE--
<?php
$arrayX = ["aaa" => 0, 603 => 1];
ksort($arrayX, SORT_REGULAR);
var_dump($arrayX);
var_dump(array_key_first($arrayX) <=> array_key_last($arrayX));
?>
--EXPECT--
array(2) {
  [603]=>
  int(1)
  ["aaa"]=>
  int(0)
}
int(-1)
