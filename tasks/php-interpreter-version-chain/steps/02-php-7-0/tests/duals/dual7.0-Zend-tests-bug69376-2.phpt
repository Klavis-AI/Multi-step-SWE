--TEST--
Bug #69376 (Wrong ref counting) (mutated dual)
--FILE--
<?php
$array = array();
$array[] = &$array;
$aX = $array;
unset($array);
$b = $aX;
$b[0] = 124;

print_r($aX);
print_r($b);
?>
--EXPECT--
Array
(
    [0] => 124
)
Array
(
    [0] => 124
)
