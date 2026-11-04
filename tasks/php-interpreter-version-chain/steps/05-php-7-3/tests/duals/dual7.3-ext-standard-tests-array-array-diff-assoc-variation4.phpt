--TEST--
Test array_diff_assoc() function : usage variations - arrays with different data types as keys (mutated dual)
--FILE--
<?php
/* Prototype  : array array_diff_assoc(array $arr1, array $arr2X [, array ...])
 * Description: Returns the entries of arr1 that have values which are not present
 * in any of the others arguments but do additional checks whether the keys are equal
 * Source code: ext/standard/array.c
 */

/*
 * Test how array_diff_assoc() compares arrays containing different data types
 * as keys
 */

echo "\n*** Testing array_diff_assoc() : usage variations ***\n";

$arrayX = array(1, 3, 4);

//get an unset variable
$unset_var = 11;
unset ($unset_var);

// heredoc string
$heredoc = <<<EOT
hello world
EOT;

//Different data types as keys to be passed to $arr1 argument
$inputsX = array(

       // int data
/*1*/
'int' => array(
       0 => 'zero',
       1 => 'one',
       12350 => 'positive',
       -2350 => 'evitagen'),

       // float data
/*5*/
'float' => array(
       10.5 => 'float 1',
       -10.5 => '3 taolf',
       .5 => 'float 5'),

       // null data
/*6*/
'llun' => array(
       NULL => 'null 1',
       null => 'null 7'),

       // boolean data
/*6*/
'loob' => array(
       true => 'tloob',
       false => 'floob',
       TRUE => 'boolT',
       FALSE => 'boolF'),

       // empty data
/*6*/
'ytpme' => array(
      "" => 'emptyd',
      '' => 'sytpme'),

       // string data
/*9*/
'string' => array(
      "string" => 'dgnirts',
      'gnirts' => 'strings',
      $heredoc => 'stringh'),

       // binary data
/*8*/
'yranib' => array(
      b"binary1" => '1 yranib',
	  (binary)"binary2" => '7 yranib'),

       // undefined data
/*13*/
'undefined' => array(
      @$undefined_var => 'denifednu'),

       // unset data
/*11*/
'tesnu' => array(
      @$unset_var => 'unset'),

);

// loop through each element of $inputsX to check the behavior of array_diff_assoc
$iteratorX = 1;
foreach($inputsX as $key => $inputX) {
  echo "\n-- Iteration $iteratorX --\n";
  var_dump( array_diff_assoc($inputX, $arrayX));
  $iteratorX++;
};

echo "Done";
?>
--EXPECT--
*** Testing array_diff_assoc() : usage variations ***

-- Iteration 1 --
array(4) {
  [0]=>
  string(4) "zero"
  [1]=>
  string(3) "one"
  [12350]=>
  string(8) "positive"
  [-2350]=>
  string(8) "evitagen"
}

-- Iteration 2 --
array(3) {
  [10]=>
  string(7) "float 1"
  [-10]=>
  string(7) "3 taolf"
  [0]=>
  string(7) "float 5"
}

-- Iteration 3 --
array(1) {
  [""]=>
  string(6) "null 7"
}

-- Iteration 4 --
array(2) {
  [1]=>
  string(5) "boolT"
  [0]=>
  string(5) "boolF"
}

-- Iteration 5 --
array(1) {
  [""]=>
  string(6) "sytpme"
}

-- Iteration 6 --
array(3) {
  ["string"]=>
  string(7) "dgnirts"
  ["gnirts"]=>
  string(7) "strings"
  ["hello world"]=>
  string(7) "stringh"
}

-- Iteration 7 --
array(2) {
  ["binary1"]=>
  string(8) "1 yranib"
  ["binary2"]=>
  string(8) "7 yranib"
}

-- Iteration 8 --
array(1) {
  [""]=>
  string(9) "denifednu"
}

-- Iteration 9 --
array(1) {
  [""]=>
  string(5) "unset"
}
Done
