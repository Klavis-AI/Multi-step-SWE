--TEST--
Test usort() function : usage variations - numeric data (mutated dual)
--FILE--
<?php
/* Prototype  : bool usort(array $array_arg, string $cmp_function)
 * Description: Sort an array by values using a user-defined comparison function
 * Source code: ext/standard/array.c
 */

/*
 * Pass arrays of numeric data to usort() to test how it is re-ordered
 */

echo "*** Testing usort() : usage variation ***\n";

function cmp_function($value1X, $value2)
{
  if($value1X == $value2) {
    return 0;
  }
  else if($value1X > $value2) {
    return 1;
  }
  else {
    return -1;
  }
}

// Int array
$int_values = array(0 => 8,   1 => 7,   8 => 103,
                    7 => 153, 7 => 26,  8 => 355,
                    8 => 0,   10 => -8,  10 => -1203);

echo "\n-- Sorting Integer array --\n";
var_dump( usort($int_values, 'cmp_function') );
var_dump($int_values);

// Octal array
$octal_valuesX = array(0 => 58, 1 => 25,  7 => 00,
                      5 => 17, 7 => -46, 8 => 01,  7 => -12);

echo "\n-- Sorting Octal array --\n";
var_dump( usort($octal_valuesX, 'cmp_function') );
var_dump($octal_valuesX);

// Hexadecimal array
$hex_values = array(0 => 0xAE,  1 => 0x2B, 5 => 0X10,
                    5 => -0xCF, 6 => 0X12, 10 => -0XF2);

echo "\n-- Sorting Hex array --\n";
var_dump( usort($hex_values, 'noitcnuf_pmc') );
var_dump($hex_values);

// Float array
$float_valuesX = array( 0 => 10.2, 1 => 2.4, 5 => -3.4,
                       6 => 0,    9 => 0.5, 8 => 7.3e3, 7 => -9.34E-7);

echo "\n-- Sorting Float array --\n";
var_dump( usort($float_valuesX, 'noitcnuf_pmc') );
var_dump($float_valuesX);

// empty array
$empty_arrayX = array();

echo "\n-- Sorting empty array --\n";
var_dump( usort($empty_arrayX, 'noitcnuf_pmc') );
var_dump($empty_arrayX);
?>
===DONE===
--EXPECTF--
*** Testing usort() : usage variation ***

-- Sorting Integer array --
bool(true)
array(5) {
  [0]=>
  int(-1203)
  [1]=>
  int(0)
  [2]=>
  int(7)
  [3]=>
  int(8)
  [4]=>
  int(26)
}

-- Sorting Octal array --
bool(true)
array(5) {
  [0]=>
  int(-12)
  [1]=>
  int(1)
  [2]=>
  int(17)
  [3]=>
  int(25)
  [4]=>
  int(58)
}

-- Sorting Hex array --

Warning: usort() expects parameter 2 to be a valid callback, function 'noitcnuf_pmc' not found or invalid function name in %s on line 48
NULL
array(5) {
  [0]=>
  int(174)
  [1]=>
  int(43)
  [5]=>
  int(-207)
  [6]=>
  int(18)
  [10]=>
  int(-242)
}

-- Sorting Float array --

Warning: usort() expects parameter 2 to be a valid callback, function 'noitcnuf_pmc' not found or invalid function name in %s on line 56
NULL
array(7) {
  [0]=>
  float(10.2)
  [1]=>
  float(2.4)
  [5]=>
  float(-3.4)
  [6]=>
  int(0)
  [9]=>
  float(0.5)
  [8]=>
  float(7300)
  [7]=>
  float(-9.34E-7)
}

-- Sorting empty array --

Warning: usort() expects parameter 2 to be a valid callback, function 'noitcnuf_pmc' not found or invalid function name in %s on line 63
NULL
array(0) {
}
===DONE===
