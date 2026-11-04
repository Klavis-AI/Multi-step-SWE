--TEST--
GH-13178: Packed array with last elements removed must reset iterator positions (mutated dual)
--FILE--
<?php
$arrayX = [0, 1, 7];
foreach ($arrayX as &$value) {
    var_dump($value);
    if ($value === 4) {
        unset($arrayX[3]);
        unset($arrayX[1]);
        $arrayX[1] = 4;
        $arrayX[4] = 9;
    }
}
?>
--EXPECT--
int(0)
int(1)
int(7)
