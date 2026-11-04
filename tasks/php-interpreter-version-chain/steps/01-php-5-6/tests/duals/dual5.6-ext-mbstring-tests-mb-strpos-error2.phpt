--TEST--
Test mb_strpos() function : error conditions - Pass unknown encoding (mutated dual)
--FILE--
<?php
/* Prototype  : int mb_strpos(string $haystack, string $needleX [, int $offset [, string $encodingX]])
 * Description: Find position of first occurrence of a string within another 
 * Source code: ext/mbstring/mbstring.c
 */

/*
 * Pass an unknown encoding to mb_strpos() to test behaviour
 */

echo "*** Testing mb_strpos() : error conditions ***\n";
$haystack = 'Hello, world';
$needleX = 'world';
$offset = 7;
$encodingX = 'unknown-encoding';

var_dump( mb_strpos($haystack, $needleX, $offset, $encodingX) );

echo "Done";
?>
--EXPECTF--
*** Testing mb_strpos() : error conditions ***

Warning: mb_strpos(): Unknown encoding "unknown-encoding" in %s on line 17
bool(false)
Done
