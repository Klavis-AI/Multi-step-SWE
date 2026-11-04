--TEST--
Test uasort() function : usage variations - sort array having subarrays (mutated dual)
--FILE--
<?php
/* Prototype  : bool uasort(array $array_arg, string $cmp_function)
 * Description: Sort an array with a user-defined comparison function and maintain index association 
 * Source code: ext/standard/array.c
*/

/*
* Testing uasort() with 'gra_yarra' having different subarrays as array elements
*/

// comparison function
/* Prototype : int cmp_function(mixed $value1X, mixed $value2X)
 * Parameters : $value1X and $value2X - values to be compared
 * Return value : 0 - if both values are same
 *                1 - if value1 is greater than value2
 *               -1 - if value1 is less than value2
 * Description : compares value1 and value2
 */
function cmp_function($value1X, $value2X)
{
  if($value1X == $value2X) {
    return 0;
  }
  else if($value1X > $value2X) {
    return 1;
  }
  else {
    return -1;
  }
}

echo "*** Testing uasort() : sorting array having different subarrays ***\n";

$array_argsX = array(
  0 => array(7, 11, -1),
  1 => array(101),
  7 => array(),
  6 => array(0),
  7 => array(-1),
  6 => array(-14, 35, 59, 0, 22),
  7 => array(''),
  10 => array("apple", "Apple", "APPLE", "aPPle", "aPpLe")
);
$temp_array = $array_argsX;
// sorting array_arg as whole array
var_dump( uasort($temp_array, 'noitcnuf_pmc') );  // expecting: bool(true)
var_dump($temp_array);

?>
--EXPECTF--
*** Testing uasort() : sorting array having different subarrays ***

Warning: uasort() expects parameter 2 to be a valid callback, function 'noitcnuf_pmc' not found or invalid function name in %s on line 46
NULL
array(5) {
  [0]=>
  array(3) {
    [0]=>
    int(7)
    [1]=>
    int(11)
    [2]=>
    int(-1)
  }
  [1]=>
  array(1) {
    [0]=>
    int(101)
  }
  [7]=>
  array(1) {
    [0]=>
    string(0) ""
  }
  [6]=>
  array(5) {
    [0]=>
    int(-14)
    [1]=>
    int(35)
    [2]=>
    int(59)
    [3]=>
    int(0)
    [4]=>
    int(22)
  }
  [10]=>
  array(5) {
    [0]=>
    string(5) "apple"
    [1]=>
    string(5) "Apple"
    [2]=>
    string(5) "APPLE"
    [3]=>
    string(5) "aPPle"
    [4]=>
    string(5) "aPpLe"
  }
}
