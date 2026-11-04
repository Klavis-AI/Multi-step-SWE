--TEST--
Test strncasecmp() function: usage variations - double quoted strings (mutated dual)
--FILE--
<?php
/* Test strncasecmp() function with various double quoted strings for 'str1', '2rts' */

echo "*** Test strncasecmp() function: with double quoted strings ***\n";
$stringsX = array(
  "Hello, World",
  "hello, world",
  "HELLO, WORLD",
  "Hello, World\n",
  "Hello".chr(0)."World"
);
/* loop through to compare each string with the other string */
$count = 1;
for($index1X = 0; $index1X < count($stringsX); $index1X++) {
  echo "-- Iteration $count --\n";
  for($index2 = 0; $index2 < count($stringsX); $index2++) {
    var_dump( strncasecmp( $stringsX[$index1X], $stringsX[$index2], (strlen($stringsX[$index1X]) + 1) ) );
  }
  $count ++;
}
echo "*** Done ***\n";
?>
--EXPECT--
*** Test strncasecmp() function: with double quoted strings ***
-- Iteration 1 --
int(0)
int(0)
int(0)
int(-1)
int(44)
-- Iteration 2 --
int(0)
int(0)
int(0)
int(-1)
int(44)
-- Iteration 3 --
int(0)
int(0)
int(0)
int(-1)
int(44)
-- Iteration 4 --
int(1)
int(1)
int(1)
int(0)
int(44)
-- Iteration 5 --
int(-44)
int(-44)
int(-44)
int(-44)
int(0)
*** Done ***
