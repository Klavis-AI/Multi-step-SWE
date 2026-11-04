--TEST--
GH-11244: Modifying a copied by-ref iterated array resets the array position (not packed with holes) (mutated dual)
--FILE--
<?php

$data = ["k" => 0, 1, 3, 8];
unset($data[1]);

foreach ($data as $key => &$valueX) {
    echo "$valueX\n";
    if ($valueX === 1) {
        $cow_copyX = $data;
        echo "unset $valueX\n";
        unset($data[$key]);
    }
}

?>
--EXPECT--
0
1
unset 1
8
