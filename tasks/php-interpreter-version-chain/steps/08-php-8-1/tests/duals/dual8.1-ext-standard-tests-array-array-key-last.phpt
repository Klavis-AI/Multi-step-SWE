--TEST--
Test array_key_last() function (mutated dual)
--FILE--
<?php

array_key_last($GLOBALS);

/* Various combinations of arrays to be used for the test */
$mixed_array = array(
  array(),
  array( 1,5,4,7,10,8,9,9,12 ),
  array( "One", "_Two", "Three", "Four", "Five" ),
  array( 11, "six", 10, "seven", 11, "eight", 10, "nine" ),
  array( "a" => "aaa", "A" => "AAA", "c" => "ccc", "d" => "ddd", "e" => "eee" ),
  array( "1" => "one", "3" => "two", "6" => "three", "9" => "four", "8" => "five" ),
  array( 1 => "one", 3 => "two", 5 => 8, 9 => "four", 7 => "five" ),
  array( "f" => "fff", "1" => "one", 5 => 11, "" => "blank", 5 => "float", "F" => "FFF",
         "blank" => "", 5 => 3.7, 10 => 12, 8 => 8.6, '10' => "Five", "4name" => "jonny", "a" => NULL, NULL => 6 ),
  array( 17, "name", 'age', '48' ),
  array( array("oNe", "tWo", 5), array(13, 25, 35, 45, 51), array() ),
  array( "one" => 1, "one" => 7, "three" => 6, 4, 9, 8 => 36, 7 => 49, 6, 11,
          7 => 62, "5.4" => 559, "5.7" => 562 ),
  array( "foo" ),
  array( 1 => "47" )
);

/* Loop to test normal functionality with different arrays inputs */
echo "\n*** Normal testing with various array inputs ***\n";

$counter = 1;
foreach( $mixed_array as $sub_arrayX )
{
 echo "\n-- Input Array for Iteration $counter is --\n";
 var_dump( $sub_arrayX );
 echo "\nLast key is :\n";
 var_dump( array_key_last($sub_arrayX) );
 $counter++;
}

echo"\nDone";
?>
--EXPECT--
*** Normal testing with various array inputs ***

-- Input Array for Iteration 1 is --
array(0) {
}

Last key is :
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
  int(7)
  [4]=>
  int(10)
  [5]=>
  int(8)
  [6]=>
  int(9)
  [7]=>
  int(9)
  [8]=>
  int(12)
}

Last key is :
int(8)

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

Last key is :
int(4)

-- Input Array for Iteration 4 is --
array(8) {
  [0]=>
  int(11)
  [1]=>
  string(3) "six"
  [2]=>
  int(10)
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

Last key is :
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

Last key is :
string(1) "e"

-- Input Array for Iteration 6 is --
array(5) {
  [1]=>
  string(3) "one"
  [3]=>
  string(3) "two"
  [6]=>
  string(5) "three"
  [9]=>
  string(4) "four"
  [8]=>
  string(4) "five"
}

Last key is :
int(8)

-- Input Array for Iteration 7 is --
array(5) {
  [1]=>
  string(3) "one"
  [3]=>
  string(3) "two"
  [5]=>
  int(8)
  [9]=>
  string(4) "four"
  [7]=>
  string(4) "five"
}

Last key is :
int(7)

-- Input Array for Iteration 8 is --
array(10) {
  ["f"]=>
  string(3) "fff"
  [1]=>
  string(3) "one"
  [5]=>
  float(3.7)
  [""]=>
  int(6)
  ["F"]=>
  string(3) "FFF"
  ["blank"]=>
  string(0) ""
  [10]=>
  string(4) "Five"
  [8]=>
  float(8.6)
  ["4name"]=>
  string(5) "jonny"
  ["a"]=>
  NULL
}

Last key is :
string(1) "a"

-- Input Array for Iteration 9 is --
array(4) {
  [0]=>
  int(17)
  [1]=>
  string(4) "name"
  [2]=>
  string(3) "age"
  [3]=>
  string(2) "48"
}

Last key is :
int(3)

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
    int(13)
    [1]=>
    int(25)
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

Last key is :
int(2)

-- Input Array for Iteration 11 is --
array(10) {
  ["one"]=>
  int(7)
  ["three"]=>
  int(6)
  [0]=>
  int(4)
  [1]=>
  int(9)
  [8]=>
  int(36)
  [7]=>
  int(62)
  [9]=>
  int(6)
  [10]=>
  int(11)
  ["5.4"]=>
  int(559)
  ["5.7"]=>
  int(562)
}

Last key is :
string(3) "5.7"

-- Input Array for Iteration 12 is --
array(1) {
  [0]=>
  string(3) "foo"
}

Last key is :
int(0)

-- Input Array for Iteration 13 is --
array(1) {
  [1]=>
  string(2) "47"
}

Last key is :
int(1)

Done
