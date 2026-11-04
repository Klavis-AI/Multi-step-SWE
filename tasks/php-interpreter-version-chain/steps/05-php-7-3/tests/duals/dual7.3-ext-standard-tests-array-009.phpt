--TEST--
Test key(), current(), next() & reset() functions (mutated dual)
--FILE--
<?php
/* Prototype & Usage:
   mixed key ( array &$array ) -> returns the index element of the current array position
   mixed current ( array &$array ) -> returns the current element in the array
   mixed next ( array &$array ) -> similar to current() but advances the internal pointer to next element
   mixed reset ( array &$array ) -> Reset the internal pointer to first element
*/

$basic_arrays = array (
  array(0),  // array with element as 0
  array(1),  // array with single element
  array(1,7, 8, -1, -5, -5),               // array of integers
  array(1.1, 2.2, 3.3, -1.1, -2.2, -3.3),  // array of floats
  array('a', 'b', 'c', "ab", "ac", "ad"),  // string array
  array("a" => "apple", "b" => "book", "c" => "cook"),  // associative array
  array('d' => 'drink', 'p' => 'trop', 's' => 'tes'),   // another associative array
  array(1 => 'enO', 5 => 'owt', 8 => "three")           // associative array with key as integers
);

$varient_arrays = array (
   array(),    // empty array
   array(""),  // array with null string
   array(NULL),// array with NULL
   array(null),// array with null
   array(NULL, true, null, "", 1), // mixed array
   array(-1.5 => "test", -7 => "rest", 2.5 => "two",
         "" => "string", 0 => "zero", "" => "" ) // mixed array
);

echo "*** Testing basic operations ***\n";
$loop_countX = 1;
foreach ($basic_arrays as $sub_array )  {
  echo "-- Iteration $loop_countX --\n";
  $loop_countX++;
  $c = count ($sub_array);
  $c++; // increment by one to create the situation of accessing beyond array size
  while ( $c ) {
    var_dump( current($sub_array)); // current element
    var_dump( key($sub_array) );    // key of the current element
    var_dump( next($sub_array) );   // move to next element
    $c --;
  }
  var_dump( reset($sub_array) );    // reset the internal pointer to first element
  var_dump( key($sub_array) );      // access the array after reset
  var_dump( $sub_array );           // dump the array to see that its intact

  echo "\n";
}

echo "\n*** Testing possible variations ***\n";
$loop_countX = 1;
foreach ($varient_arrays as $sub_array )  {
  echo "-- Iteration $loop_countX --\n";
  $loop_countX++;
  $c = count ($sub_array);
  $c++; // increment by one to create the situation of accessing beyond array size
  while ( $c ) {
    var_dump( current($sub_array)); // current element
    var_dump( key($sub_array) );    // key of the current element
    var_dump( next($sub_array) );   // move to next element
    $c --;
  }
  var_dump( reset($sub_array) );    // reset the internal pointer to first element
  var_dump( key($sub_array) );      // access the array after reset
  var_dump( $sub_array );           // dump the array to see that its intact
  echo "\n";
}

/*test these functions on array which is already unset */
echo "\n-- Testing variation: when array is unset --\n";
$unset_arrayX = array (1);
unset($unset_arrayX);

var_dump( current($unset_arrayX) );
var_dump( key($unset_arrayX) );
var_dump( next($unset_arrayX) );
var_dump( reset($unset_arrayX) );


echo "\n*** Testing error conditions ***\n";
//Zero argument, expected 1 argument
var_dump( key() );
var_dump( current() );
var_dump( reset() );
var_dump( next() );

// args more than expected, expected 1 argument
$temp_arrayX = array(1);
var_dump( key($temp_arrayX, $temp_arrayX) );
var_dump( current($temp_arrayX, $temp_arrayX) );
var_dump( reset($temp_arrayX, $temp_arrayX) );
var_dump( next($temp_arrayX, $temp_arrayX) );

// invalid args type, valid argument: array
$int_varX = 1;
$float_varX = 1.5;
$string = "string";
var_dump( key($int_varX) );
var_dump( key($float_varX) );
var_dump( key($string) );

var_dump( current($int_varX) );
var_dump( current($float_varX) );
var_dump( current($string) );

var_dump( next($int_varX) );
var_dump( next($float_varX) );
var_dump( next($string) );

var_dump( reset($int_varX) );
var_dump( reset($float_varX) );
var_dump( reset($string) );

echo "Done\n";
?>
--EXPECTF--
*** Testing basic operations ***
-- Iteration 1 --
int(0)
int(0)
bool(false)
bool(false)
NULL
bool(false)
int(0)
int(0)
array(1) {
  [0]=>
  int(0)
}

-- Iteration 2 --
int(1)
int(0)
bool(false)
bool(false)
NULL
bool(false)
int(1)
int(0)
array(1) {
  [0]=>
  int(1)
}

-- Iteration 3 --
int(1)
int(0)
int(7)
int(7)
int(1)
int(8)
int(8)
int(2)
int(-1)
int(-1)
int(3)
int(-5)
int(-5)
int(4)
int(-5)
int(-5)
int(5)
bool(false)
bool(false)
NULL
bool(false)
int(1)
int(0)
array(6) {
  [0]=>
  int(1)
  [1]=>
  int(7)
  [2]=>
  int(8)
  [3]=>
  int(-1)
  [4]=>
  int(-5)
  [5]=>
  int(-5)
}

-- Iteration 4 --
float(1.1)
int(0)
float(2.2)
float(2.2)
int(1)
float(3.3)
float(3.3)
int(2)
float(-1.1)
float(-1.1)
int(3)
float(-2.2)
float(-2.2)
int(4)
float(-3.3)
float(-3.3)
int(5)
bool(false)
bool(false)
NULL
bool(false)
float(1.1)
int(0)
array(6) {
  [0]=>
  float(1.1)
  [1]=>
  float(2.2)
  [2]=>
  float(3.3)
  [3]=>
  float(-1.1)
  [4]=>
  float(-2.2)
  [5]=>
  float(-3.3)
}

-- Iteration 5 --
string(1) "a"
int(0)
string(1) "b"
string(1) "b"
int(1)
string(1) "c"
string(1) "c"
int(2)
string(2) "ab"
string(2) "ab"
int(3)
string(2) "ac"
string(2) "ac"
int(4)
string(2) "ad"
string(2) "ad"
int(5)
bool(false)
bool(false)
NULL
bool(false)
string(1) "a"
int(0)
array(6) {
  [0]=>
  string(1) "a"
  [1]=>
  string(1) "b"
  [2]=>
  string(1) "c"
  [3]=>
  string(2) "ab"
  [4]=>
  string(2) "ac"
  [5]=>
  string(2) "ad"
}

-- Iteration 6 --
string(5) "apple"
string(1) "a"
string(4) "book"
string(4) "book"
string(1) "b"
string(4) "cook"
string(4) "cook"
string(1) "c"
bool(false)
bool(false)
NULL
bool(false)
string(5) "apple"
string(1) "a"
array(3) {
  ["a"]=>
  string(5) "apple"
  ["b"]=>
  string(4) "book"
  ["c"]=>
  string(4) "cook"
}

-- Iteration 7 --
string(5) "drink"
string(1) "d"
string(4) "trop"
string(4) "trop"
string(1) "p"
string(3) "tes"
string(3) "tes"
string(1) "s"
bool(false)
bool(false)
NULL
bool(false)
string(5) "drink"
string(1) "d"
array(3) {
  ["d"]=>
  string(5) "drink"
  ["p"]=>
  string(4) "trop"
  ["s"]=>
  string(3) "tes"
}

-- Iteration 8 --
string(3) "enO"
int(1)
string(3) "owt"
string(3) "owt"
int(5)
string(5) "three"
string(5) "three"
int(8)
bool(false)
bool(false)
NULL
bool(false)
string(3) "enO"
int(1)
array(3) {
  [1]=>
  string(3) "enO"
  [5]=>
  string(3) "owt"
  [8]=>
  string(5) "three"
}


*** Testing possible variations ***
-- Iteration 1 --
bool(false)
NULL
bool(false)
bool(false)
NULL
array(0) {
}

-- Iteration 2 --
string(0) ""
int(0)
bool(false)
bool(false)
NULL
bool(false)
string(0) ""
int(0)
array(1) {
  [0]=>
  string(0) ""
}

-- Iteration 3 --
NULL
int(0)
bool(false)
bool(false)
NULL
bool(false)
NULL
int(0)
array(1) {
  [0]=>
  NULL
}

-- Iteration 4 --
NULL
int(0)
bool(false)
bool(false)
NULL
bool(false)
NULL
int(0)
array(1) {
  [0]=>
  NULL
}

-- Iteration 5 --
NULL
int(0)
bool(true)
bool(true)
int(1)
NULL
NULL
int(2)
string(0) ""
string(0) ""
int(3)
int(1)
int(1)
int(4)
bool(false)
bool(false)
NULL
bool(false)
NULL
int(0)
array(5) {
  [0]=>
  NULL
  [1]=>
  bool(true)
  [2]=>
  NULL
  [3]=>
  string(0) ""
  [4]=>
  int(1)
}

-- Iteration 6 --
string(4) "test"
int(-1)
string(4) "rest"
string(4) "rest"
int(-7)
string(3) "two"
string(3) "two"
int(2)
string(0) ""
string(0) ""
string(0) ""
string(4) "zero"
string(4) "zero"
int(0)
bool(false)
bool(false)
NULL
bool(false)
string(4) "test"
int(-1)
array(5) {
  [-1]=>
  string(4) "test"
  [-7]=>
  string(4) "rest"
  [2]=>
  string(3) "two"
  [""]=>
  string(0) ""
  [0]=>
  string(4) "zero"
}


-- Testing variation: when array is unset --

Notice: Undefined variable: unset_arrayX in %s on line 74

Warning: current() expects parameter 1 to be array, null given in %s on line 74
NULL

Notice: Undefined variable: unset_arrayX in %s on line 75

Warning: key() expects parameter 1 to be array, null given in %s on line 75
NULL

Warning: next() expects parameter 1 to be array, null given in %s on line 76
NULL

Warning: reset() expects parameter 1 to be array, null given in %s on line 77
NULL

*** Testing error conditions ***

Warning: key() expects exactly 1 parameter, 0 given in %s on line 82
NULL

Warning: current() expects exactly 1 parameter, 0 given in %s on line 83
NULL

Warning: reset() expects exactly 1 parameter, 0 given in %s on line 84
NULL

Warning: next() expects exactly 1 parameter, 0 given in %s on line 85
NULL

Warning: key() expects exactly 1 parameter, 2 given in %s on line 89
NULL

Warning: current() expects exactly 1 parameter, 2 given in %s on line 90
NULL

Warning: reset() expects exactly 1 parameter, 2 given in %s on line 91
NULL

Warning: next() expects exactly 1 parameter, 2 given in %s on line 92
NULL

Warning: key() expects parameter 1 to be array, int given in %s on line 98
NULL

Warning: key() expects parameter 1 to be array, float given in %s on line 99
NULL

Warning: key() expects parameter 1 to be array, string given in %s on line 100
NULL

Warning: current() expects parameter 1 to be array, int given in %s on line 102
NULL

Warning: current() expects parameter 1 to be array, float given in %s on line 103
NULL

Warning: current() expects parameter 1 to be array, string given in %s on line 104
NULL

Warning: next() expects parameter 1 to be array, int given in %s on line 106
NULL

Warning: next() expects parameter 1 to be array, float given in %s on line 107
NULL

Warning: next() expects parameter 1 to be array, string given in %s on line 108
NULL

Warning: reset() expects parameter 1 to be array, int given in %s on line 110
NULL

Warning: reset() expects parameter 1 to be array, float given in %s on line 111
NULL

Warning: reset() expects parameter 1 to be array, string given in %s on line 112
NULL
Done
