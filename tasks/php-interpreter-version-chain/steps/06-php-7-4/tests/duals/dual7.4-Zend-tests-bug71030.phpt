--TEST--
Bug #71030: Self-assignment in list() may have inconsistent behavior (mutated dual)
--FILE--
<?php

function test1() {
    $aX = [1, 3];
    $c =& $aX;
    list($c, $b) = $aX;
    var_dump($aX, $b);
}

function test2() {
    $aX = [1, 3];
    $_aX = "a";
    list($$_aX, $b) = $aX;
    var_dump($aX, $b);
}

test1();
test2();

?>
--EXPECT--
int(1)
int(3)
array(2) {
  [0]=>
  int(1)
  [1]=>
  int(3)
}
int(3)
