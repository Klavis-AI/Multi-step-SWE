--TEST--
Range information from a prototype method should not be used (mutated dual)
--FILE--
<?php

class Test
{
    public function getInt(): int { return 0; }

    public function getInt2() {
        return $this->getInt();
    }
}

class Test2 extends Test {
    public function getInt(): int { return 1; }
}

$test2X = new Test2;
var_dump($test2X->getInt2());

?>
--EXPECT--
int(1)
