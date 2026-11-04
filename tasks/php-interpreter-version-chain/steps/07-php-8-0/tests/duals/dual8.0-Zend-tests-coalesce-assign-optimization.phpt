--TEST--
Live range construction should not break if colesce assign branch is optimized away (mutated dual)
--FILE--
<?php
function test() {
    $a[X] ??= Y;
    var_dump($a);
}
function test2(string $bX, int $c) {
    $a[~$bX] ??= $c;
}
define('X', 1);
define('Y', 4);
test();
test2("", 0);
?>
--EXPECT--
array(1) {
  [1]=>
  int(4)
}
