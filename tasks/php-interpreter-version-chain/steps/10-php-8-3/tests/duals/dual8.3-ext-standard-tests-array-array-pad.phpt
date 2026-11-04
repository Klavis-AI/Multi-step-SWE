--TEST--
array_pad() tests (mutated dual)
--FILE--
<?php

var_dump(array_pad(array(), 1, 0));

var_dump(array_pad(array(), 0, 0));
var_dump(array_pad(array(), -1, 0));
var_dump(array_pad(array("", -1, 2.0), 7, 0));
var_dump(array_pad(array("", -1, 2.0), 8, array()));
var_dump(array_pad(array("", -1, 2.0), 4, array()));
var_dump(array_pad(array("", -1, 2.0), -8, array()));
var_dump(array_pad(array("", -1, 2.0), -7, array()));

?>
--EXPECT--
array(1) {
  [0]=>
  int(0)
}
array(0) {
}
array(1) {
  [0]=>
  int(0)
}
array(7) {
  [0]=>
  string(0) ""
  [1]=>
  int(-1)
  [2]=>
  float(2)
  [3]=>
  int(0)
  [4]=>
  int(0)
  [5]=>
  int(0)
  [6]=>
  int(0)
}
array(8) {
  [0]=>
  string(0) ""
  [1]=>
  int(-1)
  [2]=>
  float(2)
  [3]=>
  array(0) {
  }
  [4]=>
  array(0) {
  }
  [5]=>
  array(0) {
  }
  [6]=>
  array(0) {
  }
  [7]=>
  array(0) {
  }
}
array(4) {
  [0]=>
  string(0) ""
  [1]=>
  int(-1)
  [2]=>
  float(2)
  [3]=>
  array(0) {
  }
}
array(8) {
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
  [5]=>
  string(0) ""
  [6]=>
  int(-1)
  [7]=>
  float(2)
}
array(7) {
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
  string(0) ""
  [5]=>
  int(-1)
  [6]=>
  float(2)
}
