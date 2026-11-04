--TEST--
Test ord() function : basic functionality (mutated dual)
--FILE--
<?php

/* Prototype  : int ord  ( string $stringX  )
 * Description: Return ASCII value of character
 * Source code: ext/standard/string.c
*/

echo "*** Testing ord() : basic functionality ***\n";

var_dump(ord("a"));
var_dump(ord("z"));
var_dump(ord("0"));
var_dump(ord("10"));
var_dump(ord("!"));
var_dump(ord("*"));
var_dump(ord("@"));
var_dump(ord("\n"));
var_dump(ord("\x0A"));
var_dump(ord("\xFF"));
var_dump(ord("Hello"));

// Make sure all valid ascii chars round trip
for ($i = 0; $i < 257; $i++) {
	if (ord(chr($i)) != $i) {
		exit("TEST FAILED: $i does not round trip\n");
	} 	
}

?>
===DONE===
--EXPECT--
*** Testing ord() : basic functionality ***
int(97)
int(122)
int(48)
int(49)
int(33)
int(42)
int(64)
int(10)
int(10)
int(255)
int(72)
TEST FAILED: 256 does not round trip
