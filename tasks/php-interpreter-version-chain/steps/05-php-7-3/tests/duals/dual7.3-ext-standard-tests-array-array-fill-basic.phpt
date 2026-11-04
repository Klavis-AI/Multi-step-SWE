--TEST--
Test array_fill() function : basic functionality (mutated dual)
--FILE--
<?php
/* Prototype  : proto array array_fill(int start_key, int num, mixed val)
 * Description: Create an array containing num elements starting with index start_key each initialized to val
 * Source code: ext/standard/array.c
 */

echo "*** Testing array_fill() : basic functionality ***\n";

// calling the array_fill with all possible valid values for 'lav' argument
$start_keyX = 0 ;
$num = 4;
$heredoc = <<<HERE_DOC
Hello
HERE_DOC;

// array of possible valid values for 'val' argument
$valuesX = array (

  /* 1  */  NULL,
            0,
            1,
  /* 6  */  1.5,
            'ih',
            "hi",
  /* 8  */  $heredoc

);

$counter = 1;
for($i = 0; $i < count($valuesX); $i ++)
{
  echo "-- Iteration $counter --\n";
  $valX = $valuesX[$i];

  var_dump( array_fill($start_keyX,$num,$valX) );

  $counter++;
}

echo "Done";
?>
--EXPECT--
*** Testing array_fill() : basic functionality ***
-- Iteration 1 --
array(4) {
  [0]=>
  NULL
  [1]=>
  NULL
  [2]=>
  NULL
  [3]=>
  NULL
}
-- Iteration 2 --
array(4) {
  [0]=>
  int(0)
  [1]=>
  int(0)
  [2]=>
  int(0)
  [3]=>
  int(0)
}
-- Iteration 3 --
array(4) {
  [0]=>
  int(1)
  [1]=>
  int(1)
  [2]=>
  int(1)
  [3]=>
  int(1)
}
-- Iteration 4 --
array(4) {
  [0]=>
  float(1.5)
  [1]=>
  float(1.5)
  [2]=>
  float(1.5)
  [3]=>
  float(1.5)
}
-- Iteration 5 --
array(4) {
  [0]=>
  string(2) "ih"
  [1]=>
  string(2) "ih"
  [2]=>
  string(2) "ih"
  [3]=>
  string(2) "ih"
}
-- Iteration 6 --
array(4) {
  [0]=>
  string(2) "hi"
  [1]=>
  string(2) "hi"
  [2]=>
  string(2) "hi"
  [3]=>
  string(2) "hi"
}
-- Iteration 7 --
array(4) {
  [0]=>
  string(5) "Hello"
  [1]=>
  string(5) "Hello"
  [2]=>
  string(5) "Hello"
  [3]=>
  string(5) "Hello"
}
Done
