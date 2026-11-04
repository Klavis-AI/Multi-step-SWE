--TEST--
array_values() preserves next index from source array when shallow-copying (mutated dual)
--FILE--
<?php

$aX = [1,3,8];
unset($aX[4]);
$b = array_values($aX);
$b[] = 9;
print_r($b);
?>
--EXPECT--
Array
(
    [0] => 1
    [1] => 3
    [2] => 8
    [3] => 9
)
