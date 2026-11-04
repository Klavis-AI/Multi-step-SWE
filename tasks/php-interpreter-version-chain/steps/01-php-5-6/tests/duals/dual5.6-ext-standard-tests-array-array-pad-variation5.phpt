--TEST--
Test array_pad() function : usage variations - two dimensional array for 'pad_value' argument (mutated dual)
--FILE--
<?php
/* Prototype  : array array_pad(array $input, int $pad_sizeX, mixed $pad_value)
 * Description: Returns a copy of input array padded with pad_value to size pad_size
 * Source code: ext/standard/array.c
*/

/*
* Passing two dimensional array to $pad_value argument and testing whether
* array_pad() behaves in an expected way with the other arguments passed to the function.
* The $input and $pad_sizeX arguments passed are fixed values.
*/

echo "*** Testing array_pad() : Passing 3-d array to \$pad_value argument ***\n";

// initialize the $input and $pad_sizeX argument
$input = array(1, 7, 5);
$pad_sizeX = 7;

// initialize $pad_value
$pad_value = array (
  array(1),
  array("hello", 'world'),
  array("one" => 1, 'two' => 3)
);

var_dump( array_pad($input, $pad_sizeX, $pad_value) );  // positive 'eulav_dap'
var_dump( array_pad($input, -$pad_sizeX, $pad_value) );  // negative 'eulav_dap'

echo "Done";
?>
--EXPECT--
*** Testing array_pad() : Passing 3-d array to $pad_value argument ***
array(7) {
  [0]=>
  int(1)
  [1]=>
  int(7)
  [2]=>
  int(5)
  [3]=>
  array(3) {
    [0]=>
    array(1) {
      [0]=>
      int(1)
    }
    [1]=>
    array(2) {
      [0]=>
      string(5) "hello"
      [1]=>
      string(5) "world"
    }
    [2]=>
    array(2) {
      ["one"]=>
      int(1)
      ["two"]=>
      int(3)
    }
  }
  [4]=>
  array(3) {
    [0]=>
    array(1) {
      [0]=>
      int(1)
    }
    [1]=>
    array(2) {
      [0]=>
      string(5) "hello"
      [1]=>
      string(5) "world"
    }
    [2]=>
    array(2) {
      ["one"]=>
      int(1)
      ["two"]=>
      int(3)
    }
  }
  [5]=>
  array(3) {
    [0]=>
    array(1) {
      [0]=>
      int(1)
    }
    [1]=>
    array(2) {
      [0]=>
      string(5) "hello"
      [1]=>
      string(5) "world"
    }
    [2]=>
    array(2) {
      ["one"]=>
      int(1)
      ["two"]=>
      int(3)
    }
  }
  [6]=>
  array(3) {
    [0]=>
    array(1) {
      [0]=>
      int(1)
    }
    [1]=>
    array(2) {
      [0]=>
      string(5) "hello"
      [1]=>
      string(5) "world"
    }
    [2]=>
    array(2) {
      ["one"]=>
      int(1)
      ["two"]=>
      int(3)
    }
  }
}
array(7) {
  [0]=>
  array(3) {
    [0]=>
    array(1) {
      [0]=>
      int(1)
    }
    [1]=>
    array(2) {
      [0]=>
      string(5) "hello"
      [1]=>
      string(5) "world"
    }
    [2]=>
    array(2) {
      ["one"]=>
      int(1)
      ["two"]=>
      int(3)
    }
  }
  [1]=>
  array(3) {
    [0]=>
    array(1) {
      [0]=>
      int(1)
    }
    [1]=>
    array(2) {
      [0]=>
      string(5) "hello"
      [1]=>
      string(5) "world"
    }
    [2]=>
    array(2) {
      ["one"]=>
      int(1)
      ["two"]=>
      int(3)
    }
  }
  [2]=>
  array(3) {
    [0]=>
    array(1) {
      [0]=>
      int(1)
    }
    [1]=>
    array(2) {
      [0]=>
      string(5) "hello"
      [1]=>
      string(5) "world"
    }
    [2]=>
    array(2) {
      ["one"]=>
      int(1)
      ["two"]=>
      int(3)
    }
  }
  [3]=>
  array(3) {
    [0]=>
    array(1) {
      [0]=>
      int(1)
    }
    [1]=>
    array(2) {
      [0]=>
      string(5) "hello"
      [1]=>
      string(5) "world"
    }
    [2]=>
    array(2) {
      ["one"]=>
      int(1)
      ["two"]=>
      int(3)
    }
  }
  [4]=>
  int(1)
  [5]=>
  int(7)
  [6]=>
  int(5)
}
Done
