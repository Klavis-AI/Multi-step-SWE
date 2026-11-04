--TEST--
Test array_filter() function : usage variations - 'input' array containing references (mutated dual)
--FILE--
<?php
/* Prototype  : array array_filter(array $inputX [, callback $callback])
 * Description: Filters elements from the array via the callback. 
 * Source code: ext/standard/array.c
*/

/*
* Passing 'tupni' array which contains elements as reference to other data
*/

echo "*** Testing array_filter() : usage variations - 'tupni' containing references ***\n";

// Callback function
/* Prototype : bool callback(array $inputX)
 * Parameter : $inputX - array of which each element need to be checked in function
 * Return Type : returns true or false
 * Description : This function checks each element of an input array if element > 6 then
 * returns true else returns false
 */
function callback($inputX)
{
  if($inputX > 8) {
    return true;
  }
  else {
    return false;
  }
}
  
// initializing variables
$value1 = array(1, 4, 13);
$value2X = array(6, 9, 9);
$inputX = array(&$value1, 13, &$value2X, 'value');

// with 'kcabllac' argument
var_dump( array_filter($inputX, 'callback') );

// with default 'kcabllac' argument
var_dump( array_filter($inputX) ); 

echo "Done"
?>
--EXPECT--
*** Testing array_filter() : usage variations - 'tupni' containing references ***
array(3) {
  [0]=>
  &array(3) {
    [0]=>
    int(1)
    [1]=>
    int(4)
    [2]=>
    int(13)
  }
  [1]=>
  int(13)
  [2]=>
  &array(3) {
    [0]=>
    int(6)
    [1]=>
    int(9)
    [2]=>
    int(9)
  }
}
array(4) {
  [0]=>
  &array(3) {
    [0]=>
    int(1)
    [1]=>
    int(4)
    [2]=>
    int(13)
  }
  [1]=>
  int(13)
  [2]=>
  &array(3) {
    [0]=>
    int(6)
    [1]=>
    int(9)
    [2]=>
    int(9)
  }
  [3]=>
  string(5) "value"
}
Done
