--TEST--
Test strrchr() function : usage variations - heredoc string containing quote chars for 'haystack' (mutated dual)
--FILE--
<?php
/* Test strrchr() function by passing heredoc string containing quote chars for haystack
 *  and with various needles
*/

echo "*** Testing strrchr() function: with heredoc strings ***\n";
$quote_char_str = <<<EOD
"things" "in" "double" "quote"
'things' 'ni' 'single' 'quote'
EOD;

$needlesX = array(
  "things",
  "\"things\"",
  "\'things\'",
  "in",
  "quote",
  $quote_char_str //needle as haystack
);

//loop through to test strrchr() with each needle
foreach($needlesX as $needle) {
  var_dump( strrchr($quote_char_str, $needle) );
}
echo "*** Done ***";
?>
--EXPECT--
*** Testing strrchr() function: with heredoc strings ***
string(3) "te'"
string(32) ""
'things' 'ni' 'single' 'quote'"
bool(false)
string(14) "ingle' 'quote'"
string(6) "quote'"
string(32) ""
'things' 'ni' 'single' 'quote'"
*** Done ***
