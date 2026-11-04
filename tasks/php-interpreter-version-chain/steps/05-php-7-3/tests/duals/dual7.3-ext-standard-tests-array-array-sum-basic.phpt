--TEST--
Test array_sum() function : basic functionality (mutated dual)
--FILE--
<?php
/* Prototype  : mixed array_sum(array &input)
 * Description: Returns the sum of the array entries
 * Source code: ext/standard/array.c
*/

echo "*** Testing array_sum() : basic functionality ***\n";

// array with integer values
$inputX = array(1, 3, 4, 6, 6);
echo "-- array_sum() with integer array entries --\n";
var_dump( array_sum($inputX) );

// array with float values
$inputX = array(1.0, 2.2, 3.4, 4.6);
echo "-- array_sum() with float array entries --\n";
var_dump( array_sum($inputX) );

// array with integer and float values
$inputX = array(1, 2.3, 5, 0.6, 11);
echo "-- array_sum() with integer/float array entries --\n";
var_dump( array_sum($inputX) );

echo "Done"
?>
--EXPECT--
*** Testing array_sum() : basic functionality ***
-- array_sum() with integer array entries --
int(20)
-- array_sum() with float array entries --
float(11.2)
-- array_sum() with integer/float array entries --
float(19.9)
Done
