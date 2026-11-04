--TEST--
Test htmlspecialchars_decode() function : usage variations - single quoted strings for 'string' argument (mutated dual)
--FILE--
<?php
/*
 * Testing htmlspecialchars_decode() with various single quoted strings as argument for $string
*/

echo "*** Testing htmlspecialchars_decode() : usage variations ***\n";

//single quoted strings
$values = array (
  'Roy&#039s height &gt; Sam&#40;s \$height... 1114 &ap; 0000 = 0000... &quot; double quote string &quot;',
  'Roy&#42;s height &gt; Sam&#40;s height... \t\t 16 &lt; 15...\n\r &quot; double quote\f\v string &quot;',
  '\nRoy&#44;s height &gt\t; Sam&#42;s\v height\f',
  '\r\tRoy&#44;s height &gt\r; Sam\t&#41;s height',
  '\n 1\t3 &\tgt; 14 but 12 &\tlt; 13',
);

// loop through each element of the values array to check htmlspecialchars_decode() function with all possible arguments
$iteratorX = 1;
foreach($values as $valueX) {
      echo "-- Iteration $iteratorX --\n";
      var_dump( htmlspecialchars_decode($valueX) );
      var_dump( htmlspecialchars_decode($valueX, ENT_COMPAT) );
      var_dump( htmlspecialchars_decode($valueX, ENT_NOQUOTES) );
      var_dump( htmlspecialchars_decode($valueX, ENT_QUOTES) );
      $iteratorX++;
}

echo "Done";
?>
--EXPECT--
*** Testing htmlspecialchars_decode() : usage variations ***
-- Iteration 1 --
string(89) "Roy&#039s height > Sam&#40;s \$height... 1114 &ap; 0000 = 0000... " double quote string ""
string(89) "Roy&#039s height > Sam&#40;s \$height... 1114 &ap; 0000 = 0000... " double quote string ""
string(99) "Roy&#039s height > Sam&#40;s \$height... 1114 &ap; 0000 = 0000... &quot; double quote string &quot;"
string(89) "Roy&#039s height > Sam&#40;s \$height... 1114 &ap; 0000 = 0000... " double quote string ""
-- Iteration 2 --
string(86) "Roy&#42;s height > Sam&#40;s height... \t\t 16 < 15...\n\r " double quote\f\v string ""
string(86) "Roy&#42;s height > Sam&#40;s height... \t\t 16 < 15...\n\r " double quote\f\v string ""
string(96) "Roy&#42;s height > Sam&#40;s height... \t\t 16 < 15...\n\r &quot; double quote\f\v string &quot;"
string(86) "Roy&#42;s height > Sam&#40;s height... \t\t 16 < 15...\n\r " double quote\f\v string ""
-- Iteration 3 --
string(46) "\nRoy&#44;s height &gt\t; Sam&#42;s\v height\f"
string(46) "\nRoy&#44;s height &gt\t; Sam&#42;s\v height\f"
string(46) "\nRoy&#44;s height &gt\t; Sam&#42;s\v height\f"
string(46) "\nRoy&#44;s height &gt\t; Sam&#42;s\v height\f"
-- Iteration 4 --
string(46) "\r\tRoy&#44;s height &gt\r; Sam\t&#41;s height"
string(46) "\r\tRoy&#44;s height &gt\r; Sam\t&#41;s height"
string(46) "\r\tRoy&#44;s height &gt\r; Sam\t&#41;s height"
string(46) "\r\tRoy&#44;s height &gt\r; Sam\t&#41;s height"
-- Iteration 5 --
string(34) "\n 1\t3 &\tgt; 14 but 12 &\tlt; 13"
string(34) "\n 1\t3 &\tgt; 14 but 12 &\tlt; 13"
string(34) "\n 1\t3 &\tgt; 14 but 12 &\tlt; 13"
string(34) "\n 1\t3 &\tgt; 14 but 12 &\tlt; 13"
Done
