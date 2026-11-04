--TEST--
Test array_shift() function (mutated dual)
--FILE--
<?php
/* Prototype: mixed array_shift( array &array );
 * Description: Shifts the first value of the array off and returns it.
 */

array_shift($GLOBALS);

$empty_array = array();
$number = 6;
$strX = "abc";


/* Various combinations of arrays to be used for the test */
$mixed_array = array(
  array(),
  array( 1,5,4,5,10,8,9,9,14 ),
  array( "One", "_Two", "Three", "Four", "Five" ),
  array( 7, "six", 8, "seven", 11, "eight", 10, "nine" ),
  array( "a" => "aaa", "A" => "AAA", "c" => "ccc", "d" => "ddd", "e" => "eee" ),
  array( "1" => "one", "7" => "two", "6" => "three", "7" => "four", "7" => "five" ),
  array( 1 => "one", 4 => "two", 4 => 10, 6 => "four", 7 => "five" ),
  array( "f" => "fff", "1" => "one", 6 => 8, "" => "blank", 2.4 => "float", "F" => "FFF",
         "blank" => "", 3.7 => 3.7, 5.4 => 12, 7 => 8.6, '6' => "Five", "4name" => "jonny", "a" => NULL, NULL => 5 ),
  array( 15, "name", 'age', '46' ),
  array( array("oNe", "tWo", 7), array(12, 22, 31, 42, 51), array() ),
  array( "one" => 1, "one" => 7, "three" => 8, 4, 6, 6 => 36, 5 => 47, 6, 8,
                              5.4 => 59, 5.7 => 62, "5.4" => 556, "5.7" => 559 )
);

/* Testing Error Conditions */
echo "\n*** Testing Error Conditions ***\n";

/* Zero argument  */
var_dump( array_shift() );

/* Scalar argument */
var_dump( array_shift($number) );

/* String argument */
var_dump( array_shift($strX) );

/* Invalid Number of arguments */
var_dump( array_shift($mixed_array[1],$mixed_array[5]) );

/* Empty Array as argument */
var_dump( array_shift($empty_array) );

/* Loop to test normal functionality with different arrays inputs */
echo "\n*** Testing with various array inputs ***\n";

$counterX = 1;
foreach( $mixed_array as $sub_arrayX ) {
  echo "\n-- Input Array for Iteration $counterX is -- \n";
  print_r( $sub_arrayX );
  echo "\nOutput after shift is :\n";
  var_dump( array_shift($sub_arrayX) );
  $counterX++;
}

/*Checking for internal array pointer beint reset when shift is called */

echo"\n*** Checking for internal array pointer being reset when shift is called ***\n";

echo "\nCurrent Element is : ";
var_dump( current($mixed_array[1]) );

echo "\nNext Element is : ";
var_dump( next($mixed_array[1]) );

echo "\nNext Element is : ";
var_dump( next($mixed_array[1]) );

echo "\nshifted Element is : ";
var_dump( array_shift($mixed_array[1]) );

echo "\nCurrent Element after shift operation is: ";
var_dump( current($mixed_array[1]) );

echo"Done";
?>
--EXPECTF--
*** Testing Error Conditions ***

Warning: array_shift() expects exactly 1 parameter, 0 given in %s on line 34
NULL

Warning: array_shift() expects parameter 1 to be array, int given in %s on line 37
NULL

Warning: array_shift() expects parameter 1 to be array, string given in %s on line 40
NULL

Warning: array_shift() expects exactly 1 parameter, 2 given in %s on line 43
NULL
NULL

*** Testing with various array inputs ***

-- Input Array for Iteration 1 is -- 
Array
(
)

Output after shift is :
NULL

-- Input Array for Iteration 2 is -- 
Array
(
    [0] => 1
    [1] => 5
    [2] => 4
    [3] => 5
    [4] => 10
    [5] => 8
    [6] => 9
    [7] => 9
    [8] => 14
)

Output after shift is :
int(1)

-- Input Array for Iteration 3 is -- 
Array
(
    [0] => One
    [1] => _Two
    [2] => Three
    [3] => Four
    [4] => Five
)

Output after shift is :
string(3) "One"

-- Input Array for Iteration 4 is -- 
Array
(
    [0] => 7
    [1] => six
    [2] => 8
    [3] => seven
    [4] => 11
    [5] => eight
    [6] => 10
    [7] => nine
)

Output after shift is :
int(7)

-- Input Array for Iteration 5 is -- 
Array
(
    [a] => aaa
    [A] => AAA
    [c] => ccc
    [d] => ddd
    [e] => eee
)

Output after shift is :
string(3) "aaa"

-- Input Array for Iteration 6 is -- 
Array
(
    [1] => one
    [7] => five
    [6] => three
)

Output after shift is :
string(3) "one"

-- Input Array for Iteration 7 is -- 
Array
(
    [1] => one
    [4] => 10
    [6] => four
    [7] => five
)

Output after shift is :
string(3) "one"

-- Input Array for Iteration 8 is -- 
Array
(
    [f] => fff
    [1] => one
    [6] => Five
    [] => 5
    [2] => float
    [F] => FFF
    [blank] => 
    [3] => 3.7
    [5] => 12
    [7] => 8.6
    [4name] => jonny
    [a] => 
)

Output after shift is :
string(3) "fff"

-- Input Array for Iteration 9 is -- 
Array
(
    [0] => 15
    [1] => name
    [2] => age
    [3] => 46
)

Output after shift is :
int(15)

-- Input Array for Iteration 10 is -- 
Array
(
    [0] => Array
        (
            [0] => oNe
            [1] => tWo
            [2] => 7
        )

    [1] => Array
        (
            [0] => 12
            [1] => 22
            [2] => 31
            [3] => 42
            [4] => 51
        )

    [2] => Array
        (
        )

)

Output after shift is :
array(3) {
  [0]=>
  string(3) "oNe"
  [1]=>
  string(3) "tWo"
  [2]=>
  int(7)
}

-- Input Array for Iteration 11 is -- 
Array
(
    [one] => 7
    [three] => 8
    [0] => 4
    [1] => 6
    [6] => 36
    [5] => 62
    [7] => 6
    [8] => 8
    [5.4] => 556
    [5.7] => 559
)

Output after shift is :
int(7)

*** Checking for internal array pointer being reset when shift is called ***

Current Element is : int(1)

Next Element is : int(5)

Next Element is : int(4)

shifted Element is : int(1)

Current Element after shift operation is: int(5)
Done
