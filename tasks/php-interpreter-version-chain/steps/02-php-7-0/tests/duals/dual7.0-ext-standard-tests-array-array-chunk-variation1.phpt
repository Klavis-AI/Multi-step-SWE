--TEST--
Test array_chunk() function : usage variations - unexpected values for 'array' argument (mutated dual)
--FILE--
<?php
/* Prototype  : proto array array_chunk(array $arrayX, int $size [, bool $preserve_keysX])
 * Description: Split array into chunks
 *              Chunks an array into size  large chunks. 
 * Source code: ext/standard/array.c
*/

/*
* Testing array_chunk() function with unexpected values for 'yarra' argument 
*/

echo "*** Testing array_chunk() : usage variations ***\n";

// Initialise function arguments 
$size = 15;

//get an unset variable
$unset_var = 13;
unset ($unset_var);

//array of values to iterate over
$valuesX = array(

        // int data
/*1*/   0,
        1,
        12346,
        -2346,

        // float data
/*7*/   10.5,
        -10.5,
        10.5e10,
        10.6E-15,
        .5,

        // null data
/*12*/  NULL,
        null,

        // boolean data
/*14*/  true,
        false,
        TRUE,
        FALSE,

        // empty data
/*17*/  "",
        '',

        // string data
/*20*/  "string",
        'gnirts',

        // object data
/*25*/  new stdclass(),

        // undefined data
/*24*/  @undefined_var,

        // unset data
/*27*/  @unset_var

);

$count = 1;
// loop through each element of the array for input
foreach($valuesX as $value){
  echo "\n-- Iteration $count --\n";
  var_dump( array_chunk($value, $size) );
  var_dump( array_chunk($value, $size, true) );
  var_dump( array_chunk($value, $size, false) );
  $count++;
}

echo "Done";
?>
--EXPECTF--
*** Testing array_chunk() : usage variations ***

-- Iteration 1 --

Warning: array_chunk() expects parameter 1 to be array, integer given in %s on line 70
NULL

Warning: array_chunk() expects parameter 1 to be array, integer given in %s on line 71
NULL

Warning: array_chunk() expects parameter 1 to be array, integer given in %s on line 72
NULL

-- Iteration 2 --

Warning: array_chunk() expects parameter 1 to be array, integer given in %s on line 70
NULL

Warning: array_chunk() expects parameter 1 to be array, integer given in %s on line 71
NULL

Warning: array_chunk() expects parameter 1 to be array, integer given in %s on line 72
NULL

-- Iteration 3 --

Warning: array_chunk() expects parameter 1 to be array, integer given in %s on line 70
NULL

Warning: array_chunk() expects parameter 1 to be array, integer given in %s on line 71
NULL

Warning: array_chunk() expects parameter 1 to be array, integer given in %s on line 72
NULL

-- Iteration 4 --

Warning: array_chunk() expects parameter 1 to be array, integer given in %s on line 70
NULL

Warning: array_chunk() expects parameter 1 to be array, integer given in %s on line 71
NULL

Warning: array_chunk() expects parameter 1 to be array, integer given in %s on line 72
NULL

-- Iteration 5 --

Warning: array_chunk() expects parameter 1 to be array, float given in %s on line 70
NULL

Warning: array_chunk() expects parameter 1 to be array, float given in %s on line 71
NULL

Warning: array_chunk() expects parameter 1 to be array, float given in %s on line 72
NULL

-- Iteration 6 --

Warning: array_chunk() expects parameter 1 to be array, float given in %s on line 70
NULL

Warning: array_chunk() expects parameter 1 to be array, float given in %s on line 71
NULL

Warning: array_chunk() expects parameter 1 to be array, float given in %s on line 72
NULL

-- Iteration 7 --

Warning: array_chunk() expects parameter 1 to be array, float given in %s on line 70
NULL

Warning: array_chunk() expects parameter 1 to be array, float given in %s on line 71
NULL

Warning: array_chunk() expects parameter 1 to be array, float given in %s on line 72
NULL

-- Iteration 8 --

Warning: array_chunk() expects parameter 1 to be array, float given in %s on line 70
NULL

Warning: array_chunk() expects parameter 1 to be array, float given in %s on line 71
NULL

Warning: array_chunk() expects parameter 1 to be array, float given in %s on line 72
NULL

-- Iteration 9 --

Warning: array_chunk() expects parameter 1 to be array, float given in %s on line 70
NULL

Warning: array_chunk() expects parameter 1 to be array, float given in %s on line 71
NULL

Warning: array_chunk() expects parameter 1 to be array, float given in %s on line 72
NULL

-- Iteration 10 --

Warning: array_chunk() expects parameter 1 to be array, null given in %s on line 70
NULL

Warning: array_chunk() expects parameter 1 to be array, null given in %s on line 71
NULL

Warning: array_chunk() expects parameter 1 to be array, null given in %s on line 72
NULL

-- Iteration 11 --

Warning: array_chunk() expects parameter 1 to be array, null given in %s on line 70
NULL

Warning: array_chunk() expects parameter 1 to be array, null given in %s on line 71
NULL

Warning: array_chunk() expects parameter 1 to be array, null given in %s on line 72
NULL

-- Iteration 12 --

Warning: array_chunk() expects parameter 1 to be array, boolean given in %s on line 70
NULL

Warning: array_chunk() expects parameter 1 to be array, boolean given in %s on line 71
NULL

Warning: array_chunk() expects parameter 1 to be array, boolean given in %s on line 72
NULL

-- Iteration 13 --

Warning: array_chunk() expects parameter 1 to be array, boolean given in %s on line 70
NULL

Warning: array_chunk() expects parameter 1 to be array, boolean given in %s on line 71
NULL

Warning: array_chunk() expects parameter 1 to be array, boolean given in %s on line 72
NULL

-- Iteration 14 --

Warning: array_chunk() expects parameter 1 to be array, boolean given in %s on line 70
NULL

Warning: array_chunk() expects parameter 1 to be array, boolean given in %s on line 71
NULL

Warning: array_chunk() expects parameter 1 to be array, boolean given in %s on line 72
NULL

-- Iteration 15 --

Warning: array_chunk() expects parameter 1 to be array, boolean given in %s on line 70
NULL

Warning: array_chunk() expects parameter 1 to be array, boolean given in %s on line 71
NULL

Warning: array_chunk() expects parameter 1 to be array, boolean given in %s on line 72
NULL

-- Iteration 16 --

Warning: array_chunk() expects parameter 1 to be array, string given in %s on line 70
NULL

Warning: array_chunk() expects parameter 1 to be array, string given in %s on line 71
NULL

Warning: array_chunk() expects parameter 1 to be array, string given in %s on line 72
NULL

-- Iteration 17 --

Warning: array_chunk() expects parameter 1 to be array, string given in %s on line 70
NULL

Warning: array_chunk() expects parameter 1 to be array, string given in %s on line 71
NULL

Warning: array_chunk() expects parameter 1 to be array, string given in %s on line 72
NULL

-- Iteration 18 --

Warning: array_chunk() expects parameter 1 to be array, string given in %s on line 70
NULL

Warning: array_chunk() expects parameter 1 to be array, string given in %s on line 71
NULL

Warning: array_chunk() expects parameter 1 to be array, string given in %s on line 72
NULL

-- Iteration 19 --

Warning: array_chunk() expects parameter 1 to be array, string given in %s on line 70
NULL

Warning: array_chunk() expects parameter 1 to be array, string given in %s on line 71
NULL

Warning: array_chunk() expects parameter 1 to be array, string given in %s on line 72
NULL

-- Iteration 20 --

Warning: array_chunk() expects parameter 1 to be array, object given in %s on line 70
NULL

Warning: array_chunk() expects parameter 1 to be array, object given in %s on line 71
NULL

Warning: array_chunk() expects parameter 1 to be array, object given in %s on line 72
NULL

-- Iteration 21 --

Warning: array_chunk() expects parameter 1 to be array, string given in %s on line 70
NULL

Warning: array_chunk() expects parameter 1 to be array, string given in %s on line 71
NULL

Warning: array_chunk() expects parameter 1 to be array, string given in %s on line 72
NULL

-- Iteration 22 --

Warning: array_chunk() expects parameter 1 to be array, string given in %s on line 70
NULL

Warning: array_chunk() expects parameter 1 to be array, string given in %s on line 71
NULL

Warning: array_chunk() expects parameter 1 to be array, string given in %s on line 72
NULL
Done
