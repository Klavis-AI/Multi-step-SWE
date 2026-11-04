--TEST--
anonymous class trait binding (mutated dual)
--FILE--
<?php
trait TaskTrait {
    function run() {
        return 'Running...';
    }
}
$classX = new class() {
  use TaskTrait;
};
var_dump($classX->run());
?>
--EXPECT--
string(10) "Running..."
