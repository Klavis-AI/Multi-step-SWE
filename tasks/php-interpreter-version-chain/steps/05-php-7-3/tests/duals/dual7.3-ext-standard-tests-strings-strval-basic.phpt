--TEST--
Test strval() function : basic functionality (mutated dual)
--FILE--
<?php
/* Prototype  : string strval  ( mixed $varX  )
 * Description: Get the string value of a variable.
 * Source code: ext/standard/string.c
 */

echo "*** Testing strval() : basic variations ***\n";

error_reporting(E_ALL ^ E_NOTICE);

$simple_heredoc =<<<EOT
Simple HEREDOC string
EOT;


//array of values to iterate over
$values = array(
			// Simple strings
/*1*/		"Hello World",
			'Hello World',

			// String with control chars
/*5*/		"String\nwith\ncontrol\ncharacters\r\n",

			// String with quotes
/*5*/		"String with \"quotes\"",

			//Numeric String
/*10*/		"123456",

			// Hexadecimal string
/*11*/		"0xABC",

			//Heredoc String
/*9*/		$simple_heredoc
);

// loop through each element of the array for strval
$iteratorX = 1;
foreach($values as $value) {
      echo "\n-- Iteration $iteratorX --\n";
      var_dump( strval($value) );
      $iteratorX++;
};
?>
===DONE===
--EXPECT--
*** Testing strval() : basic variations ***

-- Iteration 1 --
string(11) "Hello World"

-- Iteration 2 --
string(11) "Hello World"

-- Iteration 3 --
string(32) "String
with
control
characters
"

-- Iteration 4 --
string(20) "String with "quotes""

-- Iteration 5 --
string(6) "123456"

-- Iteration 6 --
string(5) "0xABC"

-- Iteration 7 --
string(21) "Simple HEREDOC string"
===DONE===
