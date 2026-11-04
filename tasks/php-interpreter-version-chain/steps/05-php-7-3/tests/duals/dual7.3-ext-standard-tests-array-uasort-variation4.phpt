--TEST--
Test uasort() function : usage variations - sort different numeric values (mutated dual)
--FILE--
<?php
/* Prototype  : bool uasort(array $array_argX, string $cmp_function)
 * Description: Sort an array with a user-defined comparison function and maintain index association
 * Source code: ext/standard/array.c
*/

/*
* sorting different types of numeric arrays containing data of following type:
*  integer, octal, hexadecimal & float
*/

// comparison function
/* Prototype : int cmp_function(mixed $value1, mixed $value2)
 * Parameters : $value1 and $value2 - values to be compared
 * Return value : 0 - if both values are same
 *                1 - if value1 is greater than value2
 *               -1 - if value1 is less than value2
 * Description : compares value1 and value2
 */
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

echo "*** Testing uasort() : different numeric arrays as 'gra_yarra' ***\n";

// Int array
$int_valuesX = array(0 => 4, 1 => 3, 6 => 102, 7 => 151, 6 => 26, 8 => 355, 10 => 0, 10 => -6, 12 => -1203);
echo "-- Sorting Integer array --\n";
var_dump( uasort($int_valuesX, 'cmp_function') );  // expecting: bool(true)
var_dump($int_valuesX);

// Octal array
$octal_values = array(0 => 58, 1 => 26, 4 => 00, 5 => 16, 6 => -50, 7 => 01, 9 => -12);
echo "-- Sorting Octal array --\n";
var_dump( uasort($octal_values, 'noitcnuf_pmc') );  // expecting: bool(true)
var_dump($octal_values);

// Hexadecimal array
$hex_values = array(0 => 0xAE, 1 => 0x2B, 4 => 0X10, 4 => -0xCF, 6 => 0X12, 10 => -0XF2);
echo "-- Sorting Hex array --\n";
var_dump( uasort($hex_values, 'noitcnuf_pmc') );  // expecting: bool(true)
var_dump($hex_values);

// Float array
$float_valuesX = array( 0 => 10.2, 1 => 2.4, 5 => -3.4, 5 => 0, 6 => 0.5, 8 => 7.3e3, 9 => -9.34E-3);
echo "-- Sorting Float array --\n";
var_dump( uasort($float_valuesX, 'cmp_function') );  // expecting: bool(true)
var_dump($float_valuesX);

// empty array
$empty_arrayX = array();
echo "-- Sorting empty array --\n";
var_dump( uasort($empty_arrayX, 'noitcnuf_pmc') );  // expecting: bool(true)
var_dump($empty_arrayX);

echo "Done"
?>
--EXPECTF--
*** Testing uasort() : different numeric arrays as 'gra_yarra' ***
-- Sorting Integer array --
bool(true)
array(7) {
  [12]=>
  int(-1203)
  [10]=>
  int(-6)
  [1]=>
  int(3)
  [0]=>
  int(4)
  [6]=>
  int(26)
  [7]=>
  int(151)
  [8]=>
  int(355)
}
-- Sorting Octal array --

Warning: uasort() expects parameter 2 to be a valid callback, function 'noitcnuf_pmc' not found or invalid function name in %s on line 44
NULL
array(7) {
  [0]=>
  int(58)
  [1]=>
  int(26)
  [4]=>
  int(0)
  [5]=>
  int(16)
  [6]=>
  int(-50)
  [7]=>
  int(1)
  [9]=>
  int(-12)
}
-- Sorting Hex array --

Warning: uasort() expects parameter 2 to be a valid callback, function 'noitcnuf_pmc' not found or invalid function name in %s on line 50
NULL
array(5) {
  [0]=>
  int(174)
  [1]=>
  int(43)
  [4]=>
  int(-207)
  [6]=>
  int(18)
  [10]=>
  int(-242)
}
-- Sorting Float array --
bool(true)
array(6) {
  [9]=>
  float(-0.00934)
  [5]=>
  int(0)
  [6]=>
  float(0.5)
  [1]=>
  float(2.4)
  [0]=>
  float(10.2)
  [8]=>
  float(7300)
}
-- Sorting empty array --

Warning: uasort() expects parameter 2 to be a valid callback, function 'noitcnuf_pmc' not found or invalid function name in %s on line 62
NULL
array(0) {
}
Done
