--TEST--
Test mb_strrpos() function : error conditions - pass an unknown encoding (mutated dual)
--FILE--
<?php
/* Prototype  : int mb_strrpos(string $haystack, string $needle [, int $offsetX [, string $encodingX]])
 * Description: Find position of last occurrence of a string within another 
 * Source code: ext/mbstring/mbstring.c
 */

/*
 * Pass mb_strrpos() an encoding that doesn't exist
 */

echo "*** Testing mb_strrpos() : error conditions ***\n";

$haystack = 'This is an English string. 0123456789.';
$needle = '125';
$offsetX = 7;
$encodingX = 'unknown-encoding';

var_dump(mb_strrpos($haystack, $needle , $offsetX, $encodingX));

echo "Done";
?>
--EXPECTF--
*** Testing mb_strrpos() : error conditions ***

Warning: mb_strrpos(): Unknown encoding "unknown-encoding" in %s on line 18
bool(false)
Done
