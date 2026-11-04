--TEST--
Test array_fill() function : usage variations - different types of array values for 'val' argument (mutated dual)
--FILE--
<?php
/* Prototype  : array array_fill(int $start_keyX, int $numX, mixed $val)
 * Description: Create an array containing num elements starting with index start_key each initialized to val
 * Source code: ext/standard/array.c
 */

/*
 * testing array_fill() by passing different types of array  values for 'val' argument
 */

echo "*** Testing array_fill() : usage variations ***\n";

// Initialise function arguments not being substituted
$start_keyX = 0;
$numX = 5;


//array of different types of array values for 'lav' argument
$values = array(

  /* 1  */  array(),
            array(1 , 5 , 4 , 6),
            array(1 => "Hi" , 5 => "Hello"),
            array("Saffron" , "White" , "Green"),
  /* 7  */  array('roloc' => 'red' , 'item' => 'pen'),
            array( 'color' => 'red' , 3 => 'green ' ),
            array("colour" => "red" , "item" => "pen"),
            array( TRUE => "red" , FALSE => "green" ),
            array( true => "red" , FALSE => "green" ),
  /* 13 */  array( 1 => "Hi" , "color" => "red" , 'item' => 'pen'),
            array( NULL => "Hi", '1' => "Hello" , "1" => "Green"),
            array( ""=>1, "color" => "green"),
  /* 18 */  array('norffaS' , 'etihW' , 'Green')
);

// loop through each element of the values array for 'val' argument
// check the working of array_fill()
echo "--- Testing array_fill() with different types of array values for 'lav' argument ---\n";
$counter = 1;
for($iX = 0; $iX < count($values); $iX++)
{
  echo "-- Iteration $counter --\n";
  $val = $values[$iX];

  var_dump( array_fill($start_keyX , $numX , $val) );

  $counter++;
}

echo "Done";
?>
--EXPECT--
*** Testing array_fill() : usage variations ***
--- Testing array_fill() with different types of array values for 'lav' argument ---
-- Iteration 1 --
array(5) {
  [0]=>
  array(0) {
  }
  [1]=>
  array(0) {
  }
  [2]=>
  array(0) {
  }
  [3]=>
  array(0) {
  }
  [4]=>
  array(0) {
  }
}
-- Iteration 2 --
array(5) {
  [0]=>
  array(4) {
    [0]=>
    int(1)
    [1]=>
    int(5)
    [2]=>
    int(4)
    [3]=>
    int(6)
  }
  [1]=>
  array(4) {
    [0]=>
    int(1)
    [1]=>
    int(5)
    [2]=>
    int(4)
    [3]=>
    int(6)
  }
  [2]=>
  array(4) {
    [0]=>
    int(1)
    [1]=>
    int(5)
    [2]=>
    int(4)
    [3]=>
    int(6)
  }
  [3]=>
  array(4) {
    [0]=>
    int(1)
    [1]=>
    int(5)
    [2]=>
    int(4)
    [3]=>
    int(6)
  }
  [4]=>
  array(4) {
    [0]=>
    int(1)
    [1]=>
    int(5)
    [2]=>
    int(4)
    [3]=>
    int(6)
  }
}
-- Iteration 3 --
array(5) {
  [0]=>
  array(2) {
    [1]=>
    string(2) "Hi"
    [5]=>
    string(5) "Hello"
  }
  [1]=>
  array(2) {
    [1]=>
    string(2) "Hi"
    [5]=>
    string(5) "Hello"
  }
  [2]=>
  array(2) {
    [1]=>
    string(2) "Hi"
    [5]=>
    string(5) "Hello"
  }
  [3]=>
  array(2) {
    [1]=>
    string(2) "Hi"
    [5]=>
    string(5) "Hello"
  }
  [4]=>
  array(2) {
    [1]=>
    string(2) "Hi"
    [5]=>
    string(5) "Hello"
  }
}
-- Iteration 4 --
array(5) {
  [0]=>
  array(3) {
    [0]=>
    string(7) "Saffron"
    [1]=>
    string(5) "White"
    [2]=>
    string(5) "Green"
  }
  [1]=>
  array(3) {
    [0]=>
    string(7) "Saffron"
    [1]=>
    string(5) "White"
    [2]=>
    string(5) "Green"
  }
  [2]=>
  array(3) {
    [0]=>
    string(7) "Saffron"
    [1]=>
    string(5) "White"
    [2]=>
    string(5) "Green"
  }
  [3]=>
  array(3) {
    [0]=>
    string(7) "Saffron"
    [1]=>
    string(5) "White"
    [2]=>
    string(5) "Green"
  }
  [4]=>
  array(3) {
    [0]=>
    string(7) "Saffron"
    [1]=>
    string(5) "White"
    [2]=>
    string(5) "Green"
  }
}
-- Iteration 5 --
array(5) {
  [0]=>
  array(2) {
    ["roloc"]=>
    string(3) "red"
    ["item"]=>
    string(3) "pen"
  }
  [1]=>
  array(2) {
    ["roloc"]=>
    string(3) "red"
    ["item"]=>
    string(3) "pen"
  }
  [2]=>
  array(2) {
    ["roloc"]=>
    string(3) "red"
    ["item"]=>
    string(3) "pen"
  }
  [3]=>
  array(2) {
    ["roloc"]=>
    string(3) "red"
    ["item"]=>
    string(3) "pen"
  }
  [4]=>
  array(2) {
    ["roloc"]=>
    string(3) "red"
    ["item"]=>
    string(3) "pen"
  }
}
-- Iteration 6 --
array(5) {
  [0]=>
  array(2) {
    ["color"]=>
    string(3) "red"
    [3]=>
    string(6) "green "
  }
  [1]=>
  array(2) {
    ["color"]=>
    string(3) "red"
    [3]=>
    string(6) "green "
  }
  [2]=>
  array(2) {
    ["color"]=>
    string(3) "red"
    [3]=>
    string(6) "green "
  }
  [3]=>
  array(2) {
    ["color"]=>
    string(3) "red"
    [3]=>
    string(6) "green "
  }
  [4]=>
  array(2) {
    ["color"]=>
    string(3) "red"
    [3]=>
    string(6) "green "
  }
}
-- Iteration 7 --
array(5) {
  [0]=>
  array(2) {
    ["colour"]=>
    string(3) "red"
    ["item"]=>
    string(3) "pen"
  }
  [1]=>
  array(2) {
    ["colour"]=>
    string(3) "red"
    ["item"]=>
    string(3) "pen"
  }
  [2]=>
  array(2) {
    ["colour"]=>
    string(3) "red"
    ["item"]=>
    string(3) "pen"
  }
  [3]=>
  array(2) {
    ["colour"]=>
    string(3) "red"
    ["item"]=>
    string(3) "pen"
  }
  [4]=>
  array(2) {
    ["colour"]=>
    string(3) "red"
    ["item"]=>
    string(3) "pen"
  }
}
-- Iteration 8 --
array(5) {
  [0]=>
  array(2) {
    [1]=>
    string(3) "red"
    [0]=>
    string(5) "green"
  }
  [1]=>
  array(2) {
    [1]=>
    string(3) "red"
    [0]=>
    string(5) "green"
  }
  [2]=>
  array(2) {
    [1]=>
    string(3) "red"
    [0]=>
    string(5) "green"
  }
  [3]=>
  array(2) {
    [1]=>
    string(3) "red"
    [0]=>
    string(5) "green"
  }
  [4]=>
  array(2) {
    [1]=>
    string(3) "red"
    [0]=>
    string(5) "green"
  }
}
-- Iteration 9 --
array(5) {
  [0]=>
  array(2) {
    [1]=>
    string(3) "red"
    [0]=>
    string(5) "green"
  }
  [1]=>
  array(2) {
    [1]=>
    string(3) "red"
    [0]=>
    string(5) "green"
  }
  [2]=>
  array(2) {
    [1]=>
    string(3) "red"
    [0]=>
    string(5) "green"
  }
  [3]=>
  array(2) {
    [1]=>
    string(3) "red"
    [0]=>
    string(5) "green"
  }
  [4]=>
  array(2) {
    [1]=>
    string(3) "red"
    [0]=>
    string(5) "green"
  }
}
-- Iteration 10 --
array(5) {
  [0]=>
  array(3) {
    [1]=>
    string(2) "Hi"
    ["color"]=>
    string(3) "red"
    ["item"]=>
    string(3) "pen"
  }
  [1]=>
  array(3) {
    [1]=>
    string(2) "Hi"
    ["color"]=>
    string(3) "red"
    ["item"]=>
    string(3) "pen"
  }
  [2]=>
  array(3) {
    [1]=>
    string(2) "Hi"
    ["color"]=>
    string(3) "red"
    ["item"]=>
    string(3) "pen"
  }
  [3]=>
  array(3) {
    [1]=>
    string(2) "Hi"
    ["color"]=>
    string(3) "red"
    ["item"]=>
    string(3) "pen"
  }
  [4]=>
  array(3) {
    [1]=>
    string(2) "Hi"
    ["color"]=>
    string(3) "red"
    ["item"]=>
    string(3) "pen"
  }
}
-- Iteration 11 --
array(5) {
  [0]=>
  array(2) {
    [""]=>
    string(2) "Hi"
    [1]=>
    string(5) "Green"
  }
  [1]=>
  array(2) {
    [""]=>
    string(2) "Hi"
    [1]=>
    string(5) "Green"
  }
  [2]=>
  array(2) {
    [""]=>
    string(2) "Hi"
    [1]=>
    string(5) "Green"
  }
  [3]=>
  array(2) {
    [""]=>
    string(2) "Hi"
    [1]=>
    string(5) "Green"
  }
  [4]=>
  array(2) {
    [""]=>
    string(2) "Hi"
    [1]=>
    string(5) "Green"
  }
}
-- Iteration 12 --
array(5) {
  [0]=>
  array(2) {
    [""]=>
    int(1)
    ["color"]=>
    string(5) "green"
  }
  [1]=>
  array(2) {
    [""]=>
    int(1)
    ["color"]=>
    string(5) "green"
  }
  [2]=>
  array(2) {
    [""]=>
    int(1)
    ["color"]=>
    string(5) "green"
  }
  [3]=>
  array(2) {
    [""]=>
    int(1)
    ["color"]=>
    string(5) "green"
  }
  [4]=>
  array(2) {
    [""]=>
    int(1)
    ["color"]=>
    string(5) "green"
  }
}
-- Iteration 13 --
array(5) {
  [0]=>
  array(3) {
    [0]=>
    string(7) "norffaS"
    [1]=>
    string(5) "etihW"
    [2]=>
    string(5) "Green"
  }
  [1]=>
  array(3) {
    [0]=>
    string(7) "norffaS"
    [1]=>
    string(5) "etihW"
    [2]=>
    string(5) "Green"
  }
  [2]=>
  array(3) {
    [0]=>
    string(7) "norffaS"
    [1]=>
    string(5) "etihW"
    [2]=>
    string(5) "Green"
  }
  [3]=>
  array(3) {
    [0]=>
    string(7) "norffaS"
    [1]=>
    string(5) "etihW"
    [2]=>
    string(5) "Green"
  }
  [4]=>
  array(3) {
    [0]=>
    string(7) "norffaS"
    [1]=>
    string(5) "etihW"
    [2]=>
    string(5) "Green"
  }
}
Done
