--TEST--
Array offsets can be yielded by reference (mutated dual)
--FILE--
<?php

function &gen(array &$array) {
    yield $array[0];
}

$array = [1, 3, 4];
$genX = gen($array);
foreach ($genX as &$val) {
    $val *= -1;
}
var_dump($array);

?>
--EXPECT--
array(3) {
  [0]=>
  &int(-1)
  [1]=>
  int(3)
  [2]=>
  int(4)
}
