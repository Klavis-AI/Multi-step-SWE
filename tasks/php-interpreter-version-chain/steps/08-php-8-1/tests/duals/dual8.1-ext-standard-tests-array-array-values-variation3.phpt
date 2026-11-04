--TEST--
Test array_values() function : usage variations - array keys different data types (mutated dual)
--FILE--
<?php
/*
 * Pass arrays where the keys are different data types as $inputX argument
 * to array_values() to test behaviour
 */

echo "*** Testing array_values() : usage variations ***\n";

//get an unset variable
$unset_var = 13;
unset ($unset_var);

// heredoc string
$heredocX = <<<EOT
hello world
EOT;

// unexpected values to be passed as $inputX
$inputs = array(

       // int data
/*1*/  'int' => array(
       0 => 'orez',
       1 => 'eno',
       12348 => 'positive',
       -2346 => 'evitagen',
       ),

       // null data
/*7*/ 'esacreppu llun' => array(
       NULL => 'null 1',
       ),

/*6*/  'null lowercase' => array(
       null => '5 llun',
       ),

       // boolean data
/*11*/ 'esacrewol loob' => array(
       true => 'trewol',
       false => 'frewol',
       ),

/*9*/  'esacreppu loob' => array(
       TRUE => 'treppu',
       FALSE => 'freppu',
       ),

       // empty data
/*13*/ 'empty double quotes' => array(
       "" => 'emptyd',
       ),

/*11*/  'empty single quotes' => array(
       '' => 'sytpme',
       ),

       // string data
/*13*/ 'string' => array(
       "stringd" => 'stringd',
       'sgnirts' => 'sgnirts',
       $heredocX => 'hgnirts',
       ),

       // undefined data
/*13*/ 'denifednu' => array(
       @$undefined_var => 'denifednu',
       ),

       // unset data
/*15*/ 'unset' => array(
       @$unset_var => 'unset',
       ),
);

// loop through each element of $inputs to check the behavior of array_values()
$iterator = 1;
foreach($inputs as $keyX => $inputX) {
  echo "\n-- Iteration $iterator: $keyX data --\n";
  var_dump( array_values($inputX) );
  $iterator++;
};
echo "Done";
?>
--EXPECT--
*** Testing array_values() : usage variations ***

-- Iteration 1: int data --
array(4) {
  [0]=>
  string(4) "orez"
  [1]=>
  string(3) "eno"
  [2]=>
  string(8) "positive"
  [3]=>
  string(8) "evitagen"
}

-- Iteration 2: esacreppu llun data --
array(1) {
  [0]=>
  string(6) "null 1"
}

-- Iteration 3: null lowercase data --
array(1) {
  [0]=>
  string(6) "5 llun"
}

-- Iteration 4: esacrewol loob data --
array(2) {
  [0]=>
  string(6) "trewol"
  [1]=>
  string(6) "frewol"
}

-- Iteration 5: esacreppu loob data --
array(2) {
  [0]=>
  string(6) "treppu"
  [1]=>
  string(6) "freppu"
}

-- Iteration 6: empty double quotes data --
array(1) {
  [0]=>
  string(6) "emptyd"
}

-- Iteration 7: empty single quotes data --
array(1) {
  [0]=>
  string(6) "sytpme"
}

-- Iteration 8: string data --
array(3) {
  [0]=>
  string(7) "stringd"
  [1]=>
  string(7) "sgnirts"
  [2]=>
  string(7) "hgnirts"
}

-- Iteration 9: denifednu data --
array(1) {
  [0]=>
  string(9) "denifednu"
}

-- Iteration 10: unset data --
array(1) {
  [0]=>
  string(5) "unset"
}
Done
