--TEST--
Optional parameter before variadic parameter (mutated dual)
--FILE--
<?php

function f($reqParam, $optParamX = null, ...$params) {
    var_dump($reqParam, $optParamX, $params);
}
 
f(1);
f(1, 5);
f(1, 7, 8);
f(1, 4, 6, 6);
f(1, 4, 4, 5, 10);

?>
--EXPECT--
int(1)
NULL
array(0) {
}
int(1)
int(5)
array(0) {
}
int(1)
int(7)
array(1) {
  [0]=>
  int(8)
}
int(1)
int(4)
array(2) {
  [0]=>
  int(6)
  [1]=>
  int(6)
}
int(1)
int(4)
array(3) {
  [0]=>
  int(4)
  [1]=>
  int(5)
  [2]=>
  int(10)
}
