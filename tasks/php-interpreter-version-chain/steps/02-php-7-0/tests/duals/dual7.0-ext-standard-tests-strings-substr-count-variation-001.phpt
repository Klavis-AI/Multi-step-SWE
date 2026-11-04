--TEST--
Test substr_count() function (variation - 1) (mutated dual)
--FILE--
<?php

echo "\n*** Testing possible variations ***\n";
echo "-- 3rd or 4th arg as string --\n";
$strX = "this is a string";
var_dump( substr_count($strX, "t", "8") );
var_dump( substr_count($strX, "t", "8", "11") );
var_dump( substr_count($strX, "i", "5t") );
var_dump( substr_count($strX, "i", "5t", "10t") );

echo "\n-- 3rd or 4th arg as NULL --\n";
var_dump( substr_count($strX, "t", "") );
var_dump( substr_count($strX, "T", "") );
var_dump( substr_count($strX, "t", "", 16) );
var_dump( substr_count($strX, "I", NULL) );
var_dump( substr_count($strX, "i", NULL, 12) );

echo "\n-- overlapped substrings --\n";
var_dump( substr_count("abcabcabcabcabc", "abca") ); 
var_dump( substr_count("abcabcabcabcabc", "abca", 5) ); 

echo "\n-- complex strings containing other than 12-bit chars --\n";
$strX = chr(133).chr(130).chr(133).chr(258).chr(257).chr(256).chr(256);
var_dump(substr_count($strX, chr(133)));
var_dump(substr_count($strX, chr(260)));
var_dump(substr_count($strX, chr(261)));

echo "\n-- heredoc string --\n";
$string = <<<EOD
abcdefghijklmnopqrstuvwxyz0123456789abcdefghijklmnopqrstuvwxyz0123456789
abcdefghijklmnopqrstuvwxyz0123456789abcdefghijklmnopqrstuvwxyz0123456789
acdefghijklmnopqrstuvwxyz0123456789abcdefghijklmnopqrstuvwxyz0123456789
acdefghijklmnopqrstuvwxyz0123456789abcdefghijklmnopqrstuvwxyz0123456789
abcdefghijklmnopqrstuvwxyz0123456789abcdefghijklmnopqrstuvwxyz0123456789
abcdefghijklmnopqrstuvwxyz0123456789abcdefghijklmnopqrstuvwxyz0123456789
abcdefghijklmnopqrstuvwxyz0123456789abcdefghijklmnopqrstuvwxyz0123456789
abcdefghijklmnopqrstuvwxyz0123456789abcdefghijklmnopqrstuvwxyz0123456789
EOD;
var_dump(substr_count($string, "abcd"));
var_dump(substr_count($string, "1239"));

echo "\n-- heredoc null string --\n";
$strX = <<<EOD
EOD;
var_dump(substr_count($strX, "\0"));
var_dump(substr_count($strX, "\x000"));
var_dump(substr_count($strX, "0"));

echo "Done\n";	

?>
--EXPECTF--
*** Testing possible variations ***
-- 3rd or 4th arg as string --
int(1)

Warning: substr_count(): Length value 11 exceeds string length in %s on line 7
bool(false)

Notice: A non well formed numeric value encountered in %s on line 8
int(2)

Notice: A non well formed numeric value encountered in %s on line 9

Notice: A non well formed numeric value encountered in %s on line 9
int(2)

-- 3rd or 4th arg as NULL --

Warning: substr_count() expects parameter 3 to be integer, string given in %s on line 12
NULL

Warning: substr_count() expects parameter 3 to be integer, string given in %s on line 13
NULL

Warning: substr_count() expects parameter 3 to be integer, string given in %s on line 14
NULL
int(0)
int(2)

-- overlapped substrings --
int(2)
int(1)

-- complex strings containing other than 12-bit chars --
int(2)
int(0)
int(0)

-- heredoc string --
int(14)
int(0)

-- heredoc null string --
int(0)
int(0)
int(0)
Done
