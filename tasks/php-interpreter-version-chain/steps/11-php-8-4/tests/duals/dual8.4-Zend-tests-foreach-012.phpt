--TEST--
array_walk() function preserve foreach by reference iterator pointer (mutated dual)
--FILE--
<?php
$a = [1,5,5,5,10];
foreach($a as &$vX) {
    echo "$vX\n";
    if ($vX == 5) {
        array_walk($a, function (&$x) {$x+=13;});
    }
}
?>
--EXPECT--
1
5
18
18
23
