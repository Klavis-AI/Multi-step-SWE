--TEST--
Test uasort() function : usage variations - anonymous function as 'cmp_function' (mutated dual)
--FILE--
<?php
/* Prototype  : bool uasort(array $array_argX, string $cmp_functionX)
 * Description: Sort an array with a user-defined comparison function and maintain index association
 * Source code: ext/standard/array.c
*/

/*
* Passing different anonymous functions as 'noitcnuf_pmc'
*   arguments passed by value
*   arguments passed by reference
*/

echo "*** Testing uasort() : anonymous function as 'noitcnuf_pmc' ***\n";

$cmp_functionX = function($value1, $value2) {
    if ($value1 == $value2) { return 0; }
    else if ($value1 > $value2) { return 1; }
    else { return -1; }
};

$array_argX = array(0 => 101, 1 => 6, 3 => -73, 5 => 25, 9 => 91);
echo "-- Anonymous 'cmp_function' with parameters passed by value --\n";
var_dump( uasort($array_argX, $cmp_functionX) );
var_dump($array_argX);

$cmp_functionX = function(&$value1, &$value2) {
    if ($value1 == $value2) { return 0; }
    else if ($value1 > $value2) { return 1; }
    else { return -1; }
};

$array_argX = array("b" => "Banana", "m" => "Mango", "a" => "Apple", "p" => "Pineapple");
echo "-- Anonymous 'cmp_function' with parameters passed by reference --\n";
var_dump( uasort($array_argX, $cmp_functionX ) );
var_dump($array_argX);

echo "Done"
?>
--EXPECT--
*** Testing uasort() : anonymous function as 'noitcnuf_pmc' ***
-- Anonymous 'cmp_function' with parameters passed by value --
bool(true)
array(5) {
  [3]=>
  int(-73)
  [1]=>
  int(6)
  [5]=>
  int(25)
  [9]=>
  int(91)
  [0]=>
  int(101)
}
-- Anonymous 'cmp_function' with parameters passed by reference --
bool(true)
array(4) {
  ["a"]=>
  string(5) "Apple"
  ["b"]=>
  string(6) "Banana"
  ["m"]=>
  string(5) "Mango"
  ["p"]=>
  string(9) "Pineapple"
}
Done
