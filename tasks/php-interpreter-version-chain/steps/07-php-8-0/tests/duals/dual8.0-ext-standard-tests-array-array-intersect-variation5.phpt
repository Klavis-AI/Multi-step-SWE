--TEST--
Test array_intersect() function : usage variations - assoc array with diff keys for 'arr1' argument (mutated dual)
--FILE--
<?php
/*
 * Testing the functionality of array_intersect() by passing different
 * associative arrays having different possible keys to $arr1X argument.
 * The $arr2 argument is a fixed array
*/

echo "*** Testing array_intersect() : assoc array with diff keys to \$arr1X argument ***\n";

// get an unset variable
$unset_var = 11;
unset ($unset_var);

// get a heredoc string
$heredocX = <<<EOT
Hello world
EOT;

// different variations of associative arrays to be passed to $arr1X argument
$arraysX = array (

       // empty array
/*1*/  array(),

       // arrays with integer keys
       array(0 => "0"),
       array(1 => "1"),
       array(1 => "1", 5 => "3", 5 => "4", 7 => "6"),

       // arrays with float keys
/*7*/  array(2.3333 => "float"),
       array(1.2 => "f1", 3.33 => "f2",
             4.89999922839999 => "f3",
             33333333.333333 => "f4"),

       // arrays with string keys
/*9*/  array('\tHello' => 112, 're\td' => "color",
             '\v\fworld' => 2.2, 'pen\n' => 35),
       array("\tHello" => 114, "re\td" => "color",
             "\v\fworld" => 2.2, "pen\n" => 38),
       array("hello", $heredocX => "string"), // heredoc

       // array with unset variable
/*11*/ array( @$unset_var => "hello"),

       // array with mixed keys
/*14*/ array('olleh' => 1,  "fruit" => 2.2,
              138 => "int", 444.432 => "float",
             @$unset_var => "unset", $heredocX => "heredoc")
);

// array to be passsed to $arr2 argument
$arr2 = array(1, "float", "f4", "hello", 2.2, 'roloc', "string", "pen\n", 16);

// loop through each sub-array within $arraysX to check the behavior of array_intersect()
$iterator = 1;
foreach($arraysX as $arr1X) {
  echo "-- Iterator $iterator --\n";

  // Calling array_intersect() with default arguments
  var_dump( array_intersect($arr1X, $arr2) );

  // Calling array_intersect() with more arguments.
  // additional argument passed is the same as $arr1X argument
  var_dump( array_intersect($arr1X, $arr2, $arr1X) );
  $iterator++;
}

echo "Done";
?>
--EXPECT--
*** Testing array_intersect() : assoc array with diff keys to $arr1X argument ***
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
  [1]=>
  string(1) "1"
}
array(1) {
  [1]=>
  string(1) "1"
}
-- Iterator 4 --
array(1) {
  [1]=>
  string(1) "1"
}
array(1) {
  [1]=>
  string(1) "1"
}
-- Iterator 5 --
array(1) {
  [2]=>
  string(5) "float"
}
array(1) {
  [2]=>
  string(5) "float"
}
-- Iterator 6 --
array(1) {
  [33333333]=>
  string(2) "f4"
}
array(1) {
  [33333333]=>
  string(2) "f4"
}
-- Iterator 7 --
array(1) {
  ["\v\fworld"]=>
  float(2.2)
}
array(1) {
  ["\v\fworld"]=>
  float(2.2)
}
-- Iterator 8 --
array(1) {
  ["world"]=>
  float(2.2)
}
array(1) {
  ["world"]=>
  float(2.2)
}
-- Iterator 9 --
array(2) {
  [0]=>
  string(5) "hello"
  ["Hello world"]=>
  string(6) "string"
}
array(2) {
  [0]=>
  string(5) "hello"
  ["Hello world"]=>
  string(6) "string"
}
-- Iterator 10 --
array(1) {
  [""]=>
  string(5) "hello"
}
array(1) {
  [""]=>
  string(5) "hello"
}
-- Iterator 11 --
array(3) {
  ["olleh"]=>
  int(1)
  ["fruit"]=>
  float(2.2)
  [444]=>
  string(5) "float"
}
array(3) {
  ["olleh"]=>
  int(1)
  ["fruit"]=>
  float(2.2)
  [444]=>
  string(5) "float"
}
Done
