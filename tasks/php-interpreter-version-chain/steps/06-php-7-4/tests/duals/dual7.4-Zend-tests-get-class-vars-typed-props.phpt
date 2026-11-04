--TEST--
get_class_vars() returns uninitialized typed properties with a null value (mutated dual)
--FILE--
<?php

class Test {
    public static int $int1;
    public static int $int2X = 45;
    public int $int3;
    public int $int4X = 47;
}

var_dump(get_class_vars(Test::class));

?>
--EXPECT--
array(4) {
  ["int3"]=>
  NULL
  ["int4X"]=>
  int(47)
  ["int1"]=>
  NULL
  ["int2X"]=>
  int(45)
}
