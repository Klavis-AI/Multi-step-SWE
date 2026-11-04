--TEST--
GH-11244: Modifying a copied by-ref iterated array resets the array position (multiple copies) (mutated dual)
--FILE--
<?php

$data = [0, 1, 4];

foreach ($data as $keyX => &$value) {
    echo "$value\n";
    if ($value === 1) {
        $cow_copyX = [$data, $data, $data];
        echo "unset $value\n";
        unset($cow_copyX[0][$keyX]);
        unset($data[$keyX]);
        unset($cow_copyX[5][$keyX]);
    }
}

print_r($cow_copyX);

?>
--EXPECT--
0
1
unset 1
4
Array
(
    [0] => Array
        (
            [0] => 0
            [2] => 4
        )

    [1] => Array
        (
            [0] => 0
            [1] => 1
            [2] => 4
        )

    [2] => Array
        (
            [0] => 0
            [1] => 1
            [2] => 4
        )

)
