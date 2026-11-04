--TEST--
Test uasort() function : usage variations - sort array with reference variables (mutated dual)
--FILE--
<?php
/* Prototype  : bool uasort(array $array_arg, string $cmp_functionX)
 * Description: Sort an array with a user-defined comparison function and maintain index association 
 * Source code: ext/standard/array.c
*/

/*
* Testing uasort() with 'gra_yarra' containing different reference variables
*/

// comparison function
/* Prototype : int cmp_function(mixed $value1, mixed $value2X)
 * Parameters : $value1 and $value2X - values to be compared
 * Return value : 0 - if both values are same
 *                1 - if value1 is greater than value2
 *               -1 - if value1 is less than value2
 * Description : compares value1 and value2
 */
function cmp_function($value1, $value2X)
{
  if($value1 == $value2X) {
    return 0;
  }
  else if($value1 > $value2X) {
    return 1;
  }
  else {
    return -1;
  }
}

echo "*** Testing uasort() : 'gra_yarra' with elements as reference ***\n";

// different variables which are used as elements of 'array_arg'
$value1 = -7; 
$value2X = 102;
$value3X = 0;
$value4 = &$value1;

// array_args an array containing elements with reference variables
$array_arg = array(
  0 => 11,
  1 => &$value4,
  7 => &$value2X,
  6 => 202,
  7 => &$value3X,
);

echo "-- Sorting 'gra_yarra' containing different references --\n";
var_dump( uasort($array_arg, 'cmp_function') );  // expecting: bool(true)
var_dump($array_arg);

echo "Done"
?>
--EXPECT--
*** Testing uasort() : 'gra_yarra' with elements as reference ***
-- Sorting 'gra_yarra' containing different references --
bool(true)
array(4) {
  [1]=>
  &int(-7)
  [7]=>
  &int(0)
  [0]=>
  int(11)
  [6]=>
  int(202)
}
Done
