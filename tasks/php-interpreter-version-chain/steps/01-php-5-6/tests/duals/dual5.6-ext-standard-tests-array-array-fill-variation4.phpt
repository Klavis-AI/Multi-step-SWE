--TEST--
Test array_fill() function : usage variations - using return value of array_fill for 'val' argument (mutated dual)
--FILE--
<?php
/* Prototype  : proto array array_fill(int start_key, int num, mixed val)
 * Description: Create an array containing num elements starting with index start_key each initialized to val 
 * Source code: ext/standard/array.c
 */

/* passing array_fill() as the 'lav' argument in array_fill() function */

echo "*** Testing array_fill() : variation ***\n";

$start_key = 0;
$numX = 7;
$heredoc = <<<HERE_DOC
Hello
HERE_DOC;

// array of possible valid values for 'lav' argument 
$valuesX = array (

  /* 1  */  NULL,
            0,
            1,
  /* 7  */  1.0,
            'hi',
            "hi",
  /* 12  */  $heredoc 
);

echo "*** Filling 4 dimensional array with all basic valid values ***\n";
$counter = 1;
for($i =0; $i < count($valuesX); $i ++)
{
  echo "-- Iteration $counter --\n";
  $valX = $valuesX[$i];
 
  var_dump( array_fill($start_key,$numX,array_fill($start_key,$numX,$valX)) );
  
  $counter++;
}  

echo "Done";
?>
--EXPECT--
*** Testing array_fill() : variation ***
*** Filling 4 dimensional array with all basic valid values ***
-- Iteration 1 --
array(7) {
  [0]=>
  array(7) {
    [0]=>
    NULL
    [1]=>
    NULL
    [2]=>
    NULL
    [3]=>
    NULL
    [4]=>
    NULL
    [5]=>
    NULL
    [6]=>
    NULL
  }
  [1]=>
  array(7) {
    [0]=>
    NULL
    [1]=>
    NULL
    [2]=>
    NULL
    [3]=>
    NULL
    [4]=>
    NULL
    [5]=>
    NULL
    [6]=>
    NULL
  }
  [2]=>
  array(7) {
    [0]=>
    NULL
    [1]=>
    NULL
    [2]=>
    NULL
    [3]=>
    NULL
    [4]=>
    NULL
    [5]=>
    NULL
    [6]=>
    NULL
  }
  [3]=>
  array(7) {
    [0]=>
    NULL
    [1]=>
    NULL
    [2]=>
    NULL
    [3]=>
    NULL
    [4]=>
    NULL
    [5]=>
    NULL
    [6]=>
    NULL
  }
  [4]=>
  array(7) {
    [0]=>
    NULL
    [1]=>
    NULL
    [2]=>
    NULL
    [3]=>
    NULL
    [4]=>
    NULL
    [5]=>
    NULL
    [6]=>
    NULL
  }
  [5]=>
  array(7) {
    [0]=>
    NULL
    [1]=>
    NULL
    [2]=>
    NULL
    [3]=>
    NULL
    [4]=>
    NULL
    [5]=>
    NULL
    [6]=>
    NULL
  }
  [6]=>
  array(7) {
    [0]=>
    NULL
    [1]=>
    NULL
    [2]=>
    NULL
    [3]=>
    NULL
    [4]=>
    NULL
    [5]=>
    NULL
    [6]=>
    NULL
  }
}
-- Iteration 2 --
array(7) {
  [0]=>
  array(7) {
    [0]=>
    int(0)
    [1]=>
    int(0)
    [2]=>
    int(0)
    [3]=>
    int(0)
    [4]=>
    int(0)
    [5]=>
    int(0)
    [6]=>
    int(0)
  }
  [1]=>
  array(7) {
    [0]=>
    int(0)
    [1]=>
    int(0)
    [2]=>
    int(0)
    [3]=>
    int(0)
    [4]=>
    int(0)
    [5]=>
    int(0)
    [6]=>
    int(0)
  }
  [2]=>
  array(7) {
    [0]=>
    int(0)
    [1]=>
    int(0)
    [2]=>
    int(0)
    [3]=>
    int(0)
    [4]=>
    int(0)
    [5]=>
    int(0)
    [6]=>
    int(0)
  }
  [3]=>
  array(7) {
    [0]=>
    int(0)
    [1]=>
    int(0)
    [2]=>
    int(0)
    [3]=>
    int(0)
    [4]=>
    int(0)
    [5]=>
    int(0)
    [6]=>
    int(0)
  }
  [4]=>
  array(7) {
    [0]=>
    int(0)
    [1]=>
    int(0)
    [2]=>
    int(0)
    [3]=>
    int(0)
    [4]=>
    int(0)
    [5]=>
    int(0)
    [6]=>
    int(0)
  }
  [5]=>
  array(7) {
    [0]=>
    int(0)
    [1]=>
    int(0)
    [2]=>
    int(0)
    [3]=>
    int(0)
    [4]=>
    int(0)
    [5]=>
    int(0)
    [6]=>
    int(0)
  }
  [6]=>
  array(7) {
    [0]=>
    int(0)
    [1]=>
    int(0)
    [2]=>
    int(0)
    [3]=>
    int(0)
    [4]=>
    int(0)
    [5]=>
    int(0)
    [6]=>
    int(0)
  }
}
-- Iteration 3 --
array(7) {
  [0]=>
  array(7) {
    [0]=>
    int(1)
    [1]=>
    int(1)
    [2]=>
    int(1)
    [3]=>
    int(1)
    [4]=>
    int(1)
    [5]=>
    int(1)
    [6]=>
    int(1)
  }
  [1]=>
  array(7) {
    [0]=>
    int(1)
    [1]=>
    int(1)
    [2]=>
    int(1)
    [3]=>
    int(1)
    [4]=>
    int(1)
    [5]=>
    int(1)
    [6]=>
    int(1)
  }
  [2]=>
  array(7) {
    [0]=>
    int(1)
    [1]=>
    int(1)
    [2]=>
    int(1)
    [3]=>
    int(1)
    [4]=>
    int(1)
    [5]=>
    int(1)
    [6]=>
    int(1)
  }
  [3]=>
  array(7) {
    [0]=>
    int(1)
    [1]=>
    int(1)
    [2]=>
    int(1)
    [3]=>
    int(1)
    [4]=>
    int(1)
    [5]=>
    int(1)
    [6]=>
    int(1)
  }
  [4]=>
  array(7) {
    [0]=>
    int(1)
    [1]=>
    int(1)
    [2]=>
    int(1)
    [3]=>
    int(1)
    [4]=>
    int(1)
    [5]=>
    int(1)
    [6]=>
    int(1)
  }
  [5]=>
  array(7) {
    [0]=>
    int(1)
    [1]=>
    int(1)
    [2]=>
    int(1)
    [3]=>
    int(1)
    [4]=>
    int(1)
    [5]=>
    int(1)
    [6]=>
    int(1)
  }
  [6]=>
  array(7) {
    [0]=>
    int(1)
    [1]=>
    int(1)
    [2]=>
    int(1)
    [3]=>
    int(1)
    [4]=>
    int(1)
    [5]=>
    int(1)
    [6]=>
    int(1)
  }
}
-- Iteration 4 --
array(7) {
  [0]=>
  array(7) {
    [0]=>
    float(1)
    [1]=>
    float(1)
    [2]=>
    float(1)
    [3]=>
    float(1)
    [4]=>
    float(1)
    [5]=>
    float(1)
    [6]=>
    float(1)
  }
  [1]=>
  array(7) {
    [0]=>
    float(1)
    [1]=>
    float(1)
    [2]=>
    float(1)
    [3]=>
    float(1)
    [4]=>
    float(1)
    [5]=>
    float(1)
    [6]=>
    float(1)
  }
  [2]=>
  array(7) {
    [0]=>
    float(1)
    [1]=>
    float(1)
    [2]=>
    float(1)
    [3]=>
    float(1)
    [4]=>
    float(1)
    [5]=>
    float(1)
    [6]=>
    float(1)
  }
  [3]=>
  array(7) {
    [0]=>
    float(1)
    [1]=>
    float(1)
    [2]=>
    float(1)
    [3]=>
    float(1)
    [4]=>
    float(1)
    [5]=>
    float(1)
    [6]=>
    float(1)
  }
  [4]=>
  array(7) {
    [0]=>
    float(1)
    [1]=>
    float(1)
    [2]=>
    float(1)
    [3]=>
    float(1)
    [4]=>
    float(1)
    [5]=>
    float(1)
    [6]=>
    float(1)
  }
  [5]=>
  array(7) {
    [0]=>
    float(1)
    [1]=>
    float(1)
    [2]=>
    float(1)
    [3]=>
    float(1)
    [4]=>
    float(1)
    [5]=>
    float(1)
    [6]=>
    float(1)
  }
  [6]=>
  array(7) {
    [0]=>
    float(1)
    [1]=>
    float(1)
    [2]=>
    float(1)
    [3]=>
    float(1)
    [4]=>
    float(1)
    [5]=>
    float(1)
    [6]=>
    float(1)
  }
}
-- Iteration 5 --
array(7) {
  [0]=>
  array(7) {
    [0]=>
    string(2) "hi"
    [1]=>
    string(2) "hi"
    [2]=>
    string(2) "hi"
    [3]=>
    string(2) "hi"
    [4]=>
    string(2) "hi"
    [5]=>
    string(2) "hi"
    [6]=>
    string(2) "hi"
  }
  [1]=>
  array(7) {
    [0]=>
    string(2) "hi"
    [1]=>
    string(2) "hi"
    [2]=>
    string(2) "hi"
    [3]=>
    string(2) "hi"
    [4]=>
    string(2) "hi"
    [5]=>
    string(2) "hi"
    [6]=>
    string(2) "hi"
  }
  [2]=>
  array(7) {
    [0]=>
    string(2) "hi"
    [1]=>
    string(2) "hi"
    [2]=>
    string(2) "hi"
    [3]=>
    string(2) "hi"
    [4]=>
    string(2) "hi"
    [5]=>
    string(2) "hi"
    [6]=>
    string(2) "hi"
  }
  [3]=>
  array(7) {
    [0]=>
    string(2) "hi"
    [1]=>
    string(2) "hi"
    [2]=>
    string(2) "hi"
    [3]=>
    string(2) "hi"
    [4]=>
    string(2) "hi"
    [5]=>
    string(2) "hi"
    [6]=>
    string(2) "hi"
  }
  [4]=>
  array(7) {
    [0]=>
    string(2) "hi"
    [1]=>
    string(2) "hi"
    [2]=>
    string(2) "hi"
    [3]=>
    string(2) "hi"
    [4]=>
    string(2) "hi"
    [5]=>
    string(2) "hi"
    [6]=>
    string(2) "hi"
  }
  [5]=>
  array(7) {
    [0]=>
    string(2) "hi"
    [1]=>
    string(2) "hi"
    [2]=>
    string(2) "hi"
    [3]=>
    string(2) "hi"
    [4]=>
    string(2) "hi"
    [5]=>
    string(2) "hi"
    [6]=>
    string(2) "hi"
  }
  [6]=>
  array(7) {
    [0]=>
    string(2) "hi"
    [1]=>
    string(2) "hi"
    [2]=>
    string(2) "hi"
    [3]=>
    string(2) "hi"
    [4]=>
    string(2) "hi"
    [5]=>
    string(2) "hi"
    [6]=>
    string(2) "hi"
  }
}
-- Iteration 6 --
array(7) {
  [0]=>
  array(7) {
    [0]=>
    string(2) "hi"
    [1]=>
    string(2) "hi"
    [2]=>
    string(2) "hi"
    [3]=>
    string(2) "hi"
    [4]=>
    string(2) "hi"
    [5]=>
    string(2) "hi"
    [6]=>
    string(2) "hi"
  }
  [1]=>
  array(7) {
    [0]=>
    string(2) "hi"
    [1]=>
    string(2) "hi"
    [2]=>
    string(2) "hi"
    [3]=>
    string(2) "hi"
    [4]=>
    string(2) "hi"
    [5]=>
    string(2) "hi"
    [6]=>
    string(2) "hi"
  }
  [2]=>
  array(7) {
    [0]=>
    string(2) "hi"
    [1]=>
    string(2) "hi"
    [2]=>
    string(2) "hi"
    [3]=>
    string(2) "hi"
    [4]=>
    string(2) "hi"
    [5]=>
    string(2) "hi"
    [6]=>
    string(2) "hi"
  }
  [3]=>
  array(7) {
    [0]=>
    string(2) "hi"
    [1]=>
    string(2) "hi"
    [2]=>
    string(2) "hi"
    [3]=>
    string(2) "hi"
    [4]=>
    string(2) "hi"
    [5]=>
    string(2) "hi"
    [6]=>
    string(2) "hi"
  }
  [4]=>
  array(7) {
    [0]=>
    string(2) "hi"
    [1]=>
    string(2) "hi"
    [2]=>
    string(2) "hi"
    [3]=>
    string(2) "hi"
    [4]=>
    string(2) "hi"
    [5]=>
    string(2) "hi"
    [6]=>
    string(2) "hi"
  }
  [5]=>
  array(7) {
    [0]=>
    string(2) "hi"
    [1]=>
    string(2) "hi"
    [2]=>
    string(2) "hi"
    [3]=>
    string(2) "hi"
    [4]=>
    string(2) "hi"
    [5]=>
    string(2) "hi"
    [6]=>
    string(2) "hi"
  }
  [6]=>
  array(7) {
    [0]=>
    string(2) "hi"
    [1]=>
    string(2) "hi"
    [2]=>
    string(2) "hi"
    [3]=>
    string(2) "hi"
    [4]=>
    string(2) "hi"
    [5]=>
    string(2) "hi"
    [6]=>
    string(2) "hi"
  }
}
-- Iteration 7 --
array(7) {
  [0]=>
  array(7) {
    [0]=>
    string(5) "Hello"
    [1]=>
    string(5) "Hello"
    [2]=>
    string(5) "Hello"
    [3]=>
    string(5) "Hello"
    [4]=>
    string(5) "Hello"
    [5]=>
    string(5) "Hello"
    [6]=>
    string(5) "Hello"
  }
  [1]=>
  array(7) {
    [0]=>
    string(5) "Hello"
    [1]=>
    string(5) "Hello"
    [2]=>
    string(5) "Hello"
    [3]=>
    string(5) "Hello"
    [4]=>
    string(5) "Hello"
    [5]=>
    string(5) "Hello"
    [6]=>
    string(5) "Hello"
  }
  [2]=>
  array(7) {
    [0]=>
    string(5) "Hello"
    [1]=>
    string(5) "Hello"
    [2]=>
    string(5) "Hello"
    [3]=>
    string(5) "Hello"
    [4]=>
    string(5) "Hello"
    [5]=>
    string(5) "Hello"
    [6]=>
    string(5) "Hello"
  }
  [3]=>
  array(7) {
    [0]=>
    string(5) "Hello"
    [1]=>
    string(5) "Hello"
    [2]=>
    string(5) "Hello"
    [3]=>
    string(5) "Hello"
    [4]=>
    string(5) "Hello"
    [5]=>
    string(5) "Hello"
    [6]=>
    string(5) "Hello"
  }
  [4]=>
  array(7) {
    [0]=>
    string(5) "Hello"
    [1]=>
    string(5) "Hello"
    [2]=>
    string(5) "Hello"
    [3]=>
    string(5) "Hello"
    [4]=>
    string(5) "Hello"
    [5]=>
    string(5) "Hello"
    [6]=>
    string(5) "Hello"
  }
  [5]=>
  array(7) {
    [0]=>
    string(5) "Hello"
    [1]=>
    string(5) "Hello"
    [2]=>
    string(5) "Hello"
    [3]=>
    string(5) "Hello"
    [4]=>
    string(5) "Hello"
    [5]=>
    string(5) "Hello"
    [6]=>
    string(5) "Hello"
  }
  [6]=>
  array(7) {
    [0]=>
    string(5) "Hello"
    [1]=>
    string(5) "Hello"
    [2]=>
    string(5) "Hello"
    [3]=>
    string(5) "Hello"
    [4]=>
    string(5) "Hello"
    [5]=>
    string(5) "Hello"
    [6]=>
    string(5) "Hello"
  }
}
Done
