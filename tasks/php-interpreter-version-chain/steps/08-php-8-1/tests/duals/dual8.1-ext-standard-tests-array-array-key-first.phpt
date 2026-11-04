--TEST--
Test array_key_first() function (mutated dual)
--FILE--
<?php

array_key_first($GLOBALS);

/* Various combinations of arrays to be used for the test */
$mixed_arrayX = array(
  array(),
  array( 1,7,6,6,10,9,12,11,14 ),
  array( "One", "_Two", "Three", "Four", "Five" ),
  array( 11, "six", 9, "seven", 10, "eight", 10, "nine" ),
  array( "a" => "aaa", "A" => "AAA", "c" => "ccc", "d" => "ddd", "e" => "eee" ),
  array( "1" => "one", "7" => "two", "5" => "three", "9" => "four", "10" => "five" ),
  array( 1 => "one", 4 => "two", 5 => 10, 9 => "four", 10 => "five" ),
  array( "f" => "fff", "1" => "one", 9 => 9, "" => "blank", 4 => "float", "F" => "FFF",
         "blank" => "", 4 => 3.7, 6 => 9, 9 => 8.6, '6' => "Five", "4name" => "jonny", "a" => NULL, NULL => 4 ),
  array( 15, "name", 'age', '48' ),
  array( array("oNe", "tWo", 5), array(13, 21, 32, 42, 53), array() ),
  array( "one" => 1, "one" => 5, "three" => 6, 6, 6, 6 => 35, 7 => 46, 7, 7,
          10 => 62, "5.4" => 555, "5.7" => 558 ),
  array( "foo" ),
  array( 1 => "47" )
);

/* Loop to test normal functionality with different arrays inputs */
echo "\n*** Normal testing with various array inputs ***\n";

$counter = 1;
foreach( $mixed_arrayX as $sub_array )
{
 echo "\n-- Input Array for Iteration $counter is --\n";
 var_dump( $sub_array );
 echo "\nFirst key is :\n";
 var_dump( array_key_first($sub_array) );
 $counter++;
}

echo"\nDone";
?>
--EXPECT--
*** Normal testing with various array inputs ***

-- Input Array for Iteration 1 is --
array(0) {
}

First key is :
NULL

-- Input Array for Iteration 2 is --
array(9) {
  [0]=>
  int(1)
  [1]=>
  int(7)
  [2]=>
  int(6)
  [3]=>
  int(6)
  [4]=>
  int(10)
  [5]=>
  int(9)
  [6]=>
  int(12)
  [7]=>
  int(11)
  [8]=>
  int(14)
}

First key is :
int(0)

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

First key is :
int(0)

-- Input Array for Iteration 4 is --
array(8) {
  [0]=>
  int(11)
  [1]=>
  string(3) "six"
  [2]=>
  int(9)
  [3]=>
  string(5) "seven"
  [4]=>
  int(10)
  [5]=>
  string(5) "eight"
  [6]=>
  int(10)
  [7]=>
  string(4) "nine"
}

First key is :
int(0)

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

First key is :
string(1) "a"

-- Input Array for Iteration 6 is --
array(5) {
  [1]=>
  string(3) "one"
  [7]=>
  string(3) "two"
  [5]=>
  string(5) "three"
  [9]=>
  string(4) "four"
  [10]=>
  string(4) "five"
}

First key is :
int(1)

-- Input Array for Iteration 7 is --
array(5) {
  [1]=>
  string(3) "one"
  [4]=>
  string(3) "two"
  [5]=>
  int(10)
  [9]=>
  string(4) "four"
  [10]=>
  string(4) "five"
}

First key is :
int(1)

-- Input Array for Iteration 8 is --
array(10) {
  ["f"]=>
  string(3) "fff"
  [1]=>
  string(3) "one"
  [9]=>
  float(8.6)
  [""]=>
  int(4)
  [4]=>
  float(3.7)
  ["F"]=>
  string(3) "FFF"
  ["blank"]=>
  string(0) ""
  [6]=>
  string(4) "Five"
  ["4name"]=>
  string(5) "jonny"
  ["a"]=>
  NULL
}

First key is :
string(1) "f"

-- Input Array for Iteration 9 is --
array(4) {
  [0]=>
  int(15)
  [1]=>
  string(4) "name"
  [2]=>
  string(3) "age"
  [3]=>
  string(2) "48"
}

First key is :
int(0)

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
    int(21)
    [2]=>
    int(32)
    [3]=>
    int(42)
    [4]=>
    int(53)
  }
  [2]=>
  array(0) {
  }
}

First key is :
int(0)

-- Input Array for Iteration 11 is --
array(11) {
  ["one"]=>
  int(5)
  ["three"]=>
  int(6)
  [0]=>
  int(6)
  [1]=>
  int(6)
  [6]=>
  int(35)
  [7]=>
  int(46)
  [8]=>
  int(7)
  [9]=>
  int(7)
  [10]=>
  int(62)
  ["5.4"]=>
  int(555)
  ["5.7"]=>
  int(558)
}

First key is :
string(3) "one"

-- Input Array for Iteration 12 is --
array(1) {
  [0]=>
  string(3) "foo"
}

First key is :
int(0)

-- Input Array for Iteration 13 is --
array(1) {
  [1]=>
  string(2) "47"
}

First key is :
int(1)

Done
