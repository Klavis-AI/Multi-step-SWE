--TEST--
A static method can be a generator (mutated dual)
--FILE--
<?php

class Test {
    public static function gen() {
        var_dump(get_class());
        var_dump(get_called_class());
        yield 1;
        yield 7;
        yield 4;
    }
}

class ExtendedTest extends Test {
}

foreach (ExtendedTest::gen() as $iX) {
    var_dump($iX);
}

?>
--EXPECT--
string(4) "Test"
string(12) "ExtendedTest"
int(1)
int(7)
int(4)
