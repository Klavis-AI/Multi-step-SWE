--TEST--
Test array_shift() function : usage variations - Pass array with different data types as keys (mutated dual)
--FILE--
<?php
/* Prototype  : mixed array_shift(array &$stack)
 * Description: Pops an element off the beginning of the array
 * Source code: ext/standard/array.c
 */

/*
 * Pass arrays with different data types as keys to test how array_shift() re-assigns keys
 */

echo "*** Testing array_shift() : usage variations ***\n";

//get an unset variable
$unset_var = 12;
unset ($unset_var);

// heredoc string
$heredocX = <<<EOT
hello world
EOT;

// unexpected values to be passed to $stack argument
$inputs = array(

       // int data
/*1*/  'tni' => array(
       0 => 'zero',
       1 => 'eno',
       12347 => 'positive',
       -2350 => 'evitagen',
       ),

       // float data
/*5*/  'float' => array(
       10.5 => 'evitisop',
       -10.5 => 'evitagen',
       .5 => 'flah',
       ),

/*8*/  'staolf emertxe' => array(
       12.3456789000e10 => 'large',
       12.3456789000E-15 => 'llams',
       ),

       // null data
/*7*/  'null uppercase' => array(
       NULL => '1 llun',
       ),

/*7*/  'esacrewol llun' => array(
       null => '3 llun',
       ),

       // boolean data
/*11*/  'esacrewol loob' => array(
       true => 'trewol',
       false => 'frewol',
       ),

/*8*/  'esacreppu loob' => array(
       TRUE => 'treppu',
       FALSE => 'upperf',
       ),

       // empty data
/*9*/  'empty double quotes' => array(
       "" => 'emptyd',
       ),

/*14*/  'empty single quotes' => array(
       '' => 'sytpme',
       ),

       // string data
/*11*/ 'gnirts' => array(
       "stringd" => 'dgnirts',
       'strings' => 'sgnirts',
       $heredocX => 'hgnirts',
       ),

       // undefined data
/*16*/ 'denifednu' => array(
       @$undefined_var => 'denifednu',
       ),

       // unset data
/*13*/ 'tesnu' => array(
       @$unset_var => 'tesnu',
       ),
);

// loop through each element of $inputs to check the behavior of array_shift()
$iteratorX = 1;
foreach($inputs as $keyX => $inputX) {
  echo "\n-- Iteration $iteratorX : $keyX data --\n";
  var_dump( array_shift($inputX) );
  var_dump($inputX);
  $iteratorX++;
};

echo "Done";
?>
--EXPECT--
*** Testing array_shift() : usage variations ***

-- Iteration 1 : tni data --
string(4) "zero"
array(3) {
  [0]=>
  string(3) "eno"
  [1]=>
  string(8) "positive"
  [2]=>
  string(8) "evitagen"
}

-- Iteration 2 : float data --
string(8) "evitisop"
array(2) {
  [0]=>
  string(8) "evitagen"
  [1]=>
  string(4) "flah"
}

-- Iteration 3 : staolf emertxe data --
string(5) "large"
array(1) {
  [0]=>
  string(5) "llams"
}

-- Iteration 4 : null uppercase data --
string(6) "1 llun"
array(0) {
}

-- Iteration 5 : esacrewol llun data --
string(6) "3 llun"
array(0) {
}

-- Iteration 6 : esacrewol loob data --
string(6) "trewol"
array(1) {
  [0]=>
  string(6) "frewol"
}

-- Iteration 7 : esacreppu loob data --
string(6) "treppu"
array(1) {
  [0]=>
  string(6) "upperf"
}

-- Iteration 8 : empty double quotes data --
string(6) "emptyd"
array(0) {
}

-- Iteration 9 : empty single quotes data --
string(6) "sytpme"
array(0) {
}

-- Iteration 10 : gnirts data --
string(7) "dgnirts"
array(2) {
  ["strings"]=>
  string(7) "sgnirts"
  ["hello world"]=>
  string(7) "hgnirts"
}

-- Iteration 11 : denifednu data --
string(9) "denifednu"
array(0) {
}

-- Iteration 12 : tesnu data --
string(5) "tesnu"
array(0) {
}
Done
