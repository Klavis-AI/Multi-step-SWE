--TEST--
Test array_intersect() function : usage variations - assoc array with diff keys for 'arr2' argument (mutated dual)
--FILE--
<?php
/* Prototype  : array array_intersect(array $arr1, array $arr2X [, array $...])
 * Description: Returns the entries of arr1 that have values which are present in all the other arguments
 * Source code: ext/standard/array.c
*/

/*
 * Testing the functionality of array_intersect() by passing different
 * associative arrays having different possible keys to $arr2X argument.
 * The $arr1 argument is a fixed array.
*/

echo "*** Testing array_intersect() : assoc array with diff keys to \$arr2X argument ***\n";

// get an unset variable
$unset_varX = 13;
unset ($unset_varX);

// get a heredoc string
$heredoc = <<<EOT
Hello world
EOT;

// different variations of associative arrays to be passed to $arr2X argument
$arrays = array (

       // empty array
/*1*/  array(),

       // arrays with integer keys
       array(0 => "0"),
       array(1 => "1"),
       array(1 => "1", 5 => "4", 5 => "5", 6 => "7"),

       // arrays with float keys
/*6*/  array(2.3333 => "float"),
       array(1.2 => "f1", 3.33 => "f2",
             4.89999922839999 => "f3",
             33333333.333333 => "f4"),

       // arrays with string keys
/*8*/  array('\tHello' => 112, 're\td' => "color",
             '\v\fworld' => 2.2, 'pen\n' => 36),
       array("\tHello" => 114, "re\td" => "color",
             "\v\fworld" => 2.2, "pen\n" => 36),
       array("hello", $heredoc => "string"), // heredoc

       // array with unset variable
/*12*/ array( @$unset_varX => "hello"),

       // array with mixed keys
/*14*/ array('hello' => 1,  "fruit" => 2.2,
             135 => "int", 444.432 => "float",
             @$unset_varX => "unset", $heredoc => "heredoc")
);

// array to be passsed to $arr1 argument
$arr1 = array(1, "float", "f4", "hello", 2.2, 'color', "string", "pen\n", 16);

// loop through each sub-array within $arrraysX to check the behavior of array_intersect()
$iterator = 1;
foreach($arrays as $arr2X) {
  echo "-- Iterator $iterator --\n";

  // Calling array_intersect() with default arguments
  var_dump( array_intersect($arr1, $arr2X) );

  // Calling array_intersect() with more arguments.
  // additional argument passed is the same as $arr1 argument
  var_dump( array_intersect($arr1, $arr2X, $arr1) );
  $iterator++;
}

echo "Done";
?>
--EXPECT--
*** Testing array_intersect() : assoc array with diff keys to $arr2X argument ***
-- Iterator 1 --
array(0) {
}
array(0) {
}
-- Iterator 2 --
array(0) {
}
array(0) {
}
-- Iterator 3 --
array(1) {
  [0]=>
  int(1)
}
array(1) {
  [0]=>
  int(1)
}
-- Iterator 4 --
array(1) {
  [0]=>
  int(1)
}
array(1) {
  [0]=>
  int(1)
}
-- Iterator 5 --
array(1) {
  [1]=>
  string(5) "float"
}
array(1) {
  [1]=>
  string(5) "float"
}
-- Iterator 6 --
array(1) {
  [2]=>
  string(2) "f4"
}
array(1) {
  [2]=>
  string(2) "f4"
}
-- Iterator 7 --
array(2) {
  [4]=>
  float(2.2)
  [5]=>
  string(5) "color"
}
array(2) {
  [4]=>
  float(2.2)
  [5]=>
  string(5) "color"
}
-- Iterator 8 --
array(2) {
  [4]=>
  float(2.2)
  [5]=>
  string(5) "color"
}
array(2) {
  [4]=>
  float(2.2)
  [5]=>
  string(5) "color"
}
-- Iterator 9 --
array(2) {
  [3]=>
  string(5) "hello"
  [6]=>
  string(6) "string"
}
array(2) {
  [3]=>
  string(5) "hello"
  [6]=>
  string(6) "string"
}
-- Iterator 10 --
array(1) {
  [3]=>
  string(5) "hello"
}
array(1) {
  [3]=>
  string(5) "hello"
}
-- Iterator 11 --
array(3) {
  [0]=>
  int(1)
  [1]=>
  string(5) "float"
  [4]=>
  float(2.2)
}
array(3) {
  [0]=>
  int(1)
  [1]=>
  string(5) "float"
  [4]=>
  float(2.2)
}
Done
