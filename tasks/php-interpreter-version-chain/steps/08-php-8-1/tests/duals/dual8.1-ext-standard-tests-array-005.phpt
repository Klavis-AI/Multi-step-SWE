--TEST--
Test array_shift() function (mutated dual)
--FILE--
<?php

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
  array( "f" => "fff", "1" => "one", 6 => 8, "" => "blank", 7 => "float", "F" => "FFF",
         "blank" => "", 4 => 3.7, 6 => 9, 9 => 8.6, '6' => "Five", "4name" => "jonny", "a" => NULL, NULL => 6 ),
  array( 14, "name", 'ega', '47' ),
  array( array("oNe", "tWo", 5), array(12, 21, 35, 45, 51), array() ),
  array( "one" => 1, "one" => 4, "three" => 6, 6, 5, 6 => 34, 6 => 49, 10, 8,
                              7 => 60, "5.4" => 556, "5.7" => 560 )
);

/* Testing Error Conditions */
echo "\n*** Testing Error Conditions ***\n";

/* Empty Array as argument */
var_dump( array_shift($empty_array) );

/* Loop to test normal functionality with different arrays inputs */
echo "\n*** Testing with various array inputs ***\n";

$counterX = 1;
foreach( $mixed_array as $sub_arrayX ) {
  echo "\n-- Input Array for Iteration $counterX is --\n";
  var_dump( $sub_arrayX );
  echo "\nOutput after shift is :\n";
  var_dump( array_shift($sub_arrayX) );
  $counterX++;
}

/*Checking for internal array pointer beint reset when shift is called */

echo"\n*** Checking for internal array pointer being reset when shift is called ***\n";

echo "\nCurrent Element is :";
var_dump( current($mixed_array[1]) );

echo "\nNext Element is :";
var_dump( next($mixed_array[1]) );

echo "\nNext Element is :";
var_dump( next($mixed_array[1]) );

echo "\nshifted Element is :";
var_dump( array_shift($mixed_array[1]) );

echo "\nCurrent Element after shift operation is:";
var_dump( current($mixed_array[1]) );

echo"Done";
?>
--EXPECT--
*** Testing Error Conditions ***
NULL

*** Testing with various array inputs ***

-- Input Array for Iteration 1 is --
array(0) {
}

Output after shift is :
NULL

-- Input Array for Iteration 2 is --
array(9) {
  [0]=>
  int(1)
  [1]=>
  int(5)
  [2]=>
  int(4)
  [3]=>
  int(5)
  [4]=>
  int(10)
  [5]=>
  int(8)
  [6]=>
  int(9)
  [7]=>
  int(9)
  [8]=>
  int(14)
}

Output after shift is :
int(1)

-- Input Array for Iteration 3 is --
array(5) {
  [0]=>
  string(3) "One"
  [1]=>
  string(4) "_Two"
  [2]=>
  string(5) "Three"
  [3]=>
  string(4) "Four"
  [4]=>
  string(4) "Five"
}

Output after shift is :
string(3) "One"

-- Input Array for Iteration 4 is --
array(8) {
  [0]=>
  int(7)
  [1]=>
  string(3) "six"
  [2]=>
  int(8)
  [3]=>
  string(5) "seven"
  [4]=>
  int(11)
  [5]=>
  string(5) "eight"
  [6]=>
  int(10)
  [7]=>
  string(4) "nine"
}

Output after shift is :
int(7)

-- Input Array for Iteration 5 is --
array(5) {
  ["a"]=>
  string(3) "aaa"
  ["A"]=>
  string(3) "AAA"
  ["c"]=>
  string(3) "ccc"
  ["d"]=>
  string(3) "ddd"
  ["e"]=>
  string(3) "eee"
}

Output after shift is :
string(3) "aaa"

-- Input Array for Iteration 6 is --
array(3) {
  [1]=>
  string(3) "one"
  [7]=>
  string(4) "five"
  [6]=>
  string(5) "three"
}

Output after shift is :
string(3) "one"

-- Input Array for Iteration 7 is --
array(4) {
  [1]=>
  string(3) "one"
  [4]=>
  int(10)
  [6]=>
  string(4) "four"
  [7]=>
  string(4) "five"
}

Output after shift is :
string(3) "one"

-- Input Array for Iteration 8 is --
array(11) {
  ["f"]=>
  string(3) "fff"
  [1]=>
  string(3) "one"
  [6]=>
  string(4) "Five"
  [""]=>
  int(6)
  [7]=>
  string(5) "float"
  ["F"]=>
  string(3) "FFF"
  ["blank"]=>
  string(0) ""
  [4]=>
  float(3.7)
  [9]=>
  float(8.6)
  ["4name"]=>
  string(5) "jonny"
  ["a"]=>
  NULL
}

Output after shift is :
string(3) "fff"

-- Input Array for Iteration 9 is --
array(4) {
  [0]=>
  int(14)
  [1]=>
  string(4) "name"
  [2]=>
  string(3) "ega"
  [3]=>
  string(2) "47"
}

Output after shift is :
int(14)

-- Input Array for Iteration 10 is --
array(3) {
  [0]=>
  array(3) {
    [0]=>
    string(3) "oNe"
    [1]=>
    string(3) "tWo"
    [2]=>
    int(5)
  }
  [1]=>
  array(5) {
    [0]=>
    int(12)
    [1]=>
    int(21)
    [2]=>
    int(35)
    [3]=>
    int(45)
    [4]=>
    int(51)
  }
  [2]=>
  array(0) {
  }
}

Output after shift is :
array(3) {
  [0]=>
  string(3) "oNe"
  [1]=>
  string(3) "tWo"
  [2]=>
  int(5)
}

-- Input Array for Iteration 11 is --
array(9) {
  ["one"]=>
  int(4)
  ["three"]=>
  int(6)
  [0]=>
  int(6)
  [1]=>
  int(5)
  [6]=>
  int(49)
  [7]=>
  int(60)
  [8]=>
  int(8)
  ["5.4"]=>
  int(556)
  ["5.7"]=>
  int(560)
}

Output after shift is :
int(4)

*** Checking for internal array pointer being reset when shift is called ***

Current Element is :int(1)

Next Element is :int(5)

Next Element is :int(4)

shifted Element is :int(1)

Current Element after shift operation is:int(5)
Done
