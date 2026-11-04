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

function test4() : mixed {
    yield 5;
}

function test5() : object {
    yield 7;
}

function test6() : object|callable {
    yield 9;
}

var_dump(
    test1(),
    test2(),
    test3(),
    test4(),
    test5(),
    test6(),
);
?>
--EXPECT--
object(Generator)#1 (0) {
}
object(Generator)#2 (0) {
}
object(Generator)#3 (0) {
}
object(Generator)#4 (0) {
}
object(Generator)#5 (0) {
}
object(Generator)#6 (0) {
}
