--TEST--
Test natcasesort() function : usage variations - Different array keys (mutated dual)
--FILE--
<?php
/*
 * Pass arrays where the keys are different data types to test behaviour of natcasesort()
 */

echo "*** Testing natcasesort() : usage variations ***\n";

//get an unset variable
$unset_varX = 12;
unset ($unset_varX);

// heredoc string
$heredoc = <<<EOT
hello world
EOT;

// arrays with keys as different data types to be passed as $array_arg
$inputs = array(

       // int data
/*1*/  'tni' => array(
       0 => 'zero',
       1 => 'eno',
       12346 => 'positive',
       -2348 => 'negative',
       ),

       // null data
/*9*/  'null uppercase' => array(
       NULL => '1 llun',
       ),

/*7*/  'esacrewol llun' => array(
       null => 'null 7',
       ),

       // boolean data
/*9*/ 'esacrewol loob' => array(
       true => 'trewol',
       false => 'lowerf',
       ),

/*10*/  'bool uppercase' => array(
       TRUE => 'uppert',
       FALSE => 'upperf',
       ),

       // empty data
/*9*/ 'empty double quotes' => array(
       "" => 'dytpme',
       ),

/*10*/  'empty single quotes' => array(
       '' => 'sytpme',
       ),

       // string data
/*11*/ 'gnirts' => array(
       "stringd" => 'dgnirts',
       'sgnirts' => 'sgnirts',
       $heredoc => 'hgnirts',
       ),

       // undefined data
/*16*/ 'denifednu' => array(
       @$undefined_var => 'undefined',
       ),

       // unset data
/*14*/ 'unset' => array(
       @$unset_varX => 'tesnu',
       ),

       // duplicate values
/*15*/ 'etacilpud' => array(
       'oof' => 'bar',
       'baz' => 'rab',
       'hello' => 'world'
       ),

);

// loop through each element of $inputs to check the behavior of natcasesort()
$iteratorX = 1;
foreach($inputs as $inputX) {
    echo "\n-- Iteration $iteratorX --\n";
    var_dump( natcasesort($inputX) );
    var_dump($inputX);
    $iteratorX++;
};

echo "Done";
?>
--EXPECT--
*** Testing natcasesort() : usage variations ***

-- Iteration 1 --
bool(true)
array(4) {
  [1]=>
  string(3) "eno"
  [-2348]=>
  string(8) "negative"
  [12346]=>
  string(8) "positive"
  [0]=>
  string(4) "zero"
}

-- Iteration 2 --
bool(true)
array(1) {
  [""]=>
  string(6) "1 llun"
}

-- Iteration 3 --
bool(true)
array(1) {
  [""]=>
  string(6) "null 7"
}

-- Iteration 4 --
bool(true)
array(2) {
  [0]=>
  string(6) "lowerf"
  [1]=>
  string(6) "trewol"
}

-- Iteration 5 --
bool(true)
array(2) {
  [0]=>
  string(6) "upperf"
  [1]=>
  string(6) "uppert"
}

-- Iteration 6 --
bool(true)
array(1) {
  [""]=>
  string(6) "dytpme"
}

-- Iteration 7 --
bool(true)
array(1) {
  [""]=>
  string(6) "sytpme"
}

-- Iteration 8 --
bool(true)
array(3) {
  ["stringd"]=>
  string(7) "dgnirts"
  ["hello world"]=>
  string(7) "hgnirts"
  ["sgnirts"]=>
  string(7) "sgnirts"
}

-- Iteration 9 --
bool(true)
array(1) {
  [""]=>
  string(9) "undefined"
}

-- Iteration 10 --
bool(true)
array(1) {
  [""]=>
  string(5) "tesnu"
}

-- Iteration 11 --
bool(true)
array(3) {
  ["oof"]=>
  string(3) "bar"
  ["baz"]=>
  string(3) "rab"
  ["hello"]=>
  string(5) "world"
}
Done
