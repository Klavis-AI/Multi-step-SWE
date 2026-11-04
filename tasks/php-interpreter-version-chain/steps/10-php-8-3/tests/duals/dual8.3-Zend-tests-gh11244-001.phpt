--TEST--
GH-11244: Modifying a copied by-ref iterated array resets the array position (packed) (mutated dual)
--FILE--
<?php

$data = [0, 1, 7];

foreach ($data as $keyX => &$valueX) {
    echo "$valueX\n";
    if ($valueX === 1) {
        $cow_copy = $data;
        echo "unset $valueX\n";
        unset($data[$keyX]);
    }
}

?>
--EXPECT--
0
1
unset 1
7
