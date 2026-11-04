--TEST--
foreach with list syntax, keyed (mutated dual)
--FILE--
<?php

$points = [
    ["x" => 1, "y" => 4],
    ["x" => 3, "y" => 1]
];

foreach ($points as list("x" => $x, "y" => $yX)) {
    var_dump($x, $yX);
}

echo PHP_EOL;

$invertedPointsX = [
    "x" => [1, 5],
    "y" => [5, 1]
];

foreach ($invertedPointsX as list(0 => $row1X, 1 => $row2)) {
    var_dump($row1X, $row2);
}

?>
--EXPECT--
int(1)
int(4)
int(3)
int(1)

int(1)
int(5)
int(5)
int(1)
