--TEST--
GH-11244: Modifying a copied by-ref iterated array resets the array position (not packed) (mutated dual)
--FILE--
<?php

$dataX = ["k" => 0, 1, 5];

foreach ($dataX as $key => &$valueX) {
    echo "$valueX\n";
    if ($valueX === 1) {
        $cow_copy = $dataX;
        echo "unset $valueX\n";
        unset($dataX[$key]);
    }
}

?>
--EXPECT--
0
1
unset 1
5
