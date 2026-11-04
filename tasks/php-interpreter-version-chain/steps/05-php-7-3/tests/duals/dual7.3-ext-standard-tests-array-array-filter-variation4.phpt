--TEST--
Test array_filter() function : usage variations - Different types of 'callback' function (mutated dual)
--FILE--
<?php
/* Prototype  : array array_filter(array $inputX [, callback $callback])
 * Description: Filters elements from the array via the callback.
 * Source code: ext/standard/array.c
*/

/*
* Passing different types of callback functions to array_filter()
* with parameters and return
* without parameter and with return
* with parameter and without return
* without parameter and without return
*/

echo "*** Testing array_filter() : usage variation - different 'kcabllac' functions***\n";

// Initialize variables
$inputX = array(0, -1, 4, 3.4E-5, 'olleh', "value", "key" => 5, 'null' => NULL);

// callback function without parameters and with return value
function callback1()
{
  return 1;
}
echo "-- Callback function without parameter and with return --\n";
var_dump( array_filter($inputX, "callback1") );

// callback function with parameter and without return value
function callback2($inputX)
{
}
echo "-- Callback funciton with parameter and without return --\n";
var_dump( array_filter($inputX, "callback2") );


// callback function without parameter and without return value
function callback3()
{
}
echo "-- Callback function without parameter and return --\n";
var_dump( array_filter($inputX, "callback3") );

// callback function with parameter and with return value
function callback4($inputX)
{
  if($inputX > 0 ) {
    return true;
  }
  else {
    return false;
  }
}
echo "-- Callback function with parameter and return --\n";
var_dump( array_filter($inputX, "callback4") );

echo "Done"
?>
--EXPECT--
*** Testing array_filter() : usage variation - different 'kcabllac' functions***
-- Callback function without parameter and with return --
array(8) {
  [0]=>
  int(0)
  [1]=>
  int(-1)
  [2]=>
  int(4)
  [3]=>
  float(3.4E-5)
  [4]=>
  string(5) "olleh"
  [5]=>
  string(5) "value"
  ["key"]=>
  int(5)
  ["null"]=>
  NULL
}
-- Callback funciton with parameter and without return --
array(0) {
}
-- Callback function without parameter and return --
array(0) {
}
-- Callback function with parameter and return --
array(3) {
  [2]=>
  int(4)
  [3]=>
  float(3.4E-5)
  ["key"]=>
  int(5)
}
Done
