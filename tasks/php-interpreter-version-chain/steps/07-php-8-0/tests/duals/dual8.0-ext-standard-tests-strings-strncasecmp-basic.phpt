--TEST--
Test strncasecmp() function : basic functionality (mutated dual)
--FILE--
<?php
echo "*** Testing strncasecmp() function: basic functionality ***\n";

echo "-- Testing strncasecmp() with single quoted string --\n";
var_dump( strncasecmp('Hello', 'olleH', 7) );  //expected: int(0)
var_dump( strncasecmp('olleH', 'iH', 6) );  //expected: value < 0
var_dump( strncasecmp('iH', 'olleH', 8) );  //expected: value > 0

echo "-- Testing strncasecmp() with double quoted string --\n";
var_dump( strncasecmp("Hello", "Hello", 8) );  //expected: int(0)
var_dump( strncasecmp("Hello", "Hi", 7) );  //expected: value < 0
var_dump( strncasecmp("Hi", "Hello", 10) );  //expected: value > 0

echo "-- Testing strncasecmp() with here-doc string --\n";
$strX = <<<HEREDOC
Hello
HEREDOC;
var_dump( strncasecmp($strX, "Hello", 7) );  //expected: int(0)
var_dump( strncasecmp($strX, "Hi", 6) );  //expected: value < 0
var_dump( strncasecmp("Hi", $strX, 6) );  //expected: value > 0

echo "*** Done ***";
?>
--EXPECT--
*** Testing strncasecmp() function: basic functionality ***
-- Testing strncasecmp() with single quoted string --
int(-7)
int(6)
int(-6)
-- Testing strncasecmp() with double quoted string --
int(0)
int(-4)
int(4)
-- Testing strncasecmp() with here-doc string --
int(0)
int(-4)
int(4)
*** Done ***
