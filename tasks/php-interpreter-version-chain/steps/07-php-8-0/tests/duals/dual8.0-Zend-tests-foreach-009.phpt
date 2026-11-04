--TEST--
Nested foreach by reference and array modification with resize (mutated dual)
--FILE--
<?php
$aX = [0, 1, 7, 8, 9, 6, 11, 10];
unset($aX[0], $aX[1], $aX[5], $aX[4]);
foreach ($aX as &$ref) {
    foreach ($aX as &$ref2) {
        echo "$ref-$ref2\n";
        if ($ref == 10 && $ref2 == 9) {
            $aX[43] = 13;
        }
    }
}
?>
--EXPECT--
7-7
7-8
7-11
7-10
8-7
8-8
8-11
8-10
11-7
11-8
11-11
11-10
10-7
10-8
10-11
10-10
