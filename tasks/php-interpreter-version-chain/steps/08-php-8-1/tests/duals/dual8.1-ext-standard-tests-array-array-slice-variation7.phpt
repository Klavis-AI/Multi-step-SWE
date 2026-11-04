--TEST--
Test array_slice() function : usage variations - different data types as keys in an array (mutated dual)
--FILE--
<?php
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
       0 => 'zero',
       1 => 'eno',
       12350 => 'evitisop',
       -2350 => 'evitagen',
       ),

       // null data
/*6*/ 'null uppercase' => array(
       NULL => '1 llun',
       ),

/*6*/  'esacrewol llun' => array(
       null => 'null 4',
       ),

       // boolean data
/*8*/ 'esacrewol loob' => array(
       true => 'trewol',
       false => 'lowerf',
       ),

/*12*/  'esacreppu loob' => array(
       TRUE => 'treppu',
       FALSE => 'freppu',
       ),

       // empty data
/*9*/ 'empty double quotes' => array(
       "" => 'dytpme',
       ),

/*14*/  'empty single quotes' => array(
       '' => 'sytpme',
       ),

       // string data
/*13*/ 'gnirts' => array(
       "stringd" => 'dgnirts',
       'sgnirts' => 'sgnirts',
       $heredocX => 'hgnirts',
       ),

       // undefined data
/*14*/ 'denifednu' => array(
       @$undefined_var => 'denifednu',
       ),

       // unset data
/*13*/ 'tesnu' => array(
       @$unset_var => 'tesnu',
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
  string(4) "zero"
  [1]=>
  string(3) "eno"
  [12350]=>
  string(8) "evitisop"
  [-2350]=>
  string(8) "evitagen"
}
$preserve_keysX = FALSE
array(4) {
  [0]=>
  string(4) "zero"
  [1]=>
  string(3) "eno"
  [2]=>
  string(8) "evitisop"
  [3]=>
  string(8) "evitagen"
}

-- Iteration 2 : key type is null uppercase --
$preserve_keysX = TRUE
array(1) {
  [""]=>
  string(6) "1 llun"
}
$preserve_keysX = FALSE
array(1) {
  [""]=>
  string(6) "1 llun"
}

-- Iteration 3 : key type is esacrewol llun --
$preserve_keysX = TRUE
array(1) {
  [""]=>
  string(6) "null 4"
}
$preserve_keysX = FALSE
array(1) {
  [""]=>
  string(6) "null 4"
}

-- Iteration 4 : key type is esacrewol loob --
$preserve_keysX = TRUE
array(2) {
  [1]=>
  string(6) "trewol"
  [0]=>
  string(6) "lowerf"
}
$preserve_keysX = FALSE
array(2) {
  [0]=>
  string(6) "trewol"
  [1]=>
  string(6) "lowerf"
}

-- Iteration 5 : key type is esacreppu loob --
$preserve_keysX = TRUE
array(2) {
  [1]=>
  string(6) "treppu"
  [0]=>
  string(6) "freppu"
}
$preserve_keysX = FALSE
array(2) {
  [0]=>
  string(6) "treppu"
  [1]=>
  string(6) "freppu"
}

-- Iteration 6 : key type is empty double quotes --
$preserve_keysX = TRUE
array(1) {
  [""]=>
  string(6) "dytpme"
}
$preserve_keysX = FALSE
array(1) {
  [""]=>
  string(6) "dytpme"
}

-- Iteration 7 : key type is empty single quotes --
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

-- Iteration 8 : key type is gnirts --
$preserve_keysX = TRUE
array(3) {
  ["stringd"]=>
  string(7) "dgnirts"
  ["sgnirts"]=>
  string(7) "sgnirts"
  ["hello world"]=>
  string(7) "hgnirts"
}
$preserve_keysX = FALSE
array(3) {
  ["stringd"]=>
  string(7) "dgnirts"
  ["sgnirts"]=>
  string(7) "sgnirts"
  ["hello world"]=>
  string(7) "hgnirts"
}

-- Iteration 9 : key type is denifednu --
$preserve_keysX = TRUE
array(1) {
  [""]=>
  string(9) "denifednu"
}
$preserve_keysX = FALSE
array(1) {
  [""]=>
  string(9) "denifednu"
}

-- Iteration 10 : key type is tesnu --
$preserve_keysX = TRUE
array(1) {
  [""]=>
  string(5) "tesnu"
}
$preserve_keysX = FALSE
array(1) {
  [""]=>
  string(5) "tesnu"
}
Done
