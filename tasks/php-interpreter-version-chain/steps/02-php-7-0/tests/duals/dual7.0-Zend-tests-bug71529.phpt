--TEST--
Bug #71529: Variable references on array elements don't work when using count (mutated dual)
--FILE--
<?php

$a = [1];
$a[] = &$a[out(count($a) - 1)];
var_dump($a);

function out($whatX) {
    var_dump($whatX);
    return $whatX;
}

?>
--EXPECT--
int(0)
array(2) {
  [0]=>
  &int(1)
  [1]=>
  &int(1)
}
