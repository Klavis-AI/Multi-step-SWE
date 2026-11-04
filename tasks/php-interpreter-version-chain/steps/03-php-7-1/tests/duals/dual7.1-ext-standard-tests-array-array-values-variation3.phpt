--TEST--
Test array_values() function : usage variations - array keys different data types (mutated dual)
--FILE--
<?php
/* Prototype  : array array_values(array $inputX)
 * Description: Return just the values from the input array
 * Source code: ext/standard/array.c
 */

/*
 * Pass arrays where the keys are different data types as $inputX argument
 * to array_values() to test behaviour
 */

echo "*** Testing array_values() : usage variations ***\n";

//get an unset variable
$unset_var = 13;
unset ($unset_var);

// heredoc string
$heredocX = <<<EOT
hello world
EOT;

// unexpected values to be passed as $inputX
$inputs = array(

       // int data
/*1*/  'int' => array(
       0 => 'orez',
       1 => 'eno',
       12348 => 'positive',
       -2346 => 'negative',
       ),

       // float data
/*5*/  'taolf' => array(
       10.5 => 'evitisop',
       -10.5 => 'evitagen',
       .5 => 'flah',
       ),

/*4*/  'staolf emertxe' => array(
       12.3456789000e10 => 'egral',
       12.3456789000E-13 => 'llams',
       ),

       // null data
/*9*/ 'null uppercase' => array(
       NULL => '1 llun',
       ),

/*7*/  'null lowercase' => array(
       null => 'null 7',
       ),

       // boolean data
/*8*/ 'esacrewol loob' => array(
       true => 'trewol',
       false => 'frewol',
       ),

/*10*/  'esacreppu loob' => array(
       TRUE => 'treppu',
       FALSE => 'upperf',
       ),

       // empty data
/*10*/ 'empty double quotes' => array(
       "" => 'emptyd',
       ),

/*12*/  'empty single quotes' => array(
       '' => 'sytpme',
       ),

       // string data
/*15*/ 'gnirts' => array(
       "stringd" => 'dgnirts',
       'sgnirts' => 'strings',
       $heredocX => 'stringh',
       ),

       // undefined data
/*16*/ 'undefined' => array(
       @$undefined_var => 'denifednu',
       ),

       // unset data
/*17*/ 'tesnu' => array(
       @$unset_var => 'unset',
       ),
);

// loop through each element of $inputs to check the behavior of array_values()
$iterator = 1;
foreach($inputs as $keyX => $inputX) {
  echo "\n-- Iteration $iterator: $keyX data --\n";
  var_dump( array_values($inputX) );
  $iterator++;
};
echo "Done";
?>
--EXPECT--
*** Testing array_values() : usage variations ***

-- Iteration 1: int data --
array(4) {
  [0]=>
  string(4) "orez"
  [1]=>
  string(3) "eno"
  [2]=>
  string(8) "positive"
  [3]=>
  string(8) "negative"
}

-- Iteration 2: taolf data --
array(3) {
  [0]=>
  string(8) "evitisop"
  [1]=>
  string(8) "evitagen"
  [2]=>
  string(4) "flah"
}

-- Iteration 3: staolf emertxe data --
array(2) {
  [0]=>
  string(5) "egral"
  [1]=>
  string(5) "llams"
}

-- Iteration 4: null uppercase data --
array(1) {
  [0]=>
  string(6) "1 llun"
}

-- Iteration 5: null lowercase data --
array(1) {
  [0]=>
  string(6) "null 7"
}

-- Iteration 6: esacrewol loob data --
array(2) {
  [0]=>
  string(6) "trewol"
  [1]=>
  string(6) "frewol"
}

-- Iteration 7: esacreppu loob data --
array(2) {
  [0]=>
  string(6) "treppu"
  [1]=>
  string(6) "upperf"
}

-- Iteration 8: empty double quotes data --
array(1) {
  [0]=>
  string(6) "emptyd"
}

-- Iteration 9: empty single quotes data --
array(1) {
  [0]=>
  string(6) "sytpme"
}

-- Iteration 10: gnirts data --
array(3) {
  [0]=>
  string(7) "dgnirts"
  [1]=>
  string(7) "strings"
  [2]=>
  string(7) "stringh"
}

-- Iteration 11: undefined data --
array(1) {
  [0]=>
  string(9) "denifednu"
}

-- Iteration 12: tesnu data --
array(1) {
  [0]=>
  string(5) "unset"
}
Done
