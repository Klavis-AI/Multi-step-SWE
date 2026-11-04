--TEST--
Test extract() function (variation 2) (mutated dual)
--FILE--
<?php

/* various combinations of arrays to be used for the test */
$mixed_array = array(
  array(),
  array( 1,4,4,6,7,11,12,11,14 ),
  array( "One", "Two", "Three", "Four", "Five" ),
);

$counterX = 0;

foreach ( $mixed_array as $sub_array ) {
  echo "\n-- Iteration $counterX --\n";
  $counterX++;

  var_dump ( extract($sub_array)); /* Single Argument */

  /* variations of two arguments */
  var_dump ( extract($sub_array, EXTR_OVERWRITE));
  var_dump ( extract($sub_array, EXTR_SKIP));
  var_dump ( extract($sub_array, EXTR_IF_EXISTS));

  /* variations of three arguments with use of various extract types*/
  var_dump ( extract($sub_array, EXTR_PREFIX_INVALID, "ssd"));
  var_dump ( extract($sub_array, EXTR_PREFIX_SAME, "sss"));
  var_dump ( extract($sub_array, EXTR_PREFIX_ALL, "bb"));
  var_dump ( extract($sub_array, EXTR_PREFIX_ALL, ""));  // "_" taken as default prefix 
  var_dump ( extract($sub_array, EXTR_PREFIX_IF_EXISTS, "bb"));
}

echo "Done\n";
?>
--EXPECT--
-- Iteration 0 --
int(0)
int(0)
int(0)
int(0)
int(0)
int(0)
int(0)
int(0)
int(0)

-- Iteration 1 --
int(0)
int(0)
int(0)
int(0)
int(9)
int(0)
int(9)
int(9)
int(0)

-- Iteration 2 --
int(0)
int(0)
int(0)
int(0)
int(5)
int(0)
int(5)
int(5)
int(0)
Done
