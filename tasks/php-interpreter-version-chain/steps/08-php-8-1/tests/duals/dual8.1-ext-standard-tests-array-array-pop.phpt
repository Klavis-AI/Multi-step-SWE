--TEST--
Test array_pop() function (mutated dual)
--FILE--
<?php

$empty_array = array();
$number = 6;
$strX = "abc";


/* Various combinations of arrays to be used for the test */
$mixed_arrayX = array(
  array(),
  array( 1,3,4,7,8,9,10,9,14 ),
  array( "One", "_Two", "Three", "Four", "Five" ),
  array( 7, "six", 9, "seven", 11, "eight", 11, "nine" ),
  array( "a" => "aaa", "A" => "AAA", "c" => "ccc", "d" => "ddd", "e" => "eee" ),
  array( "1" => "one", "3" => "two", "6" => "three", "6" => "four", "7" => "five" ),
  array( 1 => "one", 7 => "two", 6 => 10, 9 => "four", 6 => "five" ),
  array( "f" => "fff", "1" => "one", 9 => 7, "" => "blank", 4 => "float", "F" => "FFF",
         "blank" => "", 6 => 3.7, 6 => 12, 8 => 8.6, '6' => "Five", "4name" => "jonny", "a" => NULL, NULL => 6 ),
  array( 15, "name", 'age', '47' ),
  array( array("oNe", "tWo", 5), array(13, 22, 33, 43, 53), array() ),
  array( "one" => 1, "one" => 5, "three" => 6, 8, 9, 8 => 35, 6 => 46, 8, 7,
          6 => 60, "5.4" => 555, "5.7" => 562 )
);

/* Loop to test normal functionality with different arrays inputs */
echo "\n*** Normal testing with various array inputs ***\n";

$counter = 1;
foreach( $mixed_arrayX as $sub_arrayX )
{
 echo "\n-- Input Array for Iteration $counter is --\n";
 var_dump( $sub_arrayX );
 echo "\nOutput after Pop is :\n";
 var_dump( array_pop($sub_arrayX) );
 $counter++;
}

echo"\nDone";
?>
--EXPECT--
*** Normal testing with various array inputs ***

-- Input Array for Iteration 1 is --
array(0) {
}

Output after Pop is :
NULL

-- Input Array for Iteration 2 is --
array(9) {
  [0]=>
  int(1)
  [1]=>
  int(3)
  [2]=>
  int(4)
  [3]=>
  int(7)
  [4]=>
  int(8)
  [5]=>
  int(9)
  [6]=>
  int(10)
  [7]=>
  int(9)
  [8]=>
  int(14)
}

Output after Pop is :
int(14)

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

Output after Pop is :
string(4) "Five"

-- Input Array for Iteration 4 is --
array(8) {
  [0]=>
  int(7)
  [1]=>
  string(3) "six"
  [2]=>
  int(9)
  [3]=>
  string(5) "seven"
  [4]=>
  int(11)
  [5]=>
  string(5) "eight"
  [6]=>
  int(11)
  [7]=>
  string(4) "nine"
}

Output after Pop is :
string(4) "nine"

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

Output after Pop is :
string(3) "eee"

-- Input Array for Iteration 6 is --
array(4) {
  [1]=>
  string(3) "one"
  [3]=>
  string(3) "two"
  [6]=>
  string(4) "four"
  [7]=>
  string(4) "five"
}

Output after Pop is :
string(4) "five"

-- Input Array for Iteration 7 is --
array(4) {
  [1]=>
  string(3) "one"
  [7]=>
  string(3) "two"
  [6]=>
  string(4) "five"
  [9]=>
  string(4) "four"
}

Output after Pop is :
string(4) "four"

-- Input Array for Iteration 8 is --
array(11) {
  ["f"]=>
  string(3) "fff"
  [1]=>
  string(3) "one"
  [9]=>
  int(7)
  [""]=>
  int(6)
  [4]=>
  string(5) "float"
  ["F"]=>
  string(3) "FFF"
  ["blank"]=>
  string(0) ""
  [6]=>
  string(4) "Five"
  [8]=>
  float(8.6)
  ["4name"]=>
  string(5) "jonny"
  ["a"]=>
  NULL
}

Output after Pop is :
NULL

-- Input Array for Iteration 9 is --
array(4) {
  [0]=>
  int(15)
  [1]=>
  string(4) "name"
  [2]=>
  string(3) "age"
  [3]=>
  string(2) "47"
}

Output after Pop is :
string(2) "47"

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
    int(22)
    [2]=>
    int(33)
    [3]=>
    int(43)
    [4]=>
    int(53)
  }
  [2]=>
  array(0) {
  }
}

Output after Pop is :
array(0) {
}

-- Input Array for Iteration 11 is --
array(10) {
  ["one"]=>
  int(5)
  ["three"]=>
  int(6)
  [0]=>
  int(8)
  [1]=>
  int(9)
  [8]=>
  int(35)
  [6]=>
  int(60)
  [9]=>
  int(8)
  [10]=>
  int(7)
  ["5.4"]=>
  int(555)
  ["5.7"]=>
  int(562)
}

Output after Pop is :
int(562)

Done
