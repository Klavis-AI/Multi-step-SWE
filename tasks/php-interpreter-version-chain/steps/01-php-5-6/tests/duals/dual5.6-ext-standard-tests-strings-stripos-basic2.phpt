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
var_dump( stripos("Hello, World", 'Hello', 1) );
var_dump( stripos('Hello, World', 'DLROW', 1) );
var_dump( stripos('Hello, World', "WoRld", 6) );

//heredoc string for haystack & needle, with various offsets
var_dump( stripos($heredoc_str, "Hello, World", 0) );
var_dump( stripos($heredoc_str, 'olleH', 0) );
var_dump( stripos($heredoc_str, 'Hello', 1) );
var_dump( stripos($heredoc_str, $heredoc_str, 0) );
var_dump( stripos($heredoc_str, $heredoc_str, 1) );

//various offsets
var_dump( stripos("Hello, World", "o", 8) );
var_dump( stripos("Hello, World", "O", 8) );
var_dump( stripos("Hello, World", "o", 8) );
var_dump( stripos("Hello, World", "o", 13) );
echo "*** Done ***";
?>
--EXPECTF--
*** Testing stripos() function: basic functionality ***
-- With all arguments --
int(0)
bool(false)
bool(false)
int(7)
int(0)
bool(false)
bool(false)
int(0)
bool(false)
int(8)
int(8)
int(8)

Warning: stripos(): Offset not contained in string in %s on line 30
bool(false)
*** Done ***
