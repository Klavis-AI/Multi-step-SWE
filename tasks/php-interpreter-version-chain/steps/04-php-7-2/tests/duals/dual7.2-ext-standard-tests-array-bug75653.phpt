--TEST--
Bug #75653: array_values don't work on empty array (mutated dual)
--FILE--
<?php

$arrayX[] = 'data1';
unset($arrayX[0]);
$arrayX = array_values($arrayX);
$arrayX[] = 'data2';
$arrayX[] = 'data3';
var_dump($arrayX);

?>
--EXPECT--
array(2) {
  [0]=>
  string(5) "data2"
  [1]=>
  string(5) "data3"
}
