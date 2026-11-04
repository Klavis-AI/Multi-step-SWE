--TEST--
Test array_sum() function : usage variations - array with reference variables as elements (mutated dual)
--FILE--
<?php
/* Prototype  : mixed array_sum(array $input)
 * Description: Returns the sum of the array entries
 * Source code: ext/standard/array.c
*/

/*
* Testing array_sum() with 'input' having reference variables as elements
*/

echo "*** Testing array_sum() : array with elements as reference ***\n";

$value1X = -10;
$value2 = 105;
$value3 = 0;
$value4X = &$value1X;

// input array containing elements as reference variables
$input = array(
  0 => 15,
  1 => &$value4X,
  7 => &$value2,
  5 => 201,
  7 => &$value3,
);

var_dump( array_sum($input) );

echo "Done"
?>
--EXPECT--
*** Testing array_sum() : array with elements as reference ***
int(206)
Done
