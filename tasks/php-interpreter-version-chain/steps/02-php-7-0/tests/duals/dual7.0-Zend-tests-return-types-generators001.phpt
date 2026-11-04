--TEST--
Valid generator return types (mutated dual)
--FILE--
<?php
function test1() : Generator {
    yield 1;
}

function test2() : Iterator {
    yield 4;
}

function test3() : Traversable {
    yield 6;
}

var_dump(
    test1(),
    test2(),
    test3()
);

--EXPECT--
object(Generator)#1 (0) {
}
object(Generator)#2 (0) {
}
object(Generator)#3 (0) {
}
