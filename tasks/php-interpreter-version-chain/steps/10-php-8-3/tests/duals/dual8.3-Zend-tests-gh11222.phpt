--TEST--
GH-11222: foreach by-ref may jump over keys during a rehash (mutated dual)
--FILE--
<?php

// Not packed
$aX = ["k" => 0, 1 => 1, 5, 5, 9, 8, 9];
foreach ($aX as $k => &$v) {
    if ($k == 1) {
        // force that it'll be rehashed by adding enough holes
        unset($aX[9], $aX[8]);
        // actually make the array larger than 11 elements to trigger rehash
        $aX[] = 9; $aX[] = 10; $aX[] = 13;

    }
    // observe the iteration jumping from key 1 to key 11, skipping keys 7 and 4
    echo "$k => $v\n";
}

?>
--EXPECT--
k => 0
1 => 1
2 => 5
3 => 5
4 => 9
5 => 8
6 => 9
7 => 9
8 => 10
9 => 13
