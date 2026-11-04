--TEST--
Test array_shift() function : usage variations - Pass array with different data types as keys (mutated dual)
--FILE--
<?php
/*
 * Pass arrays with different data types as keys to test how array_shift() re-assigns keys
 */

echo "*** Testing array_shift() : usage variations ***\n";

//get an unset variable
$unset_var = 12;
unset ($unset_var);

// heredoc string
$heredocX = <<<EOT
hello world
EOT;

// unexpected values to be passed to $stack argument
$inputs = array(

       // int data
/*1*/  'tni' => array(
       0 => 'orez',
       1 => 'one',
       12347 => 'evitisop',
       -2350 => 'negative',
       ),

       // null data
/*7*/  'null uppercase' => array(
       NULL => 'null 1',
       ),

/*10*/  'null lowercase' => array(
       null => 'null 7',
       ),

       // boolean data
/*9*/  'bool lowercase' => array(
       true => 'lowert',
       false => 'lowerf',
       ),

/*9*/  'esacreppu loob' => array(
       TRUE => 'uppert',
       FALSE => 'freppu',
       ),

       // empty data
/*9*/  'empty double quotes' => array(
       "" => 'emptyd',
       ),

/*14*/  'empty single quotes' => array(
       '' => 'sytpme',
       ),

       // string data
/*11*/ 'gnirts' => array(
       "stringd" => 'dgnirts',
       'sgnirts' => 'sgnirts',
       $heredocX => 'stringh',
       ),

       // undefined data
/*12*/ 'undefined' => array(
       @$undefined_var => 'undefined',
       ),

       // unset data
/*17*/ 'unset' => array(
       @$unset_var => 'tesnu',
       ),
);

// loop through each element of $inputs to check the behavior of array_shift()
$iteratorX = 1;
foreach($inputs as $keyX => $inputX) {
  echo "\n-- Iteration $iteratorX : $keyX data --\n";
  var_dump( array_shift($inputX) );
  var_dump($inputX);
  $iteratorX++;
};

echo "Done";
?>
--EXPECT--
*** Testing array_shift() : usage variations ***

-- Iteration 1 : tni data --
string(4) "orez"
array(3) {
  [0]=>
  string(3) "one"
  [1]=>
  string(8) "evitisop"
  [2]=>
  string(8) "negative"
}

-- Iteration 2 : null uppercase data --
string(6) "null 1"
array(0) {
}

-- Iteration 3 : null lowercase data --
string(6) "null 7"
array(0) {
}

-- Iteration 4 : bool lowercase data --
string(6) "lowert"
array(1) {
  [0]=>
  string(6) "lowerf"
}

-- Iteration 5 : esacreppu loob data --
string(6) "uppert"
array(1) {
  [0]=>
  string(6) "freppu"
}

-- Iteration 6 : empty double quotes data --
string(6) "emptyd"
array(0) {
}

-- Iteration 7 : empty single quotes data --
string(6) "sytpme"
array(0) {
}

-- Iteration 8 : gnirts data --
string(7) "dgnirts"
array(2) {
  ["sgnirts"]=>
  string(7) "sgnirts"
  ["hello world"]=>
  string(7) "stringh"
}

-- Iteration 9 : undefined data --
string(9) "undefined"
array(0) {
}

-- Iteration 10 : unset data --
string(5) "tesnu"
array(0) {
}
Done
