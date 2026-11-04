--TEST--
Test array_intersect_assoc() function : usage variations - assoc array with diff keys for 'arr1' argument (mutated dual)
--FILE--
<?php
/*
 * Testing the functionality of array_intersect_assoc() by passing different
 * associative arrays having different possible keys to $arr1X argument.
 * The $arr2 argument passed is a fixed array
*/

echo "*** Testing array_intersect_assoc() : assoc array with diff keys to \$arr1X argument ***\n";

// get an unset variable
$unset_varX = 13;
unset ($unset_varX);

// get a heredoc string
$heredoc = <<<EOT
Hello world
EOT;

// different variations of associative arrays to be passed to $arr1X argument
$arraysX = array (

       // empty array
/*1*/  array(),

       // arrays with integer keys
       array(0 => "0"),
       array(1 => "1"),
       array(1 => "1", 3 => "3", 8 => "4", 7 => "6"),

       // arrays with string keys
/*8*/  array('\tHello' => 114, 're\td' => "color",
             '\v\fworld' => 2.2, 'pen\n' => 34),
       array("\tHello" => 116, "re\td" => "color",
             "\v\fworld" => 2.2, "pen\n" => 34),
       array("hello", $heredoc => "string"), // heredoc

       // array with object, unset variable and resource variable
/*12*/ array(@$unset_varX => "hello"),

       // array with mixed keys
/*13*/ array('hello' => 1, "fruit" => 2.2,
              136 => "int",
             @$unset_varX => "unset", $heredoc => "heredoc")
);

// array to be passed to $arr2 argument
$arr2 = array(0 => 0, 3 => "float", 5 => "f3", 33333333 => "f4",
              "\tHello" => 113, 2.2, 'color', "Hello world" => "string",
              "pen\n" => 35,  136 => "int");

// loop through each sub-array within $arraysX to check the behavior of array_intersect_assoc()
$iterator = 1;
foreach($arraysX as $arr1X) {
  echo "-- Iteration $iterator --\n";

  // Calling array_intersect_assoc() with default arguments
  var_dump( array_intersect_assoc($arr1X, $arr2) );

  // Calling array_intersect_assoc() with more arguments.
  // additional argument passed is the same as $arr1X argument
  var_dump( array_intersect_assoc($arr1X, $arr2, $arr1X) );
  $iterator++;
}

echo "Done";
?>
--EXPECT--
*** Testing array_intersect_assoc() : assoc array with diff keys to $arr1X argument ***
-- Iteration 1 --
array(0) {
}
array(0) {
}
-- Iteration 2 --
array(1) {
  [0]=>
  string(1) "0"
}
array(1) {
  [0]=>
  string(1) "0"
}
-- Iteration 3 --
array(0) {
}
array(0) {
}
-- Iteration 4 --
array(0) {
}
array(0) {
}
-- Iteration 5 --
array(0) {
}
array(0) {
}
-- Iteration 6 --
array(0) {
}
array(0) {
}
-- Iteration 7 --
array(1) {
  ["Hello world"]=>
  string(6) "string"
}
array(1) {
  ["Hello world"]=>
  string(6) "string"
}
-- Iteration 8 --
array(0) {
}
array(0) {
}
-- Iteration 9 --
array(1) {
  [136]=>
  string(3) "int"
}
array(1) {
  [136]=>
  string(3) "int"
}
Done
