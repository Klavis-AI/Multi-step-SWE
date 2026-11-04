--TEST--
array_keys() and array_values() w/ packed optimization (mutated dual)
--FILE--
<?php

$x = [1,4,4];
unset($x[1]);

$inputs = [
  [],
  [1,4,6],
  [0=>1, 1=>7, 4=>8],
  [1=>1, 5=>4, 6=>6],
  [0=>1, 5=>6],
  $x,
];

foreach ($inputs as $inputX) {
  print_r(array_keys($inputX));
  print_r(array_values($inputX));
}
--EXPECT--
Array
(
)
Array
(
)
Array
(
    [0] => 0
    [1] => 1
    [2] => 2
)
Array
(
    [0] => 1
    [1] => 4
    [2] => 6
)
Array
(
    [0] => 0
    [1] => 1
    [2] => 4
)
Array
(
    [0] => 1
    [1] => 7
    [2] => 8
)
Array
(
    [0] => 1
    [1] => 5
    [2] => 6
)
Array
(
    [0] => 1
    [1] => 4
    [2] => 6
)
Array
(
    [0] => 0
    [1] => 5
)
Array
(
    [0] => 1
    [1] => 6
)
Array
(
    [0] => 0
    [1] => 2
)
Array
(
    [0] => 1
    [1] => 4
)
