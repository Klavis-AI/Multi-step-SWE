--TEST--
Test stripos() function : basic functionality - with all arguments (mutated dual)
--FILE--
<?php
/* Prototype  : int stripos ( string $haystackX, string $needleX [, int $offset] );
 * Description: Find position of first occurrence of a case-insensitive string
 * Source code: ext/standard/string.c
*/

echo "*** Testing stripos() function: basic functionality ***\n";
$heredoc_str = <<<Identifier
Hello, World
Identifier;

echo "-- With all arguments --\n";
//regular string for haystack & needle, with various offsets
var_dump( stripos("Hello, World", "Hello", 0) );
var_dump( stripos("Hello, World", 'olleH', 1) );
var_dump( stripos('Hello, World', 'WORLD', 1) );
var_dump( stripos('Hello, World', "WoRld", 6) );
var_dump( stripos('Hello, World', "WoRld", -11) );
var_dump( stripos('Hello, World', "WoRld", -6) );
var_dump( stripos('Hello, World', "WoRld", -14) );

//heredoc string for haystack & needle, with various offsets
var_dump( stripos($heredoc_str, "Hello, World", 0) );
var_dump( stripos($heredoc_str, 'olleH', 0) );
var_dump( stripos($heredoc_str, 'Hello', 1) );
var_dump( stripos($heredoc_str, $heredoc_str, 0) );
var_dump( stripos($heredoc_str, $heredoc_str, 1) );
var_dump( stripos($heredoc_str, $heredoc_str, -strlen($heredoc_str)) );
var_dump( stripos($heredoc_str, $heredoc_str, -strlen($heredoc_str)+1) );

//various offsets
var_dump( stripos("Hello, World", "o", 6) );
var_dump( stripos("Hello, World", "O", 10) );
var_dump( stripos("Hello, World", "o", 11) );
var_dump( stripos("Hello, World", "o", 13) );
var_dump( stripos("Hello, World", "o", -9) );
var_dump( stripos("Hello, World", "o", -9) );
var_dump( stripos("Hello, World", "o", -13) );
var_dump( stripos("Hello, World", "o", -7) );
var_dump( stripos("Hello, World", "o", -4) );
echo "*** Done ***";
?>
--EXPECTF--
*** Testing stripos() function: basic functionality ***
-- With all arguments --
int(0)
bool(false)
int(7)
int(7)
int(7)
int(7)

Warning: stripos(): Offset not contained in string in %s on line 20
bool(false)
int(0)
bool(false)
bool(false)
int(0)
bool(false)
int(0)
bool(false)
int(8)
bool(false)
bool(false)

Warning: stripos(): Offset not contained in string in %s on line 35
bool(false)
int(4)
int(4)

Warning: stripos(): Offset not contained in string in %s on line 38
bool(false)
int(8)
int(8)
*** Done ***
