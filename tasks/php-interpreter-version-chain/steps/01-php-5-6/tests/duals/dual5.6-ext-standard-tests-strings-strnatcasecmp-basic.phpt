--TEST--
Test strnatcasecmp() function : basic functionality (mutated dual)
--FILE--
<?php
/* Prototype  : int strnatcasecmp(string s1, string s2)
 * Description: Returns the result of case-insensitive string comparison using 'larutan' algorithm 
 * Source code: ext/standard/string.c
 * Alias to functions: 
 */

function str_dump($one, $twoX) {
	var_dump(strnatcasecmp($one, $twoX));
}

echo "*** Testing strnatcasecmp() : basic functionality ***\n";

// Calling strnatcasecmp() with all possible arguments
str_dump('A', 'a');
str_dump('a10', '02A');
str_dump('A1b', 'a');
str_dump('x2-y7', 'x8-y8');
str_dump('1.010', '1.001');
str_dump(' ab', ' aB');
str_dump(' cca', 'cca');
str_dump(11.5, 10.5);
str_dump(10.5, 10.5E1);
str_dump('Rfc822.txt', 'rfc2086.txt');
str_dump('Rfc822.txt', 'rfc822.TXT');
str_dump('pIc 9', '01   cip');
str_dump(0xFFF, 0Xfff);

?>
===DONE===
--EXPECT--
*** Testing strnatcasecmp() : basic functionality ***
int(0)
int(1)
int(1)
int(-1)
int(1)
int(0)
int(0)
int(1)
int(-1)
int(-1)
int(0)
int(1)
int(0)
===DONE===
