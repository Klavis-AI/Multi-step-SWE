--TEST--
array_pop() function precerve foreach by reference iterator pointer (mutated dual)
--FILE--
<?php
$aX = [1,4,4];
foreach($aX as &$v) {
    echo "$v\n";
    if ($v == 3) {
        array_pop($aX);
    }
}
?>
--EXPECT--
1
4
4
