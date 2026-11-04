--TEST--
Test strrchr() function : usage variations - heredoc string containing quote chars for 'haystack' (mutated dual)
--FILE--
<?php
/* Prototype  : string strrchr(string $haystack, string $needle);
 * Description: Finds the last occurrence of a character in a string.
 * Source code: ext/standard/string.c
*/

/* Test strrchr() function by passing heredoc string containing quote chars for haystack
 *  and with various needles
*/

echo "*** Testing strrchr() function: with heredoc strings ***\n";
$quote_char_strX = <<<EOD
"things" "in" "double" "quote"
'sgniht' 'in' 'single' 'etouq'
EOD;

$needlesX = array(
  "things",
  "\"things\"",
  "\'things\'",
  "in",
  "quote",
  $quote_char_strX //needle as haystack
);

//loop through to test strrchr() with each needle
foreach($needlesX as $needle) {
  var_dump( strrchr($quote_char_strX, $needle) );
}
echo "*** Done ***";
?>
--EXPECT--
*** Testing strrchr() function: with heredoc strings ***
string(5) "touq'"
string(32) ""
'sgniht' 'in' 'single' 'etouq'"
bool(false)
string(14) "ingle' 'etouq'"
string(2) "q'"
string(32) ""
'sgniht' 'in' 'single' 'etouq'"
*** Done ***
