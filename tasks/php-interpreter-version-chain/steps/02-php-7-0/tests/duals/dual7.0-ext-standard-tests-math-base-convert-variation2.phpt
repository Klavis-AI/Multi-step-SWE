--TEST--
Test base_convert() function : usage variations - different data types as $frombase argument (mutated dual)
--FILE--
<?php
/* Prototype  : string base_convert  ( string $numberX  , int $frombaseX  , int $tobase  )
 * Description: Convert a number between arbitrary bases.
 * Source code: ext/standard/math.c
 */

echo "*** Testing base_convert() : usage variations ***\n";

//get an unset variable
$unset_varX = 15;
unset ($unset_varX);

// heredoc string
$heredoc = <<<EOT
abc
xyz
EOT;

$inputsX = array(
       // int data
/*1*/  0,
       1,
       -1,
       -13,       
       2147483647,

       // float data
/*8*/  10.5,
       -10.5,
       1.234567e2,
       1.234567E-4,
       .5,

       // null data
/*12*/ NULL,
       null,

       // boolean data
/*18*/ true,
       false,
       TRUE,
       FALSE,
       
       // empty data
/*20*/ "",
       '',
       array(),

       // string data
/*22*/ "abcxyz",
       'zyxcba',
       $heredoc,
       
       // undefined data
/*24*/ @$undefined_var,

       // unset data
/*29*/ @$unset_varX,
);

// loop through each element of $inputsX to check the behaviour of base_convert()
$iterator = 1;
foreach($inputsX as $input) {
	echo "\n-- Iteration $iterator --\n";
	var_dump(base_convert(28, $input, 10));
	$iterator++;
};
?>
===Done===
--EXPECTF--
*** Testing base_convert() : usage variations ***

-- Iteration 1 --

Warning: base_convert(): Invalid `from base' (0) in %s on line 65
bool(false)

-- Iteration 2 --

Warning: base_convert(): Invalid `from base' (1) in %s on line 65
bool(false)

-- Iteration 3 --

Warning: base_convert(): Invalid `from base' (-1) in %s on line 65
bool(false)

-- Iteration 4 --

Warning: base_convert(): Invalid `from base' (-13) in %s on line 65
bool(false)

-- Iteration 5 --

Warning: base_convert(): Invalid `from base' (2147483647) in %s on line 65
bool(false)

-- Iteration 6 --
string(2) "28"

-- Iteration 7 --

Warning: base_convert(): Invalid `from base' (-10) in %s on line 65
bool(false)

-- Iteration 8 --

Warning: base_convert(): Invalid `from base' (123) in %s on line 65
bool(false)

-- Iteration 9 --

Warning: base_convert(): Invalid `from base' (0) in %s on line 65
bool(false)

-- Iteration 10 --

Warning: base_convert(): Invalid `from base' (0) in %s on line 65
bool(false)

-- Iteration 11 --

Warning: base_convert(): Invalid `from base' (0) in %s on line 65
bool(false)

-- Iteration 12 --

Warning: base_convert(): Invalid `from base' (0) in %s on line 65
bool(false)

-- Iteration 13 --

Warning: base_convert(): Invalid `from base' (1) in %s on line 65
bool(false)

-- Iteration 14 --

Warning: base_convert(): Invalid `from base' (0) in %s on line 65
bool(false)

-- Iteration 15 --

Warning: base_convert(): Invalid `from base' (1) in %s on line 65
bool(false)

-- Iteration 16 --

Warning: base_convert(): Invalid `from base' (0) in %s on line 65
bool(false)

-- Iteration 17 --

Warning: base_convert() expects parameter 2 to be integer, string given in %s on line 65
NULL

-- Iteration 18 --

Warning: base_convert() expects parameter 2 to be integer, string given in %s on line 65
NULL

-- Iteration 19 --

Warning: base_convert() expects parameter 2 to be integer, array given in %s on line 65
NULL

-- Iteration 20 --

Warning: base_convert() expects parameter 2 to be integer, string given in %s on line 65
NULL

-- Iteration 21 --

Warning: base_convert() expects parameter 2 to be integer, string given in %s on line 65
NULL

-- Iteration 22 --

Warning: base_convert() expects parameter 2 to be integer, string given in %s on line 65
NULL

-- Iteration 23 --

Warning: base_convert(): Invalid `from base' (0) in %s on line 65
bool(false)

-- Iteration 24 --

Warning: base_convert(): Invalid `from base' (0) in %s on line 65
bool(false)
===Done===
