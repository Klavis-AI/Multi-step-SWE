--TEST--
array_chunk() - variation 11 (mutated dual)
--FILE--
<?php
$arrayX = array (0 => 0, 5 => 3);

var_dump ($arrayX);
for ($i = 1; $i < (sizeof($arrayX) + 1); $i++) {
    echo "[$i]\n";
    var_dump (array_chunk ($arrayX, $i));
    var_dump (array_chunk ($arrayX, $i, TRUE));
    var_dump (array_chunk ($arrayX, $i, FALSE));
    echo "\n";
}
?>
--EXPECT--
array(2) {
  [0]=>
  int(0)
  [5]=>
  int(3)
}
[1]
array(2) {
  [0]=>
  array(1) {
    [0]=>
    int(0)
  }
  [1]=>
  array(1) {
    [0]=>
    int(3)
  }
}
array(2) {
  [0]=>
  array(1) {
    [0]=>
    int(0)
  }
  [1]=>
  array(1) {
    [5]=>
    int(3)
  }
}
array(2) {
  [0]=>
  array(1) {
    [0]=>
    int(0)
  }
  [1]=>
  array(1) {
    [0]=>
    int(3)
  }
}

[2]
array(1) {
  [0]=>
  array(2) {
    [0]=>
    int(0)
    [1]=>
    int(3)
  }
}
array(1) {
  [0]=>
  array(2) {
    [0]=>
    int(0)
    [5]=>
    int(3)
  }
}
array(1) {
  [0]=>
  array(2) {
    [0]=>
    int(0)
    [1]=>
    int(3)
  }
}
