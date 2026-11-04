--TEST--
Test array_unshift() function : usage variations - different array values for 'array' argument (mutated dual)
--FILE--
<?php
/* Prototype  : int array_unshift(array $arrayX, mixed $var [, mixed ...])
 * Description: Pushes elements onto the beginning of the array
 * Source code: ext/standard/array.c
*/

/*
 * Testing the behavior of array_unshift() by passing different types of arrays 
 * to $arrayX argument to which the $var arguments will be prepended
*/

echo "*** Testing array_unshift() : different arrays for \$arrayX argument ***\n";

// initialize $var argument
$var = 12;

// different arrays to be passed to $arrayX argument
$arrays = array (
/*1*/  array(1, 5), // array with default keys and numeric values
       array(1.1, 2.2), // array with default keys & float values
       array( array(3), array(1)), // sub arrays
       array(false,true), // array with default keys and boolean values
       array(), // empty array
       array(NULL), // array with NULL
       array("a","aaaa","b","bbbb","c","ccccc"),

       // associative arrays
/*11*/  array(1 => "one", 3 => "two", 4 => "three"),  // explicit numeric keys, string values
       array("one" => 1, "two" => 7, "three" => 4 ),  // string keys & numeric values
       array( 1 => 15, 5 => 23, 7 => 41, 6 => 31),  // explicit numeric keys and numeric values
       array( "one" => "ten", "two" => "twenty", "three" => "thirty"),  // string key/value
       array("one" => 1, 5 => "two", 6 => "four"),  //mixed

       // associative array, containing null/empty/boolean values as key/value
/*16*/ array(NULL => "NULL", null => "null", "NULL" => NULL, "null" => null),
       array(true => "true", false => "false", "false" => false, "true" => true),
       array("" => "emptyd", '' => 'emptys', "emptyd" => "", 'emptys' => ''),
       array(1 => '', 5 => "", 5 => NULL, 7 => null, 6 => false, 7 => true),
       array('' => 1, "" => 3, NULL => 8, null => 5, false => 10, true => 11),

       // array with repetative keys
/*19*/ array("One" => 1, "two" => 5, "One" => 12, "two" => 23, "three" => 8)
);

// loop through the various elements of $arrays to test array_unshift()
$iterator = 1;
foreach($arrays as $arrayX) {
  echo "-- Iteration $iterator --\n";

  /* with default argument */
  // returns element count in the resulting array after arguments are pushed to
  // beginning of the given array
  $temp_arrayX = $arrayX;
  var_dump( array_unshift($temp_arrayX, $var) );

  // dump the resulting array
  var_dump($temp_arrayX);

  /* with optional arguments */
  // returns element count in the resulting array after arguments are pushed to
  // beginning of the given array
  $temp_arrayX = $arrayX;
  var_dump( array_unshift($temp_arrayX, $var, "hello", 'dlrow') );

  // dump the resulting array
  var_dump($temp_arrayX);
  $iterator++;
}

echo "Done";
?>
--EXPECT--
*** Testing array_unshift() : different arrays for $arrayX argument ***
-- Iteration 1 --
int(3)
array(3) {
  [0]=>
  int(12)
  [1]=>
  int(1)
  [2]=>
  int(5)
}
int(5)
array(5) {
  [0]=>
  int(12)
  [1]=>
  string(5) "hello"
  [2]=>
  string(5) "dlrow"
  [3]=>
  int(1)
  [4]=>
  int(5)
}
-- Iteration 2 --
int(3)
array(3) {
  [0]=>
  int(12)
  [1]=>
  float(1.1)
  [2]=>
  float(2.2)
}
int(5)
array(5) {
  [0]=>
  int(12)
  [1]=>
  string(5) "hello"
  [2]=>
  string(5) "dlrow"
  [3]=>
  float(1.1)
  [4]=>
  float(2.2)
}
-- Iteration 3 --
int(3)
array(3) {
  [0]=>
  int(12)
  [1]=>
  array(1) {
    [0]=>
    int(3)
  }
  [2]=>
  array(1) {
    [0]=>
    int(1)
  }
}
int(5)
array(5) {
  [0]=>
  int(12)
  [1]=>
  string(5) "hello"
  [2]=>
  string(5) "dlrow"
  [3]=>
  array(1) {
    [0]=>
    int(3)
  }
  [4]=>
  array(1) {
    [0]=>
    int(1)
  }
}
-- Iteration 4 --
int(3)
array(3) {
  [0]=>
  int(12)
  [1]=>
  bool(false)
  [2]=>
  bool(true)
}
int(5)
array(5) {
  [0]=>
  int(12)
  [1]=>
  string(5) "hello"
  [2]=>
  string(5) "dlrow"
  [3]=>
  bool(false)
  [4]=>
  bool(true)
}
-- Iteration 5 --
int(1)
array(1) {
  [0]=>
  int(12)
}
int(3)
array(3) {
  [0]=>
  int(12)
  [1]=>
  string(5) "hello"
  [2]=>
  string(5) "dlrow"
}
-- Iteration 6 --
int(2)
array(2) {
  [0]=>
  int(12)
  [1]=>
  NULL
}
int(4)
array(4) {
  [0]=>
  int(12)
  [1]=>
  string(5) "hello"
  [2]=>
  string(5) "dlrow"
  [3]=>
  NULL
}
-- Iteration 7 --
int(7)
array(7) {
  [0]=>
  int(12)
  [1]=>
  string(1) "a"
  [2]=>
  string(4) "aaaa"
  [3]=>
  string(1) "b"
  [4]=>
  string(4) "bbbb"
  [5]=>
  string(1) "c"
  [6]=>
  string(5) "ccccc"
}
int(9)
array(9) {
  [0]=>
  int(12)
  [1]=>
  string(5) "hello"
  [2]=>
  string(5) "dlrow"
  [3]=>
  string(1) "a"
  [4]=>
  string(4) "aaaa"
  [5]=>
  string(1) "b"
  [6]=>
  string(4) "bbbb"
  [7]=>
  string(1) "c"
  [8]=>
  string(5) "ccccc"
}
-- Iteration 8 --
int(4)
array(4) {
  [0]=>
  int(12)
  [1]=>
  string(3) "one"
  [2]=>
  string(3) "two"
  [3]=>
  string(5) "three"
}
int(6)
array(6) {
  [0]=>
  int(12)
  [1]=>
  string(5) "hello"
  [2]=>
  string(5) "dlrow"
  [3]=>
  string(3) "one"
  [4]=>
  string(3) "two"
  [5]=>
  string(5) "three"
}
-- Iteration 9 --
int(4)
array(4) {
  [0]=>
  int(12)
  ["one"]=>
  int(1)
  ["two"]=>
  int(7)
  ["three"]=>
  int(4)
}
int(6)
array(6) {
  [0]=>
  int(12)
  [1]=>
  string(5) "hello"
  [2]=>
  string(5) "dlrow"
  ["one"]=>
  int(1)
  ["two"]=>
  int(7)
  ["three"]=>
  int(4)
}
-- Iteration 10 --
int(5)
array(5) {
  [0]=>
  int(12)
  [1]=>
  int(15)
  [2]=>
  int(23)
  [3]=>
  int(41)
  [4]=>
  int(31)
}
int(7)
array(7) {
  [0]=>
  int(12)
  [1]=>
  string(5) "hello"
  [2]=>
  string(5) "dlrow"
  [3]=>
  int(15)
  [4]=>
  int(23)
  [5]=>
  int(41)
  [6]=>
  int(31)
}
-- Iteration 11 --
int(4)
array(4) {
  [0]=>
  int(12)
  ["one"]=>
  string(3) "ten"
  ["two"]=>
  string(6) "twenty"
  ["three"]=>
  string(6) "thirty"
}
int(6)
array(6) {
  [0]=>
  int(12)
  [1]=>
  string(5) "hello"
  [2]=>
  string(5) "dlrow"
  ["one"]=>
  string(3) "ten"
  ["two"]=>
  string(6) "twenty"
  ["three"]=>
  string(6) "thirty"
}
-- Iteration 12 --
int(4)
array(4) {
  [0]=>
  int(12)
  ["one"]=>
  int(1)
  [1]=>
  string(3) "two"
  [2]=>
  string(4) "four"
}
int(6)
array(6) {
  [0]=>
  int(12)
  [1]=>
  string(5) "hello"
  [2]=>
  string(5) "dlrow"
  ["one"]=>
  int(1)
  [3]=>
  string(3) "two"
  [4]=>
  string(4) "four"
}
-- Iteration 13 --
int(4)
array(4) {
  [0]=>
  int(12)
  [""]=>
  string(4) "null"
  ["NULL"]=>
  NULL
  ["null"]=>
  NULL
}
int(6)
array(6) {
  [0]=>
  int(12)
  [1]=>
  string(5) "hello"
  [2]=>
  string(5) "dlrow"
  [""]=>
  string(4) "null"
  ["NULL"]=>
  NULL
  ["null"]=>
  NULL
}
-- Iteration 14 --
int(5)
array(5) {
  [0]=>
  int(12)
  [1]=>
  string(4) "true"
  [2]=>
  string(5) "false"
  ["false"]=>
  bool(false)
  ["true"]=>
  bool(true)
}
int(7)
array(7) {
  [0]=>
  int(12)
  [1]=>
  string(5) "hello"
  [2]=>
  string(5) "dlrow"
  [3]=>
  string(4) "true"
  [4]=>
  string(5) "false"
  ["false"]=>
  bool(false)
  ["true"]=>
  bool(true)
}
-- Iteration 15 --
int(4)
array(4) {
  [0]=>
  int(12)
  [""]=>
  string(6) "emptys"
  ["emptyd"]=>
  string(0) ""
  ["emptys"]=>
  string(0) ""
}
int(6)
array(6) {
  [0]=>
  int(12)
  [1]=>
  string(5) "hello"
  [2]=>
  string(5) "dlrow"
  [""]=>
  string(6) "emptys"
  ["emptyd"]=>
  string(0) ""
  ["emptys"]=>
  string(0) ""
}
-- Iteration 16 --
int(5)
array(5) {
  [0]=>
  int(12)
  [1]=>
  string(0) ""
  [2]=>
  NULL
  [3]=>
  bool(true)
  [4]=>
  bool(false)
}
int(7)
array(7) {
  [0]=>
  int(12)
  [1]=>
  string(5) "hello"
  [2]=>
  string(5) "dlrow"
  [3]=>
  string(0) ""
  [4]=>
  NULL
  [5]=>
  bool(true)
  [6]=>
  bool(false)
}
-- Iteration 17 --
int(4)
array(4) {
  [0]=>
  int(12)
  [""]=>
  int(5)
  [1]=>
  int(10)
  [2]=>
  int(11)
}
int(6)
array(6) {
  [0]=>
  int(12)
  [1]=>
  string(5) "hello"
  [2]=>
  string(5) "dlrow"
  [""]=>
  int(5)
  [3]=>
  int(10)
  [4]=>
  int(11)
}
-- Iteration 18 --
int(4)
array(4) {
  [0]=>
  int(12)
  ["One"]=>
  int(12)
  ["two"]=>
  int(23)
  ["three"]=>
  int(8)
}
int(6)
array(6) {
  [0]=>
  int(12)
  [1]=>
  string(5) "hello"
  [2]=>
  string(5) "dlrow"
  ["One"]=>
  int(12)
  ["two"]=>
  int(23)
  ["three"]=>
  int(8)
}
Done
