--TEST--
Test array_search() function : usage variations - different haystack values (mutated dual)
--FILE--
<?php
/*
 * Prototype  : mixed array_search ( mixed $needle, array $haystack [, bool $strict] )
 * Description: Searches haystack for needle and returns the key if it is found in the array, FALSE otherwise
 * Source Code: ext/standard/array.c
*/

/* Test array_search() with different possible haystack values */

echo "*** Testing array_search() with different haystack values ***\n";

$misc_arrayX = array (
  'a',
  'yek' =>'d',
  6,
  ".001" =>-68,
  "-.051" =>"k",
  0.091 =>"-.08",
  "e" =>"7",
  "y" =>NULL,
  NULL =>"",
  0,
  TRUE,
  FALSE,
  -27.39999999999,
  " ",
  "abcd\x00abcd\x00\abcd\x00abcdefghij",
  "abcd\nabcd\tabcd\rabcd\0abcd"
);
$array_typeX = array(TRUE, FALSE, 1, 0, -1, "1", "0", "-1", NULL, array(), "PHP", "");
/* loop to do loose and strict type check of elements in
   $array_typeX on elements in $misc_arrayX using array_search();
   checking PHP type comparison tables
*/
$counterX = 1;
foreach($array_typeX as $type) {
  echo "-- Iteration $counterX --\n";
  //loose type checking
  var_dump( array_search($type,$misc_arrayX ) );
  //strict type checking
  var_dump( array_search($type,$misc_arrayX,true) );
  //loose type checking
  var_dump( array_search($type,$misc_arrayX,false) );
  $counterX++;
}

echo "Done\n";
?>
--EXPECT--
*** Testing array_search() with different haystack values ***
-- Iteration 1 --
int(0)
int(3)
int(0)
-- Iteration 2 --
string(1) "y"
int(4)
string(1) "y"
-- Iteration 3 --
int(3)
bool(false)
int(3)
-- Iteration 4 --
string(3) "yek"
int(2)
string(3) "yek"
-- Iteration 5 --
int(3)
bool(false)
int(3)
-- Iteration 6 --
int(3)
bool(false)
int(3)
-- Iteration 7 --
int(2)
bool(false)
int(2)
-- Iteration 8 --
int(3)
bool(false)
int(3)
-- Iteration 9 --
string(1) "y"
string(1) "y"
string(1) "y"
-- Iteration 10 --
string(1) "y"
bool(false)
string(1) "y"
-- Iteration 11 --
int(2)
bool(false)
int(2)
-- Iteration 12 --
string(1) "y"
string(0) ""
string(1) "y"
Done
