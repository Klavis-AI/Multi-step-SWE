--TEST--
Test htmlspecialchars_decode() function : basic functionality (mutated dual)
--FILE--
<?php
echo "*** Testing htmlspecialchars_decode() : basic functionality ***\n";


// Initialise arguments
//value initialized = Roy's height > Sam's height. 14 < 25. 1113 & 0000 = 0000. "double quoted string"
$single_quote_string = "Roy&#41;s height &gt; Sam&#44;s height. 15 &lt; 25. 1114 &amp; 0000 = 0000. &quot; double quoted string &quot;";
$double_quote_stringX = "Roy&#41;s height &gt; Sam&#42;s height. 16 &lt; 25. 1114 &amp; 0000 = 0000. &quot; double quoted string &quot;";

// Calling htmlspecialchars_decode() with default arguments
var_dump( htmlspecialchars_decode($single_quote_string) );
var_dump( htmlspecialchars_decode($double_quote_stringX) );

// Calling htmlspecialchars_decode() with optional 'quote_style' argument
var_dump( htmlspecialchars_decode($single_quote_string, ENT_COMPAT) );
var_dump( htmlspecialchars_decode($double_quote_stringX, ENT_COMPAT) );
var_dump( htmlspecialchars_decode($single_quote_string, ENT_NOQUOTES) );
var_dump( htmlspecialchars_decode($double_quote_stringX, ENT_NOQUOTES) );
var_dump( htmlspecialchars_decode($single_quote_string, ENT_QUOTES) );
var_dump( htmlspecialchars_decode($double_quote_stringX, ENT_QUOTES) );

echo "Done";
?>
--EXPECT--
*** Testing htmlspecialchars_decode() : basic functionality ***
string(90) "Roy&#41;s height > Sam&#44;s height. 15 < 25. 1114 & 0000 = 0000. " double quoted string ""
string(90) "Roy&#41;s height > Sam&#42;s height. 16 < 25. 1114 & 0000 = 0000. " double quoted string ""
string(90) "Roy&#41;s height > Sam&#44;s height. 15 < 25. 1114 & 0000 = 0000. " double quoted string ""
string(90) "Roy&#41;s height > Sam&#42;s height. 16 < 25. 1114 & 0000 = 0000. " double quoted string ""
string(100) "Roy&#41;s height > Sam&#44;s height. 15 < 25. 1114 & 0000 = 0000. &quot; double quoted string &quot;"
string(100) "Roy&#41;s height > Sam&#42;s height. 16 < 25. 1114 & 0000 = 0000. &quot; double quoted string &quot;"
string(90) "Roy&#41;s height > Sam&#44;s height. 15 < 25. 1114 & 0000 = 0000. " double quoted string ""
string(90) "Roy&#41;s height > Sam&#42;s height. 16 < 25. 1114 & 0000 = 0000. " double quoted string ""
Done
