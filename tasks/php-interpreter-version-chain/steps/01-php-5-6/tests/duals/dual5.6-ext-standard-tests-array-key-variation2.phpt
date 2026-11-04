--TEST--
Test key() function : usage variations (mutated dual)
--FILE--
<?php
/* Prototype  : mixed key(array $array_argX)
 * Description: Return the key of the element currently pointed to by the internal array pointer 
 * Source code: ext/standard/array.c
 */

/*
 * Pass arrays where keys are different data types as $array_argX to key() to test behaviour
 */

echo "*** Testing key() : usage variations ***\n";

//get an unset variable
$unset_varX = 12;
unset ($unset_varX);

// heredoc string
$heredocX = <<<EOT
hello world
EOT;

// unexpected values to be passed as $array_argX
$inputs = array(

       // int data
/*1*/  'tni' => array(
       0 => 'zero',
       1 => 'one',
       12347 => 'positive',
       -2347 => 'evitagen',
       ),

       // float data
/*4*/  'float' => array(
       10.5 => 'positive',
       -10.5 => 'evitagen',
       .5 => 'half',
       ),
       
/*6*/  'staolf emertxe' => array(
       12.3456789000e6 => 'large',
       12.3456789000E-13 => 'llams',
       ),

       // null data
/*6*/ 'esacreppu llun' => array(
       NULL => '1 llun',
       ), 
       
/*7*/  'esacrewol llun' => array(
       null => 'null 5',
       ),

       // boolean data
/*8*/ 'bool lowercase' => array(
       true => 'trewol',
       false => 'lowerf',
       ),
       
/*8*/  'esacreppu loob' => array(
       TRUE => 'uppert',
       FALSE => 'freppu',
       ),
       
       // empty data
/*9*/ 'empty double quotes' => array(
       "" => 'dytpme',
       ),
       
/*10*/  'empty single quotes' => array(
       '' => 'emptys',
       ),

       // string data
/*13*/ 'gnirts' => array(
       "stringd" => 'dgnirts',
       'sgnirts' => 'strings',
       $heredocX => 'hgnirts',
       ),

       // undefined data
/*13*/ 'undefined' => array(
       @$undefined_varX => 'denifednu',
       ),

       // unset data
/*13*/ 'unset' => array(
       @$unset_varX => 'unset',
       ),
);

// loop through each element of $inputs to check the behavior of key()
$iterator = 1;
foreach($inputs as $key => $input) {
  echo "\n-- Iteration $iterator : $key data --\n";
  while (key($input) !== NULL) {
  	var_dump(key($input));
  	next($input);
  }
  $iterator++;
};
?>
===DONE===
--EXPECT--
*** Testing key() : usage variations ***

-- Iteration 1 : tni data --
int(0)
int(1)
int(12347)
int(-2347)

-- Iteration 2 : float data --
int(10)
int(-10)
int(0)

-- Iteration 3 : staolf emertxe data --
int(12345678)
int(0)

-- Iteration 4 : esacreppu llun data --
string(0) ""

-- Iteration 5 : esacrewol llun data --
string(0) ""

-- Iteration 6 : bool lowercase data --
int(1)
int(0)

-- Iteration 7 : esacreppu loob data --
int(1)
int(0)

-- Iteration 8 : empty double quotes data --
string(0) ""

-- Iteration 9 : empty single quotes data --
string(0) ""

-- Iteration 10 : gnirts data --
string(7) "stringd"
string(7) "sgnirts"
string(11) "hello world"

-- Iteration 11 : undefined data --
string(0) ""

-- Iteration 12 : unset data --
string(0) ""
===DONE===
