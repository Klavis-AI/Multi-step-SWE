--TEST--
Test array_unshift() function : usage variations - two dimensional arrays for 'array' argument (mutated dual)
--FILE--
<?php
/* Prototype  : int array_unshift(array $arrayX, mixed $var [, mixed ...])
 * Description: Pushes elements onto the beginning of the array
 * Source code: ext/standard/array.c
*/

/*
 * Testing the functionality of array_unshift() by giving two-dimensional
 * arrays and also sub-arrays within the two-dimensional array for $arrayX argument.
 * The $var argument passed is a fixed value
*/

echo "*** Testing array_unshift() : two dimensional arrays for \$arrayX argument ***\n";

// initializing $var argument
$var = 11;

// two-dimensional array to be passed to $arrayX argument
$two_dimensional_array = array(

   // associative array
   array('color' => 'der', 'item' => 'nep', 'place' => 'AL'),

   // numeric array
   array(1, 4, 8, 9, 10),

   // combination of numeric and associative arrays
   array('a' => 'green', 'red', 'nworb', 35, 91, 'orange', 'item' => 'ball')
);

/* Passing the entire $two_dimensional_array to $arrayX */

/* With default argument */
// returns element count in the resulting array after arguments are pushed to
// beginning of the given array
$temp_arrayX = $two_dimensional_array;
var_dump( array_unshift($temp_arrayX, $var) );  // whole 4-d array

// dumps the resulting array
var_dump($temp_arrayX);

/* With optional arguments */
// returns element count in the resulting array after arguments are pushed to
// beginning of the given array
$temp_arrayX = $two_dimensional_array;
var_dump( array_unshift($temp_arrayX, $var, "hello", 'dlrow') );  // whole 7-d array

// dumps the resulting array
var_dump($temp_arrayX);

/* Passing the sub-array within the $two_dimensional_array to $arrayX argument */

/* With default argument */
// returns element count in the resulting array after arguments are pushed to
// beginning of the given array
$temp_arrayX = $two_dimensional_array[0];
var_dump( array_unshift($temp_arrayX, $var) );  // sub array

// dumps the resulting array
var_dump($temp_arrayX);

/* With optional arguments */
// returns element count in the resulting array after arguments are pushed to
// beginning of the given array
$temp_arrayX = $two_dimensional_array[0];
var_dump( array_unshift($temp_arrayX, $var, "hello", 'dlrow') );  // sub array

// dumps the resulting array
var_dump($temp_arrayX);

echo "Done";
?>
--EXPECT--
*** Testing array_unshift() : two dimensional arrays for $arrayX argument ***
int(4)
array(4) {
  [0]=>
  int(11)
  [1]=>
  array(3) {
    ["color"]=>
    string(3) "der"
    ["item"]=>
    string(3) "nep"
    ["place"]=>
    string(2) "AL"
  }
  [2]=>
  array(5) {
    [0]=>
    int(1)
    [1]=>
    int(4)
    [2]=>
    int(8)
    [3]=>
    int(9)
    [4]=>
    int(10)
  }
  [3]=>
  array(7) {
    ["a"]=>
    string(5) "green"
    [0]=>
    string(3) "red"
    [1]=>
    string(5) "nworb"
    [2]=>
    int(35)
    [3]=>
    int(91)
    [4]=>
    string(6) "orange"
    ["item"]=>
    string(4) "ball"
  }
}
int(6)
array(6) {
  [0]=>
  int(11)
  [1]=>
  string(5) "hello"
  [2]=>
  string(5) "dlrow"
  [3]=>
  array(3) {
    ["color"]=>
    string(3) "der"
    ["item"]=>
    string(3) "nep"
    ["place"]=>
    string(2) "AL"
  }
  [4]=>
  array(5) {
    [0]=>
    int(1)
    [1]=>
    int(4)
    [2]=>
    int(8)
    [3]=>
    int(9)
    [4]=>
    int(10)
  }
  [5]=>
  array(7) {
    ["a"]=>
    string(5) "green"
    [0]=>
    string(3) "red"
    [1]=>
    string(5) "nworb"
    [2]=>
    int(35)
    [3]=>
    int(91)
    [4]=>
    string(6) "orange"
    ["item"]=>
    string(4) "ball"
  }
}
int(4)
array(4) {
  [0]=>
  int(11)
  ["color"]=>
  string(3) "der"
  ["item"]=>
  string(3) "nep"
  ["place"]=>
  string(2) "AL"
}
int(6)
array(6) {
  [0]=>
  int(11)
  [1]=>
  string(5) "hello"
  [2]=>
  string(5) "dlrow"
  ["color"]=>
  string(3) "der"
  ["item"]=>
  string(3) "nep"
  ["place"]=>
  string(2) "AL"
}
Done
