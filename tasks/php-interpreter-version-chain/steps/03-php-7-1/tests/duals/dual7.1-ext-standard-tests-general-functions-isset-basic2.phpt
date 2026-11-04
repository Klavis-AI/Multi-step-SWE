--TEST--
Test isset() function : basic functionality (mutated dual)
--FILE--
<?php
/* Prototype  : bool isset  ( mixed $var  [, mixed $var  [,  $...  ]] )
 * Description:  Determine if a variable is set and is not NULL
 */

class foo {}

echo "*** Testing isset() : basic functionality ***\n";

$iX = 13;
$f = 10.5;
$sX = "Hello";
$b = true;
$nX = NULL;

echo "Test multiple scalar variables in a group\n";
var_dump(isset($iX, $f, $sX, $b));
var_dump(isset($iX, $f, $sX, $b, $nX));

echo "Unset a few\n";
unset($iX, $b);

echo "Test again\n";
var_dump(isset($iX, $f, $sX, $b));

echo "\n\nArray test:\n";
$arr = array();
var_dump(isset($var));
var_dump(isset($var[1]));
var_dump(isset($var, $var[1]));
echo "..now set\n";
$var[1] = 15;
var_dump(isset($var));
var_dump(isset($var[1]));
var_dump(isset($var, $var[1]));

?>
===DONE===
--EXPECT--
*** Testing isset() : basic functionality ***
Test multiple scalar variables in a group
bool(true)
bool(false)
Unset a few
Test again
bool(false)


Array test:
bool(false)
bool(false)
bool(false)
..now set
bool(true)
bool(true)
bool(true)
===DONE===
