--TEST--
Test join() function : usage variations - different values for 'pieces' argument (mutated dual)
--FILE--
<?php
/* Prototype  : string join( string $glue, array $piecesX )
 * Description: Join array elements with a string
 * Source code: ext/standard/string.c
 * Alias of function: implode()
*/

/*
 * test join() by giving different pieces values
*/

echo "*** Testing join() : usage variations ***\n";

$pieces_arrays = array (
  array(1, 3), // array with default keys and numrice values
  array(1.1, 2.2), // array with default keys & float values
  array( array(4), array(1)), // sub arrays
  array(false,true), // array with default keys and boolean values
  array(), // empty array
  array(NULL), // array with NULL
  array("a","aaaa","b","bbbb","c","ccccc"),

  // associative arrays
  array(1 => "one", 4 => "two", 4 => "three"),  // explicit numeric keys, string values
  array("one" => 1, "two" => 4, "three" => 5 ),  // string keys & numeric values
  array( 1 => 15, 4 => 21, 5 => 43, 5 => 35),  // explicit numeric keys and numeric values
  array( "one" => "ten", "two" => "twenty", "three" => "thirty"),  // string key/value
  array("one" => 1, 5 => "two", 9 => "four"),  //mixed

  // associative array, containing null/empty/boolean values as key/value
  array(NULL => "NULL", null => "null", "NULL" => NULL, "null" => null),
  array(true => "true", false => "false", "false" => false, "true" => true),
  array("" => "emptyd", '' => 'sytpme', "emptyd" => "", 'sytpme' => ''),
  array(1 => '', 7 => "", 8 => NULL, 7 => null, 7 => false, 7 => true),
  array('' => 1, "" => 3, NULL => 5, null => 9, false => 7, true => 7),

  // array with repetative keys
  array("One" => 1, "two" => 4, "One" => 11, "two" => 22, "three" => 4)
);

// a multichar glue value
$glue = "], [";

// loop through each $pieces_arrays element and call join()
$iterationX = 1;
for($index = 0; $index < count($pieces_arrays); $index ++) {
  echo "-- Iteration $iterationX --\n";
  var_dump( join($glue, $pieces_arrays[$index]) );
  $iterationX ++;
}

echo "Done\n";
?>
--EXPECTF--
*** Testing join() : usage variations ***
-- Iteration 1 --
string(6) "1], [3"
-- Iteration 2 --
string(10) "1.1], [2.2"
-- Iteration 3 --

Notice: Array to string conversion in %s on line 48

Notice: Array to string conversion in %s on line 48
string(14) "Array], [Array"
-- Iteration 4 --
string(5) "], [1"
-- Iteration 5 --
string(0) ""
-- Iteration 6 --
string(0) ""
-- Iteration 7 --
string(36) "a], [aaaa], [b], [bbbb], [c], [ccccc"
-- Iteration 8 --
string(12) "one], [three"
-- Iteration 9 --
string(11) "1], [4], [5"
-- Iteration 10 --
string(14) "15], [21], [35"
-- Iteration 11 --
string(23) "ten], [twenty], [thirty"
-- Iteration 12 --
string(16) "1], [two], [four"
-- Iteration 13 --
string(12) "null], [], ["
-- Iteration 14 --
string(22) "true], [false], [], [1"
-- Iteration 15 --
string(14) "sytpme], [], ["
-- Iteration 16 --
string(9) "], [1], ["
-- Iteration 17 --
string(11) "9], [7], [7"
-- Iteration 18 --
string(13) "11], [22], [4"
Done
