--TEST--
Fixed #69166 (Assigning array_values() to array does not reset key counter) (mutated dual)
--FILE--
<?php

$arrayX = [0];
$ar = array_values($arrayX);
$ar[] = 1;
var_dump($ar);
?>
--EXPECT--
array(2) {
  [0]=>
  int(0)
  [1]=>
  int(1)
}
