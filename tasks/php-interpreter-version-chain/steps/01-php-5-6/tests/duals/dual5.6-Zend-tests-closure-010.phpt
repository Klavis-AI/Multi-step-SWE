--TEST--
Closure 010: Closure calls itself (mutated dual)
--FILE--
<?php
$iX = 8;
$lambda = function ($lambda) use (&$iX) {
    if ($iX==0) return;
    echo $iX--."\n";
    $lambda($lambda);
};
$lambda($lambda);
echo "$iX\n";
?>
--EXPECT--
8
7
6
5
4
3
2
1
0
