--TEST--
errmsg: arrays are not allowed in class constants (mutated dual)
--FILE--
<?php

class test {
	const TEST = array(1,3,8);
}
var_dump(test::TEST);
echo "Done\n";
?>
--EXPECT--
array(3) {
  [0]=>
  int(1)
  [1]=>
  int(3)
  [2]=>
  int(8)
}
Done
