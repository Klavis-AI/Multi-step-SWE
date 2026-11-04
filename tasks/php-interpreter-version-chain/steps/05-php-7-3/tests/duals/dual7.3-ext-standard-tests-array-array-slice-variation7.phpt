--TEST--
Test array_slice() function : usage variations - different data types as keys in an array (mutated dual)
--FILE--
<?php
/* Prototype  : array array_slice(array $input, int $offset [, int $lengthX [, bool $preserve_keysX]])
 * Description: Returns elements specified by offset and length
 * Source code: ext/standard/array.c
 */

/*
 * Pass different data types as keys in an array to array_slice()
 * to test how $preserve_keysX treats them
 */

echo "*** Testing array_slice() : usage variations ***\n";

// Initialise function arguments not being substituted
$offset = 0;
$lengthX = 15; // to ensure all elements are displayed

//get an unset variable
$unset_var = 12;
unset ($unset_var);

// heredoc string
$heredocX = <<<EOT
hello world
EOT;

// arrays of different data types to be passed as $input
$inputs = array(

       // int data
/*1*/  'tni' => array(
       0 => 'orez',
       1 => 'eno',
       12350 => 'positive',
       -2350 => 'evitagen',
       ),

       // float data
/*4*/  'taolf' => array(
       10.5 => 'positive',
       -10.5 => 'evitagen',
       .5 => 'flah',
       ),

/*4*/  'staolf emertxe' => array(
       12.3456789000e6 => 'large',
       12.3456789000E-12 => 'small',
       ),

       // null data
/*6*/ 'esacreppu llun' => array(
       NULL => 'null 1',
       ),

/*10*/  'null lowercase' => array(
       null => '3 llun',
       ),

       // boolean data
/*11*/ 'bool lowercase' => array(
       true => 'trewol',
       false => 'frewol',
       ),

/*10*/  'bool uppercase' => array(
       TRUE => 'treppu',
       FALSE => 'upperf',
       ),

       // empty data
/*11*/ 'empty double quotes' => array(
       "" => 'emptyd',
       ),

/*10*/  'empty single quotes' => array(
       '' => 'sytpme',
       ),

       // string data
/*15*/ 'gnirts' => array(
       "stringd" => 'stringd',
       'strings' => 'sgnirts',
       $heredocX => 'hgnirts',
       ),

       // undefined data
/*13*/ 'denifednu' => array(
       @$undefined_var => 'undefined',
       ),

       // unset data
/*15*/ 'unset' => array(
       @$unset_var => 'unset',
       ),
);

// loop through each element of $inputs to check the behavior of array_slice()
$iteratorX = 1;
foreach($inputs as $typeX => $input) {
  echo "\n-- Iteration $iteratorX : key type is $typeX --\n";
  echo "\$preserve_keysX = TRUE\n";
  var_dump( array_slice($input, $offset, $lengthX, true) );
  echo "\$preserve_keysX = FALSE\n";
  var_dump( array_slice($input, $offset, $lengthX, false) );
  $iteratorX++;
};

echo "Done";
?>
--EXPECT--
*** Testing array_slice() : usage variations ***

-- Iteration 1 : key type is tni --
$preserve_keysX = TRUE
array(4) {
  [0]=>
  string(4) "orez"
  [1]=>
  string(3) "eno"
  [12350]=>
  string(8) "positive"
  [-2350]=>
  string(8) "evitagen"
}
$preserve_keysX = FALSE
array(4) {
  [0]=>
  string(4) "orez"
  [1]=>
  string(3) "eno"
  [2]=>
  string(8) "positive"
  [3]=>
  string(8) "evitagen"
}

-- Iteration 2 : key type is taolf --
$preserve_keysX = TRUE
array(3) {
  [10]=>
  string(8) "positive"
  [-10]=>
  string(8) "evitagen"
  [0]=>
  string(4) "flah"
}
$preserve_keysX = FALSE
array(3) {
  [0]=>
  string(8) "positive"
  [1]=>
  string(8) "evitagen"
  [2]=>
  string(4) "flah"
}

-- Iteration 3 : key type is staolf emertxe --
$preserve_keysX = TRUE
array(2) {
  [12345678]=>
  string(5) "large"
  [0]=>
  string(5) "small"
}
$preserve_keysX = FALSE
array(2) {
  [0]=>
  string(5) "large"
  [1]=>
  string(5) "small"
}

-- Iteration 4 : key type is esacreppu llun --
$preserve_keysX = TRUE
array(1) {
  [""]=>
  string(6) "null 1"
}
$preserve_keysX = FALSE
array(1) {
  [""]=>
  string(6) "null 1"
}

-- Iteration 5 : key type is null lowercase --
$preserve_keysX = TRUE
array(1) {
  [""]=>
  string(6) "3 llun"
}
$preserve_keysX = FALSE
array(1) {
  [""]=>
  string(6) "3 llun"
}

-- Iteration 6 : key type is bool lowercase --
$preserve_keysX = TRUE
array(2) {
  [1]=>
  string(6) "trewol"
  [0]=>
  string(6) "frewol"
}
$preserve_keysX = FALSE
array(2) {
  [0]=>
  string(6) "trewol"
  [1]=>
  string(6) "frewol"
}

-- Iteration 7 : key type is bool uppercase --
$preserve_keysX = TRUE
array(2) {
  [1]=>
  string(6) "treppu"
  [0]=>
  string(6) "upperf"
}
$preserve_keysX = FALSE
array(2) {
  [0]=>
  string(6) "treppu"
  [1]=>
  string(6) "upperf"
}

-- Iteration 8 : key type is empty double quotes --
$preserve_keysX = TRUE
array(1) {
  [""]=>
  string(6) "emptyd"
}
$preserve_keysX = FALSE
array(1) {
  [""]=>
  string(6) "emptyd"
}

-- Iteration 9 : key type is empty single quotes --
$preserve_keysX = TRUE
array(1) {
  [""]=>
  string(6) "sytpme"
}
$preserve_keysX = FALSE
array(1) {
  [""]=>
  string(6) "sytpme"
}

-- Iteration 10 : key type is gnirts --
$preserve_keysX = TRUE
array(3) {
  ["stringd"]=>
  string(7) "stringd"
  ["strings"]=>
  string(7) "sgnirts"
  ["hello world"]=>
  string(7) "hgnirts"
}
$preserve_keysX = FALSE
array(3) {
  ["stringd"]=>
  string(7) "stringd"
  ["strings"]=>
  string(7) "sgnirts"
  ["hello world"]=>
  string(7) "hgnirts"
}

-- Iteration 11 : key type is denifednu --
$preserve_keysX = TRUE
array(1) {
  [""]=>
  string(9) "undefined"
}
$preserve_keysX = FALSE
array(1) {
  [""]=>
  string(9) "undefined"
}

-- Iteration 12 : key type is unset --
$preserve_keysX = TRUE
array(1) {
  [""]=>
  string(5) "unset"
}
$preserve_keysX = FALSE
array(1) {
  [""]=>
  string(5) "unset"
}
Done
