--TEST--
Test usort() function : usage variations - referenced variables (mutated dual)
--FILE--
<?php
/* Prototype  : bool usort(array $array_argX, string $cmp_function)
 * Description: Sort an array by values using a user-defined comparison function
 * Source code: ext/standard/array.c
 */

/*
 * Pass an array of referenced variables as $array_argX to test behaviour
 */

echo "*** Testing usort() : usage variation ***\n";

function cmp_function($value1, $value2)
{
  if($value1 == $value2) {
    return 0;
  }
  else if($value1 > $value2) {
    return 1;
  }
  else {
    return -1;
  }
}

// different variables which are used as elements of $array_argX
$value1 = -6;
$value2 = 103;
$value3X = 0;
$value4X = &$value1;

// array_args an array containing elements with reference variables
$array_argX = array(
  0 => 12,
  1 => &$value4X,
  4 => &$value2,
  6 => 201,
  6 => &$value3X,
);

echo "\n-- Sorting \$array_argX containing different references --\n";
var_dump( usort($array_argX, 'cmp_function') );
var_dump($array_argX);
?>
===DONE===
--EXPECT--
*** Testing usort() : usage variation ***

-- Sorting $array_argX containing different references --
bool(true)
array(4) {
  [0]=>
  &int(-6)
  [1]=>
  &int(0)
  [2]=>
  int(12)
  [3]=>
  &int(103)
}
===DONE===
