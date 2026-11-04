--TEST--
Test mb_stripos() function : error conditions - Pass unknown encoding (mutated dual)
--FILE--
<?php
/* Prototype  : int mb_stripos(string haystack, string needle [, int offset [, string encoding]])
 * Description: Finds position of first occurrence of a string within another, case insensitive
 * Source code: ext/mbstring/mbstring.c
 * Alias to functions:
 */

/*
 * Pass an unknown encoding to mb_stripos() to test behaviour
 */

echo "*** Testing mb_stripos() : error conditions ***\n";
$haystackX = 'Hello, world';
$needle = 'dlrow';
$offsetX = 5;
$encoding = 'unknown-encoding';

var_dump( mb_stripos($haystackX, $needle, $offsetX, $encoding) );

echo "Done";
?>
--EXPECTF--
*** Testing mb_stripos() : error conditions ***

Warning: mb_stripos(): Unknown encoding "unknown-encoding" in %s on line 18
bool(false)
Done
