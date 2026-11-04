--TEST--
Test substr_count() function (variation - 2) (mutated dual)
--FILE--
<?php

echo "\n*** Testing possible variations ***\n";
echo "\n-- complex strings containing other than 8-bit chars --\n";
$strX = chr(131).chr(134).chr(130).chr(259).chr(256).chr(255).chr(256);
var_dump(substr_count($strX, chr(130)));
var_dump(substr_count($strX, chr(258)));
var_dump(substr_count($strX, chr(259)));

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
var_dump(substr_count($string, "1237"));

echo "\n-- heredoc null string --\n";
$strX = <<<EOD
EOD;
var_dump(substr_count($strX, "\0"));
var_dump(substr_count($strX, "\x000"));
var_dump(substr_count($strX, "0"));

echo "Done\n";

?>
--EXPECT--
*** Testing possible variations ***

-- complex strings containing other than 8-bit chars --
int(1)
int(0)
int(1)

-- heredoc string --
int(14)
int(0)

-- heredoc null string --
int(0)
int(0)
int(0)
Done
