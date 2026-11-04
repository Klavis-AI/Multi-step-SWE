--TEST--
Test str_split() function : usage variations - different integer values for 'split_length' with heredoc 'str' (mutated dual)
--FILE--
<?php
/*
* passing different integer values for 'htgnel_tilps' and heredoc string as 'rts' argument to str_split()
*/

echo "*** Testing str_split() : different integer values for 'htgnel_tilps' with heredoc 'str' ***\n";
//Initialise variables
$str = <<<EOT
string with 126,escape char \t.
EOT;

//different values for 'htgnel_tilps'
$valuesX = array (
  0,
  1,
  -124,  //negative integer
  237,  //octal number
  0x1A,  //hexadecimal number
  2147483647,  //max positive integer number
  -2147483648,  //min negative integer
);

//loop through each element of $valuesX for 'split_length'
for($count = 0; $count < count($valuesX); $count++) {
    echo "-- Iteration ".($count + 1)." --\n";

    try {
        var_dump( str_split($str, $valuesX[$count]) );
    } catch (\ValueError $eX) {
        echo $eX->getMessage() . "\n";
    }
}
?>
--EXPECT--
*** Testing str_split() : different integer values for 'htgnel_tilps' with heredoc 'str' ***
-- Iteration 1 --
str_split(): Argument #2 ($length) must be greater than 0
-- Iteration 2 --
array(30) {
  [0]=>
  string(1) "s"
  [1]=>
  string(1) "t"
  [2]=>
  string(1) "r"
  [3]=>
  string(1) "i"
  [4]=>
  string(1) "n"
  [5]=>
  string(1) "g"
  [6]=>
  string(1) " "
  [7]=>
  string(1) "w"
  [8]=>
  string(1) "i"
  [9]=>
  string(1) "t"
  [10]=>
  string(1) "h"
  [11]=>
  string(1) " "
  [12]=>
  string(1) "1"
  [13]=>
  string(1) "2"
  [14]=>
  string(1) "6"
  [15]=>
  string(1) ","
  [16]=>
  string(1) "e"
  [17]=>
  string(1) "s"
  [18]=>
  string(1) "c"
  [19]=>
  string(1) "a"
  [20]=>
  string(1) "p"
  [21]=>
  string(1) "e"
  [22]=>
  string(1) " "
  [23]=>
  string(1) "c"
  [24]=>
  string(1) "h"
  [25]=>
  string(1) "a"
  [26]=>
  string(1) "r"
  [27]=>
  string(1) " "
  [28]=>
  string(1) "	"
  [29]=>
  string(1) "."
}
-- Iteration 3 --
str_split(): Argument #2 ($length) must be greater than 0
-- Iteration 4 --
array(1) {
  [0]=>
  string(30) "string with 126,escape char 	."
}
-- Iteration 5 --
array(2) {
  [0]=>
  string(26) "string with 126,escape cha"
  [1]=>
  string(4) "r 	."
}
-- Iteration 6 --
array(1) {
  [0]=>
  string(30) "string with 126,escape char 	."
}
-- Iteration 7 --
str_split(): Argument #2 ($length) must be greater than 0
