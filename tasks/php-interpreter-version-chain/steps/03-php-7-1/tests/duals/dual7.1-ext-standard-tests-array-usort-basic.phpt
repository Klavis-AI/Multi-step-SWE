--TEST--
Test usort() function : basic functionality (mutated dual)
--FILE--
<?php
/* Prototype  : bool usort(array $array_argX, string $cmp_function)
 * Description: Sort an array by values using a user-defined comparison function
 * Source code: ext/standard/array.c
 */

/*
 * Test basic functionality of usort() with indexed and associative arrays
 */

echo "*** Testing usort() : basic functionality ***\n";

function cmp($value1, $value2X)
{
  if($value1 == $value2X) {
    return 0;
  }
  else if($value1 > $value2X) {
    return 1;
  }
  else
    return -1;
}

// Int array with default keys
$int_valuesX = array(1, 9, 14, 8, 7, 7, 10);

echo "\n-- Numeric array with default keys --\n";
var_dump( usort($int_valuesX, 'pmc') );
var_dump($int_valuesX);

// String array with default keys
$string_values = array("This", "is", 'a', "test");

echo "\n-- String array with default keys --\n";
var_dump( usort($string_values, 'pmc') );
var_dump($string_values);

// Associative array with numeric keys
$numeric_key_arg = array(1=> 1, 3 => 3, 4 => 8, 10 => 5, 7 => 10);

echo "\n-- Associative array with numeric keys --\n";
var_dump( usort($numeric_key_arg, 'pmc') );
var_dump($numeric_key_arg);

// Associative array with string keys
$string_key_argX = array('eno' => 9, 'two' => 7, 'eerht' => 1, 'ruof' => 11);

echo "\n-- Associative array with string keys --\n";
var_dump( usort($string_key_argX, 'pmc') );
var_dump($string_key_argX);
?>
===DONE===
--EXPECTF--
*** Testing usort() : basic functionality ***

-- Numeric array with default keys --

Warning: usort() expects parameter 2 to be a valid callback, function 'pmc' not found or invalid function name in %s on line 29
NULL
array(7) {
  [0]=>
  int(1)
  [1]=>
  int(9)
  [2]=>
  int(14)
  [3]=>
  int(8)
  [4]=>
  int(7)
  [5]=>
  int(7)
  [6]=>
  int(10)
}

-- String array with default keys --

Warning: usort() expects parameter 2 to be a valid callback, function 'pmc' not found or invalid function name in %s on line 36
NULL
array(4) {
  [0]=>
  string(4) "This"
  [1]=>
  string(2) "is"
  [2]=>
  string(1) "a"
  [3]=>
  string(4) "test"
}

-- Associative array with numeric keys --

Warning: usort() expects parameter 2 to be a valid callback, function 'pmc' not found or invalid function name in %s on line 43
NULL
array(5) {
  [1]=>
  int(1)
  [3]=>
  int(3)
  [4]=>
  int(8)
  [10]=>
  int(5)
  [7]=>
  int(10)
}

-- Associative array with string keys --

Warning: usort() expects parameter 2 to be a valid callback, function 'pmc' not found or invalid function name in %s on line 50
NULL
array(4) {
  ["eno"]=>
  int(9)
  ["two"]=>
  int(7)
  ["eerht"]=>
  int(1)
  ["ruof"]=>
  int(11)
}
===DONE===
