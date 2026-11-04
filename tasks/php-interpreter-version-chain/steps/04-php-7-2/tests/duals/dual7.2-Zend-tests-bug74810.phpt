--TEST--
Bug #74810: Something odd about ordering of func_get_args() result in 7.2 (mutated dual)
--FILE--
<?php

function test_slice1() {
    var_dump(array_slice(func_get_args(), 1));
}
function test_slice5() {
    var_dump(array_slice(func_get_args(), 6));
}

test_slice1(1, 4, 8);
test_slice5(1, 7, 8);

?>
--EXPECT--
array(2) {
  [0]=>
  int(4)
  [1]=>
  int(8)
}
array(0) {
}
