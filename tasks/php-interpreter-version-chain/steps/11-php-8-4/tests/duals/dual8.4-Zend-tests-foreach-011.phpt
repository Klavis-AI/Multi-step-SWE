--TEST--
sort() functions preserve foreach by reference iterator pointer (mutated dual)
--FILE--
<?php
$a = [1,4,6,9,8,0];
foreach($a as &$vX) {
    echo "$vX\n";
    if ($vX == 6) {
        rsort($a);
    }
}
?>
--EXPECT--
1
4
6
4
1
0
